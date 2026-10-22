/**
 * @file qc_keeper.h
 * @brief l_4/q_clientcomm 【内部头】：会话治理（M4）
 * @platform Windows / Linux / macOS
 * @layer  lib_src/l_4/q_clientcomm（不随 install_hdr 归集到 include/，仅供模块内 .c 使用）
 *
 * 为什么要有这一层：
 *   客户端是长连接 + 多业务线程（列表查询 / 定位数据 / 视频），而协议是严格 lockstep
 *   （一问一答，禁止在收到回包前再发），且后台 Token 有 TTL(1800s)、心跳**不续期**。
 *   任一条被破坏的表现都是"UI 偶发卡死 / 偶发结果码 3 / 偶发解包错位"，极难排查。
 *   故把「心跳、断线重连、Token 失效重登、请求串行化」收敛到通信内核，三端共用一份。
 *
 * 线程模型（一会话一 gate）：
 *   - keeper 线程：按 hb_interval 发心跳；失败走退避重连；心跳/业务返回 3 时重新登录。
 *   - 业务线程（UI 自己开的）：调用 qc_request / qc_biz_query **之前**必须
 *     qc_keeper_lock()，返回后 qc_keeper_unlock()。心跳与业务因此严格互斥，
 *     不会出现两路请求交错破坏 lockstep。
 *   - 状态回调 on_state 由 keeper 线程发出：回调内**禁止**直接碰 UI 对象，
 *     必须把事件投递到各自的 UI 线程再处理（C#: DispatcherQueue；GTK: g_idle_add）。
 *
 * 口令处理（安全红线）：
 *   本层**不保存口令，也不保存口令派生值**。重新登录需要 pwd_hash 时，
 *   通过 cfg.on_relogin 回调向 UI 索取 —— UI 侧自己决定是缓存口令、弹窗重输，
 *   还是走系统凭据管理器。回调返回非 0 = 无法提供，keeper 转 OFFLINE 交给 UI 处理。
 */
#ifndef QC_KEEPER_H
#define QC_KEEPER_H

#include "qc_client.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "q_chan.h"            /* q_chan_t / Q_CHAN_TOKEN_LEN */
#include "q_net.h"             /* q_fd_t */

/* ===================== 平台原语（线程 / 互斥 / 单调时钟 / 睡眠） =====================
 * 说明：l_1 没有统一的线程封装，而本层必须跨平台（三端共享），
 *       故在此做最小封装（static inline，无额外 .c）。
 *       Windows 走 Win32（CRITICAL_SECTION + CreateThread，不引入 winpthread）；
 *       其它平台走 pthread。 */
#ifdef Q_SYS_WINDOWS
#include <windows.h>
typedef CRITICAL_SECTION qc_mutex_t;
typedef HANDLE           qc_thread_t;
#define QC_THREAD_RET     DWORD WINAPI
#define QC_THREAD_ARG     LPVOID
#define QC_THREAD_RET_VAL 0
#else
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>
typedef pthread_mutex_t qc_mutex_t;
typedef pthread_t       qc_thread_t;
#define QC_THREAD_RET     void *
#define QC_THREAD_ARG     void *
#define QC_THREAD_RET_VAL NULL
#endif

/* 单调时钟毫秒：用于心跳/退避计时。不能用墙上时钟 —— 改系统时间会把退避算成负数 */
static inline uint64_t qc_now_ms(void)
{
#ifdef Q_SYS_WINDOWS
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
#else
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0;
#endif
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
#endif
}

static inline void qc_sleep_ms(uint32_t ms)
{
#ifdef Q_SYS_WINDOWS
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000u);
#endif
}

/* 递归锁：**必须**。UI 层会用 vms_lock() 把"先查组织树再查车辆"这类多次调用
   合并成一个原子序列，而单次业务接口内部也会自己加锁 —— 若 gate 不可重入，
   这种嵌套就是必死的自锁。Windows CRITICAL_SECTION 天然递归；
   POSIX 侧必须显式设 PTHREAD_MUTEX_RECURSIVE，否则三端行为不一致。 */
static inline void qc_mutex_init(qc_mutex_t *m)
{
#ifdef Q_SYS_WINDOWS
    InitializeCriticalSection(m);
#else
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
#endif
}

static inline void qc_mutex_fini(qc_mutex_t *m)
{
#ifdef Q_SYS_WINDOWS
    DeleteCriticalSection(m);
#else
    pthread_mutex_destroy(m);
#endif
}

static inline void qc_mutex_lock(qc_mutex_t *m)
{
#ifdef Q_SYS_WINDOWS
    EnterCriticalSection(m);
#else
    pthread_mutex_lock(m);
#endif
}

static inline void qc_mutex_unlock(qc_mutex_t *m)
{
#ifdef Q_SYS_WINDOWS
    LeaveCriticalSection(m);
#else
    pthread_mutex_unlock(m);
#endif
}

#ifdef Q_SYS_WINDOWS
typedef DWORD (WINAPI *qc_thread_fn_t)(LPVOID);
#else
typedef void *(*qc_thread_fn_t)(void *);
#endif

/* 启动线程；成功 0，失败 -1。失败时 keeper 不启用（退化为"无治理"的纯同步门面） */
static inline int qc_thread_start(qc_thread_t *t, qc_thread_fn_t fn, void *arg)
{
#ifdef Q_SYS_WINDOWS
    HANDLE h = CreateThread(NULL, 0, fn, arg, 0, NULL);
    if (h == NULL) return -1;
    *t = h;
    return 0;
#else
    return pthread_create(t, NULL, fn, arg) == 0 ? 0 : -1;
#endif
}

/* 等待线程结束并回收句柄 */
static inline void qc_thread_join(qc_thread_t *t)
{
#ifdef Q_SYS_WINDOWS
    WaitForSingleObject(*t, INFINITE);
    CloseHandle(*t);
#else
    pthread_join(*t, NULL);
#endif
}

/* ===================== 会话结构（qc_client.c 与 keeper 共用） =====================
 * 注：qc_state_t / qc_keeper_cfg_t / qc_keeper_stat_t / qc_reconnect_t / QC_FAULT_*
 *     与回调类型都定义在**公共头** qc_client.h（对外 ABI），此处不再重复定义。 */
/* 服务端静态公钥 DER 上限（SM2 公钥 DER 约 91B；留足余量） */
#define QC_PUB_DER_MAX  1024u

struct qc_session
{
    q_chan_t *chan;                     /* 安全通道句柄（内部） */
    q_fd_t    fd;                       /* 已连接 socket；(q_fd_t)-1 = 未连接 */
    uint8_t   token[Q_CHAN_TOKEN_LEN];  /* 登录后缓存的会话 Token（32B） */
    int       authed;                   /* 是否已登录 */

    /* 建通道的原始材料：断线重建时**必须**用它造一个新 q_chan_t ——
       复用已握过手的 chan 会失败（状态机只接受 NEW/HELLO -> KEYED），
       而且"一连接一会话"要求新连接配新通道（Token 绑定 channel_id）。 */
    uint8_t   pub_der[QC_PUB_DER_MAX];
    size_t    pub_len;
    uint64_t  seq0;
    uint16_t  key_id;

    /* —— M4：会话治理 —— */
    qc_mutex_t io_lock;                 /* 串行化 gate：业务线程 <-> 心跳线程 */
    qc_mutex_t st_lock;                 /* 保护下方状态字段 */
    int        io_inited;
    int        st_inited;

    int         keeper_on;              /* keeper 线程是否在运行 */
    qc_thread_t thread;
    volatile int thread_stop;           /* 停止标志 */
    volatile int wake;                  /* 唤醒标志（立即心跳/重连） */

    int         state;                  /* qc_state_t（st_lock 保护） */
    uint32_t    attempts;
    uint32_t    hb_sent, hb_failed;
    uint32_t    reconnects, relogins;
    uint64_t    last_ok_ms;
    uint64_t    next_hb_ms;
    uint64_t    next_retry_ms;
    uint32_t    fault;
    char        detail[96];

    qc_keeper_cfg_t cfg;                /* 启动时的配置副本（回调指针同址保存） */
    qc_reconnect_t rc;                  /* 重连材料（不含口令） */
};

/* ===================== 模块内部函数（不对外导出） ===================== */
/* keeper 线程主循环入口 */
QC_THREAD_RET qc_keeper_main(QC_THREAD_ARG arg);

/* 内部：设置状态并发通知（须已持有 st_lock 之外调用；内部自行加锁） */
void qc_keeper_set_state(struct qc_session *s, int state, int rc, const char *msg);

/* 内部：退避时长（毫秒）—— base * 2^(attempts-1)，上限 max，带 ±20% 抖动 */
uint32_t qc_keeper_backoff_ms(const struct qc_session *s, uint32_t attempts);

/* 内部：重新登录（同连接重发 SALT + LOGIN）。成功 0；>0 结果码；<0 断连 */
int qc_keeper_reauth(struct qc_session *s);

/* 内部：重建连接（close -> [knock] -> connect -> hello -> salt -> login）。成功 0 */
int qc_keeper_rebuild(struct qc_session *s);

#endif /* QC_KEEPER_H */
