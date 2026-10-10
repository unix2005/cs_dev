/**
 * @file q_reactor_add.c
 * @brief q_reactor_add —— 注册 fd 与回调（经 epoll 事件源插件）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

int q_reactor_add(q_reactor_t *r, int fd, uint32_t events,
                  q_reactor_handler_t h, void *ctx)
{
    if (!r)
        return -1;
    return q_evsrc_epoll_add(r->ep, fd, events, h, ctx);
}
