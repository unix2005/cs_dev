/**
 * @file q_reactor_del.c
 * @brief q_reactor_del —— 注销 fd（经 epoll 事件源插件）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

int q_reactor_del(q_reactor_t *r, int fd)
{
    if (!r)
        return -1;
    return q_evsrc_epoll_del(r->ep, fd);
}
