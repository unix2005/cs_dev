#ifndef Q_EVSRC_INT_H
#define Q_EVSRC_INT_H
/* 事件源插件 内部结构体（不安装到 include/，仅供本模块 epoll 插件等使用）。
   事件源把“某种事件”抽象为：一个可 poll 的 fd + 就绪回调。epoll 只是其中一种实现。 */
#include "q_reactor.h"
#include "q_disp_int.h"

/* fd -> 回调 映射项（epoll 插件内部使用，指针地址稳定） */
struct q_reactor_ent
{
    q_fd_t fd;   /* 注册句柄：POSIX 为 int；Windows 为 64 位 SOCKET（用 q_fd_t 避免截断） */
    uint32_t events;
    q_reactor_handler_t h;
    void *ctx;
};

/* epoll 事件源插件：内部持有一个 epoll 实例 + 已注册 fd 映射。
   base.fd 返回 epfd 供分发核心 poll；base.ready 在 epfd 就绪时取出事件并内联执行 handler。 */
struct q_evsrc_epoll
{
    q_evsrc_t base;
    int epfd;
    struct q_reactor_ent **ents;  /* 指针数组，指针稳定（单独 malloc） */
    size_t n, cap;
};

/* kqueue 事件源插件（macOS/BSD）：内部持有一个 kqueue 实例 + 已注册 fd 映射。
   base.fd 返回 kqfd（可被 poll 监听可读）；base.ready 在 kq 就绪时 kevent 取事件并内联执行 handler。 */
struct q_evsrc_kqueue
{
    q_evsrc_t base;
    int kqfd;
    struct q_reactor_ent **ents;  /* 指针数组，指针稳定（单独 malloc） */
    size_t n, cap;
};

/* WSAPoll 事件源插件（Windows）：无独立 fd（base.fd 返回 -1），在 tick() 中 WSAPoll 一组 socket。
   内部持有已注册 socket 映射。 */
struct q_evsrc_poll
{
    q_evsrc_t base;
    struct q_reactor_ent **ents;
    size_t n, cap;
};

#endif /* Q_EVSRC_INT_H */
