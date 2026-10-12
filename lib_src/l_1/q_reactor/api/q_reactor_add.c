/**
 * @file q_reactor_add.c
 * @brief q_reactor_add —— 注册 fd 与回调（经当前平台事件源插件）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

int q_reactor_add(q_reactor_t *r, q_fd_t fd, uint32_t events,
                  q_reactor_handler_t h, void *ctx)
{
    if (!r)
        return -1;
#if defined(Q_SYS_LINUX)
    return q_evsrc_epoll_add((q_evsrc_epoll_t *)r->src->priv, fd, events, h, ctx);
#elif defined(Q_SYS_MACOS)
    return q_evsrc_kqueue_add((q_evsrc_kqueue_t *)r->src->priv, fd, events, h, ctx);
#elif defined(Q_SYS_WINDOWS)
    return q_evsrc_poll_add((q_evsrc_poll_t *)r->src->priv, fd, events, h, ctx);
#else
    (void)fd; (void)events; (void)h; (void)ctx;
    return -1;
#endif
}
