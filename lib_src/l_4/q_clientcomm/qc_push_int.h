/**
 * @file qc_push_int.h
 * @brief l_4/q_clientcomm 【内部头】：M5 推送运行期结构（不随 install_hdr 归集）
 * @platform Windows / Linux / macOS
 * @layer  lib_src/l_4/q_clientcomm（仅供模块内 .c 使用；对外 API 见公开头 qc_push.h）
 */
#ifndef QC_PUSH_INT_H
#define QC_PUSH_INT_H

#include "qc_push.h"
#include "qc_keeper.h"     /* 平台原语（线程/互斥/时钟）+ struct qc_session */

/* ===================== 运行期对象 ===================== */
struct qc_push {
    qc_push_cfg_t cfg;                  /* 启动参数副本（重建用；不含口令/派生值） */

    qc_session_t *sess;                 /* 第二条长连接（独立 chan / 独立 seq 空间） */

    /* 回调 */
    qc_traj_cb        on_traj;
    qc_alarm_cb       on_alarm;
    void             *cb_ud;
    qc_push_relogin_cb on_relogin;
    void             *relogin_ud;

    /* 订阅集合 + 连接状态（lock 保护） */
    qc_mutex_t  lock;
    int         lock_inited;
    char      (*vids)[QC_PUSH_KEY_LEN]; /* 动态数组：车辆主键集合 */
    uint32_t    nvid;
    uint32_t    cap_vid;
    volatile int connected;             /* 1 = 推送连接就绪（BEGIN 成功） */

    /* 接收线程 */
    qc_thread_t   thread;
    int           thread_on;            /* 线程已起（join 用） */
    volatile int  stop;                 /* 停止请求 */
    uint32_t      attempts;             /* 连续重建失败次数（退避用） */
};

/* ===================== 模块内部函数（不对外导出） ===================== */

/* 建立整条推送连接（knock -> connect -> hello -> salt -> login -> BEGIN -> 重放订阅）。
   password 非 NULL：用它在本地派生 pwd_hash（初始启动路径）；
   password 为 NULL：经 on_relogin 向 UI 索取 pwd_hash（重连路径）。
   返回 0 成功；>0 服务端结果码；<0 失败（-2 = UI 无法提供口令）。 */
int qc_push_connect(qc_push_t *p, const char *password);

/* 接收线程主循环：阻塞收帧 -> demux(TRAJ/ALARM) -> 断线则退避重建。 */
QC_THREAD_RET qc_push_thread(QC_THREAD_ARG arg);

#endif /* QC_PUSH_INT_H */
