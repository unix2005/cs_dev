#ifndef Q_TIMER_INT_H
#define Q_TIMER_INT_H
/* 定时器内部状态（不安装到 include/，仅供本模块 timer_*.c 使用）。
   跨平台实现：
     Linux   : timerfd（无线程，由内核计时）
     macOS/  : 自管道 + 周期线程（线程向写端写 1 字节，读端可读即触发）
     Windows : 回环 UDP socket + 周期线程（线程向自身 send 1 字节） */
#include "q_reactor.h"
#include <pthread.h>

struct q_timer_state
{
    q_fd_t       tfd;          /* 返回给调用方的可读 fd（timerfd / 管道读端 / UDP socket） */
    q_fd_t       wfd;          /* 触发写端：管道写端；Windows 下与 tfd 同一 socket；timerfd 下为 -1 */
    pthread_t   thr;          /* 周期线程（仅 macOS/Windows 使用） */
    volatile int stop;        /* 线程退出标志 */
    int         running;      /* 线程是否已启动 */
    uint64_t    first_ms;     /* 首次触发延迟 */
    uint64_t    interval_ms;  /* 周期（0 表示单次） */
    int         is_threaded;  /* 1=自管道/UDP 线程模型；0=timerfd */
    struct q_timer_state *next;
};

/* 全局注册表（按 tfd 查找，供 destroy 取回状态） */
struct q_timer_state *q_timer_find(q_fd_t tfd);
void q_timer_register(struct q_timer_state *s);
void q_timer_unregister(q_fd_t tfd);

/* 周期线程主函数（macOS/Windows 共用） */
void *q_timer_thread(void *arg);

#endif /* Q_TIMER_INT_H */
