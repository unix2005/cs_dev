/**
 * @file srv_push.h
 * @brief 实时推送微服务（q_service_push / pushd）公共头文件
 * @platform Linux（reactor + UDS）；macOS 可编译运行（kqueue）
 *
 * @note 定位：M5 推送栈的【下游微服务】，与 bizd 下游的 q_s_vmsv1__biz **平级**，
 *       但连接模型相反：
 *         - biz   ：bizd 每请求新建 TCP 连接、一问一答后关闭（请求/响应）；
 *         - pushd ：netd_B 为每条推送客户端连接**持有一条持久 UDS**，
 *                   C→S（BEGIN/SUB_DELTA）与 S→C（TRAJ/ALARM）在同一条 UDS 上双向流动。
 *
 *       wire 契约（与 netd 的 srv_netd.h / netd_ipc.c 一致，勿漂移）：
 *       - 传输：Unix Socket；分帧 [4 字节大端长度][TLV 序列]。
 *       - netd_B -> pushd（请求）：
 *           BEGIN     : TLV(CMD=0x0050) + TLV(SRCIP) + TLV(CHANID) + TLV(USERID)
 *                       + TLV(TOKEN)? + TLV(USERNAME)?
 *                       应答：TLV(RESULT=uint32 BE)
 *           SUB_DELTA : TLV(CMD=0x0051) + TLV(CHANID) + TLV(BODY=推送域 TLV 序列)
 *                       免回执（fire-and-forget）
 *       - pushd -> netd_B（推送，无请求上下文）：
 *           TLV(CMD=0x0052|0x0053) + TLV(BODY=推送域 TLV 序列)
 *           netd_B 收后按 CMD 加密下发给客户端。
 *
 *       推送域 TLV（Q_IPC_T_PUSH_*）与客户端密文体内层**共用同一组 tag**，
 *       定义在 include_ipc_proto.h，严禁各端重复定义。
 *
 * 并发模型（与 biz 的根本差异，务必先读）：
 *   - 每条 UDS 连接 = 一个推送会话，由线程池的一个 worker **独占读**（阻塞 recv）；
 *   - 模拟线程（push_sim.c）**统一写**所有会话的 TRAJ/ALARM；
 *   - 所有 socket 写（worker 的 BEGIN 应答 + 模拟线程的推送帧）都在 g_lock 内完成，
 *     单锁串行化 —— 测试桩会话数是个位数，简单且不会写坏帧；真实部署应改每会话
 *     写锁 + 会话表分片，此处刻意不预支这个复杂度。
 *   - 会话的增删/订阅集合修改同样在 g_lock 内；worker 退出时先摘链表再关 fd，
 *     而模拟线程整趟发送都持锁，故不会出现"边发边关"。
 */
#ifndef _SRV_PUSH_H_
#define _SRV_PUSH_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "include_stdio.h"
#include "include_net.h"
#include "include_thd.h"
#include "include_time.h"

#include "q_common.h"          /* l_1 守护进程公共：信号 / 降权 */
#include "q_reactor.h"         /* l_1 事件循环 + 有界线程池 */
#include "q_net.h"             /* l_2 socket 封装（非阻塞/阻塞收发） */
#include "q_ipc.h"             /* l_2 Unix Domain Socket */
#include "q_codec.h"           /* l_2 TLV 编解码 */
#include "q_log.h"             /* l_1 日志 / 审计 */
#include "q_mem.h"             /* l_1 安全清零 */
#include "q_util.h"            /* l_1 工具 */
#include "q_chan.h"            /* 仅取 Q_CHAN_CMD_* 指令码，避免重复定义 wire 契约 */
#include "include_ipc_proto.h" /* Q_IPC_T_* / Q_PUSH_* / Q_IPC_R_*（三端共用同一事实来源） */

/* ===================== 默认配置 ===================== */
#define PUSH_SOCK_DEF        "/var/run/gtkvms/pushd.sock"
#define PUSH_BACKLOG_DEF     16
#define PUSH_NWORKERS_DEF    4       /* 每会话一个 worker；netd_B worker 数同级即可 */
#define PUSH_POLL_MS_DEF     200
#define PUSH_IO_TIMEOUT_SEC  10      /* 收发超时：防异常对端长期占用 worker */
#define PUSH_SND_TIMEOUT_SEC 5       /* 模拟线程发送超时：UI 不读时 5s 判死，防整链停滞 */
#define PUSH_MAXMSG          65536
#define PUSH_LENPFX          4
#define PUSH_TOKEN_LEN       32      /* token 明文字节数；hex 后 64 字符 */

/* 单帧订阅集合上限：与 Q_PUSH_SHARD_MAX 对齐（netd_B 按 500/帧分片转发） */
#define PUSH_SHARD_MAX       Q_PUSH_SHARD_MAX
/* 单会话订阅车辆数硬上限：防配置错误把内存吃光（真实系统按组织勾选到不了这个量级） */
#define PUSH_SUBS_MAX        100000u

/* ===================== M5 桩数据规模（不连库的替身） =====================
 * 真实部署时车辆位置/报警由 GPS 上行与报警规则产生；此处用确定性算法模拟，
 * 保证同一时刻各客户端看到的同一台车状态一致。 */
#define PUSH_VEH_MAX         6u       /* 测试车数（--vehicles 可调 1..6） */
#define PUSH_TICK_MS_DEF     100u     /* 模拟步长（ms） */
#define PUSH_EMIT_MS_DEF     1000u    /* 每台车轨迹上报间隔（ms），错峰叠加相位 */
#define PUSH_ALARM_WINOW_MS  5000u    /* 报警活跃窗口：窗口内 TRAJ 的 AFLAGS 带该位 */
#define PUSH_USER_MAX        64       /* 用户名最大长度 */
#define PUSH_ID_MAX          40       /* 主键字符串容量：org_id CHAR(36) / vehicle_id CHAR(32) */

/* ===================== 运行期配置 ===================== */
typedef struct
{
    char      sock_path[256];    /* UDS 监听路径（netd_B 的 push.ipc_path 必须一致） */
    int       backlog;
    int       nworkers;
    int       poll_ms;
    int       io_timeout_sec;

    uint32_t  vehicles;          /* 参与模拟的车辆数（1..PUSH_VEH_MAX） */
    uint32_t  tick_ms;           /* 模拟步长 */
    uint32_t  emit_ms;           /* 轨迹上报间隔 */
    int       no_alarms;         /* 1 = 不产生报警（只测轨迹） */
    int       require_token;     /* 1=校验 token 非全零（默认开；0=放行，便于裸协议调试） */

    char      log_file[256];     /* 空 = 仅控制台 */
    int       log_level;
    int       log_console;
    uint64_t  log_max_size;
} push_cfg_t;

struct push_ctx
{
    q_reactor_t   *reactor;
    q_tpool_t     *pool;
    q_log_t       *log;
    q_fd_t         listen_fd;
};

/* ===================== 推送会话 =====================
 * 一个会话 = 一条 netd_B 来的持久 UDS 连接（1:1 一条推送客户端连接）。
 * worker 线程独占读；订阅集合与 socket 写在 g_lock 保护下由模拟线程/worker 共用。 */
typedef struct push_session
{
    int                fd;
    int                active;      /* BEGIN 已应答（此前不收发业务数据） */
    int                dead;        /* 发送失败等：置位后 worker 负责拆链 */
    char               user_name[PUSH_USER_MAX];
    char               org_id[PUSH_ID_MAX];  /* 用户所属组织：报警子树过滤根 */
    uint8_t            user_id[32];
    uint8_t            chan_id[32];
    char             (*vids)[PUSH_ID_MAX];   /* 订阅车辆主键集合（动态数组） */
    uint32_t           nvid;
    uint32_t           cap_vid;
    struct push_session *next;
} push_session_t;

/* 每连接上下文：accept 后分配，worker 线程独占（无需加锁） */
typedef struct
{
    q_fd_t         fd;
    struct push_ctx *g;
} push_conn_t;

/* ===================== 全局对象 ===================== */
extern push_cfg_t      g_pcfg;    /* 定义于 main.c */
extern struct push_ctx g_push;    /* 定义于 main.c */
/* 会话表 + 订阅集合 + socket 写串行化的唯一锁（见文件头并发模型） */
extern q_mutex_t       g_lock;    /* 定义于 push_srv.c */

/* ===================== 配置（main.c） ===================== */
void push_cfg_defaults(void);

/* ===================== 工具（push_util.c） ===================== */
void     push_put_be32(uint8_t *p, uint32_t v);
uint32_t push_get_be32(const uint8_t *p);
int      push_hex_encode(const uint8_t *b, size_t len, char *out, size_t outcap);
int      push_set_io_timeout(q_fd_t fd, int sec);
int      push_send_frame(q_fd_t fd, const uint8_t *buf, size_t len);
int      push_recv_frame(q_fd_t fd, uint8_t *buf, size_t cap, size_t *outlen);
/* 当前时刻 "YYYY-MM-DD HH:MM:SS"（本地时区；桩数据用，不与真实 GPS 时间源对接） */
void     push_now_str(char *dst, size_t cap);

/* ===================== 会话管理（push_srv.c，g_lock 内调用） ===================== */
/* 注册会话（BEGIN 处理内调用，持锁）；成功 0 */
int  push_session_add(push_session_t *s);
/* 摘除会话（worker 退出时调用，持锁） */
void push_session_remove(push_session_t *s);
/* 取会话链表头（调用方须持 g_lock） */
push_session_t *push_session_first(void);

/* 订阅集合维护（持锁调用）：ADD 去重 / REMOVE 交换尾部 / 幂等；
   返回 0；-1 超上限或内存不足 */
int  push_sub_add(push_session_t *s, const char *vid);
void push_sub_remove(push_session_t *s, const char *vid);

/* ===================== 业务处理（push_handler.c） ===================== */
/* 处理一条 netd_B 来的帧（持 g_lock 调用：可能要回 BEGIN 应答）。
   返回 0 继续；<0 连接应关闭。 */
int push_handle_frame(push_session_t *s, const uint8_t *frame, size_t flen);

/* ===================== 桩数据 + 模拟（push_stub.c / push_sim.c） ===================== */
/* 组织子树判定：org 是否在 root 的子树内（org==root 或 root 是 org 的祖先）。
   任一侧为空串返回 0。 */
int  push_org_in_subtree(const char *root, const char *org);
/* 用户名 -> 组织（测试表）；未知用户返回根组织 "1"（fail-open，仅桩行为） */
const char *push_user_org(const char *user_name);
/* 车辆数 / 取第 i 台车（只读，供模拟线程与自检使用） */
uint32_t    push_veh_count(void);
const char *push_veh_id(uint32_t i);
const char *push_veh_org(uint32_t i);
const char *push_veh_plate(uint32_t i);

/* 构造 TRAJ / ALARM 的密文体内层 TLV（与 qc_push_parse_traj/parse_alarm 对齐）；
   返回 TLV 序列长度，0 表示失败。 */
size_t push_build_traj(uint8_t *out, size_t cap, const char *vid,
                       double lon, double lat, uint32_t speed, uint32_t online,
                       uint32_t alarm_flags);
size_t push_build_alarm(uint8_t *out, size_t cap, const char *vid, const char *org,
                        const char *plate, uint32_t alarm_type,
                        double lon, double lat, const char *text, const char *addr);

/* 模拟线程主体（main.c 启动）：推进车辆 -> 按订阅/组织子树下发 TRAJ/ALARM */
void *push_sim_thread(void *arg);
/* 创建/Join 模拟线程（main.c 用） */
int  push_sim_start(pthread_t *tid);
void push_sim_join(pthread_t tid);
/* 桩数据复位到指定时刻（自检/启动各调一次，保证确定性） */
void push_stub_reset(uint64_t now_ms);

/* ===================== 服务器（push_srv.c） ===================== */
void push_on_accept(q_fd_t fd, uint32_t events, void *ctx);
void push_conn_worker(void *arg);

/* ===================== 自检（--selftest，push_stub.c） ===================== */
int  push_selftest(void);

#ifdef __cplusplus
}
#endif

#endif /* _SRV_PUSH_H_ */
