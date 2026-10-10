#ifndef Q_DISP_INT_H
#define Q_DISP_INT_H
/* 通用任务分发核心 内部结构体（不安装到 include/，仅供本模块 q_disp_*.c 使用）。
   本核心与 fd / 网络连接 / IO 完全无关：它只管理「工作线程池 + 任务队列 + 事件源插件 + 唤醒管道」。 */
#include "q_reactor.h"
#include "q_tpool_int.h"

/* 在事件循环线程内联执行的“本地任务”（用于让 reactor 调度非 fd 回调） */
struct q_local_task
{
    void (*fn)(void *arg);
    void *arg;
    struct q_local_task *next;
};

struct q_disp
{
    q_tpool_t   *pool;       /* 工作线程池；nworkers==0 时为 NULL（无并行执行器） */
    int          nworkers;
    q_evsrc_t  **srcs;       /* 已挂载的事件源插件数组（指针稳定） */
    size_t       nsrc, capsrc;
    int          wake_r;     /* 唤醒管道读端（poll 监听） */
    int          wake_w;     /* 唤醒管道写端（跨线程唤醒循环） */
    volatile int stop;       /* 内部停止标志 */
    struct q_local_task *lhead, *ltail;  /* 本地任务队列（循环线程内联执行） */
    pthread_mutex_t lmtx;
};

#endif /* Q_DISP_INT_H */
