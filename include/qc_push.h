/**
 * @file qc_push.h
 * @brief l_4/q_clientcomm 对外头：M5 实时推送长连接（三端 UI 共用的 C ABI 门面）
 * @platform Windows / Linux / macOS
 * @layer  lib_src/l_4/q_clientcomm  ->  产出 libqclientcomm
 *
 * 定位：客户端**第二条独立长连接**（轨迹 + 报警），与命令连接（qc_session/keeper
 *       治理的那条）完全平行、互不持锁：
 *         - 独立 socket / 独立 q_chan（独立 seq 空间，防重放窗口交叉）；
 *         - 独立 knockd/netd 实例（地址由配置 push.* 给出，支持域名）；
 *         - 独立线程（接收/分发/重连），**不走命令通道的 io_lock gate** ——
 *           否则一次慢查询会把实时流一起卡住，违背"不影响其他指令交互"的初衷。
 *
 * 线程模型：UI 线程只**发**（qc_push_sub_delta）；接收线程只**收**（TRAJ/ALARM，
 *       顺带负责断线重建）。q_chan 的 seq_send 与接收窗口是不同字段，
 *       一线程发 + 一线程收无数据竞争。
 *
 * 口令红线：本模块**不存口令、不存 pwd_hash**。初始建连的口令由
 *       qc_push_start 的调用方瞬态传入（栈上，用完即清）；断线重连需要的
 *       pwd_hash 经 on_relogin 回调向 UI 索取（与 keeper 同一约定）。
 *
 * 订阅集合：本地是**唯一真相**（重连时全量重放）。UI 每次只发变动 delta，
 *       大集合自动分片（Q_PUSH_SHARD_MAX/帧），调用方无需手动分批。
 */
#ifndef L_4_QC_PUSH_H
#define L_4_QC_PUSH_H

#include "qc_client.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

    /* ===================== 推送载荷（与 native ABI 解耦的轻量表示） ===================== */
    /* 车辆主键长度：d_vehicleinfo.vehicle_id 为 CHAR(32)（车牌+颜色），留足余量 */
#define QC_PUSH_KEY_LEN 40

    /* 轨迹点：一车一点一帧（事件驱动，无分页、无固定节奏）。 */
    typedef struct qc_traj {
        char     vehicle_id[QC_PUSH_KEY_LEN];
        double   lon;
        double   lat;
        uint32_t speed_kph;
        uint32_t online;
        uint32_t alarm_flags;   /* Q_PUSH_ALARM_* 位掩码 */
        char     time[24];      /* "YYYY-MM-DD HH:MM:SS" */
        char     address[256];
    } qc_traj_t;

    /* 报警事件：按"用户组织子树"过滤后逐条下发。 */
    typedef struct qc_alarm {
        char     vehicle_id[QC_PUSH_KEY_LEN];
        char     org_id[QC_PUSH_KEY_LEN];  /* 报警所属组织 */
        char     plate[16];
        uint32_t alarm_type;               /* Q_PUSH_ALARM_* 单值 */
        uint32_t reserved;
        double   lon;
        double   lat;
        char     time[24];
        char     text[256];     /* 报警描述 */
        char     address[256];  /* 报警位置 */
    } qc_alarm_t;

    /* 回调（**由接收线程调用**，实现方必须自行编组到 UI 线程）；
       载荷指针在回调返回后即失效，调用方必须在回调内拷贝。 */
    typedef void (*qc_traj_cb)(const qc_traj_t *t, void *ud);
    typedef void (*qc_alarm_cb)(const qc_alarm_t *a, void *ud);

    /* 重连时向 UI 索取口令派生值（与 keeper 的 qc_relogin_cb 同构）。
       返回 0 = 已填好 pwd_hash_out；非 0 = 无法提供（本轮重建放弃，退避后重试）。 */
    typedef int (*qc_push_relogin_cb)(void *ud, const uint8_t *salt, size_t salt_len,
                                      uint32_t iter, uint8_t pwd_hash_out[32]);

    /* ===================== 启动参数（不含口令） ===================== */
    /* 服务端 SM2 公钥 DER 上限（SM2 公钥 DER 约 91B；与 qc_session 留同等余量） */
#define QC_PUSH_PUB_DER_MAX 1024u

    typedef struct qc_push_cfg {
        char     host[QC_HOST_MAX];         /* netd_B：IPv4 或域名 */
        uint16_t port;                      /* netd_B TCP 端口 */
        char     knock[QC_HOST_MAX];        /* knockd_B UDP 端点 "host:port"；空 = 不敲门 */
        uint8_t  spa_key[32];
        size_t   spa_key_len;
        uint8_t  spa_key_id;
        uint8_t  pub_der[QC_PUSH_PUB_DER_MAX];   /* 服务端 SM2 公钥（与命令连接共用一套身份密钥） */
        size_t   pub_len;
        uint64_t seq0;
        uint16_t key_id;
        char     user[QC_USER_MAX];
        char     otp[QC_OTP_MAX];
        uint8_t  devid[32];
        size_t   devid_len;
        char     cver[QC_VER_MAX];
        uint32_t pbkdf2_iter;               /* 0 = 协议默认 */
        uint32_t backoff_base_ms;           /* 重建退避基数（默认 1000） */
        uint32_t backoff_max_ms;            /* 重建退避上限（默认 30000） */
    } qc_push_cfg_t;

    /* 运行期对象（不透明；内部持第二条连接 + 接收线程 + 订阅集合） */
    typedef struct qc_push qc_push_t;

    /* ===================== 对外 API ===================== */

    /** 创建推送对象并拷走启动参数（不含口令）。cfg 必填。失败返回 NULL。 */
    qc_push_t *qc_push_create(const qc_push_cfg_t *cfg);

    /** 销毁：先停线程再释放。幂等（p 可为 NULL）。 */
    void qc_push_destroy(qc_push_t *p);

    /** 注册回调（traj/alarm 可为 NULL 表示不关心；relogin 重连必需，否则重建必然失败）。 */
    int qc_push_set_callbacks(qc_push_t *p, qc_traj_cb traj, qc_alarm_cb alarm,
                              qc_push_relogin_cb relogin, void *ud);

    /**
     * 启动：knock -> connect -> hello -> salt -> 派生(pwd_hash) -> login -> PUSH_BEGIN，
     * 成功后拉起接收线程。**阻塞**（含网络往返），调用方须放后台线程。
     *   password : 瞬态口令（仅本次建连用，用完即清；内核不存）。重连时不问这里，
     *              而是走 on_relogin 回调向 UI 索取。
     * 返回 0 成功（接收线程已在跑）；<0 失败（连接不上/登录失败/BEGIN 被拒）。
     * 注意：失败由调用方决定降级策略（如 native 转本地 mock），本模块不生成假数据。
     */
    int qc_push_start(qc_push_t *p, const char *password);

    /** 停止（幂等）：置停止标志 -> shutdown 唤醒阻塞 recv -> join 线程 -> 关连接。
     *  订阅集合保留（下次 start 会重放），显式 qc_push_destroy 才整体释放。 */
    int qc_push_stop(qc_push_t *p);

    /**
     * 增量订阅：op = Q_PUSH_OP_ADD / Q_PUSH_OP_REMOVE；ids 为车辆主键字符串数组。
     * 语义：**先改本地集合（唯一真相），再尝试发送**。未连接时只改集合，
     *       连接恢复后由重放逻辑全量补发 —— 调用方无需关心连接状态。
     * 大集合自动分片（每帧 <= Q_PUSH_SHARD_MAX 个 ID）。
     * 返回 0；-1 参数非法 / 内存不足。
     */
    int qc_push_sub_delta(qc_push_t *p, int op, const char *const *ids, uint32_t n);

    /** 当前订阅车辆数（诊断/UI 显示用）。 */
    uint32_t qc_push_sub_count(qc_push_t *p);

    /** 推送连接是否就绪（1/0）。 */
    int qc_push_connected(qc_push_t *p);

#ifdef __cplusplus
}
#endif

#endif /* L_4_QC_PUSH_H */
