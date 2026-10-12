/**
 * @file q_reactor_del.c
 * @brief q_reactor_del —— 注销 fd（经当前平台事件源插件）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

int q_reactor_del(q_reactor_t *r, q_fd_t fd)
{
    if (!r)
        return -1;
#if defined(Q_SYS_LINUX)
    return q_evsrc_epoll_del((q_evsrc_epoll_t *)r->src->priv, fd);
#elif defined(Q_SYS_MACOS)
    return q_evsrc_kqueue_del((q_evsrc_kqueue_t *)r->src->priv, fd);
#elif defined(Q_SYS_WINDOWS)
    return q_evsrc_poll_del((q_evsrc_poll_t *)r->src->priv, fd);
#else
    (void)fd;
    return -1;
#endif
}
