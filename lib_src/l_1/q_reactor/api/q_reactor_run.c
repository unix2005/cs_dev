/**
 * @file q_reactor_run.c
 * @brief q_reactor_run —— 执行一轮事件循环（单次 epoll_wait + 分发）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

int q_reactor_run(q_reactor_t *r, int timeout_ms)
{
    if (!r)
        return -1;
    struct epoll_event evs[64];
    int n = epoll_wait(r->epfd, evs, 64, timeout_ms);
    if (n < 0)
    {
        if (errno == EINTR)
            return 0;
        return -1;
    }
    for (int i = 0; i < n; i++)
    {
        struct q_reactor_ent *e = (struct q_reactor_ent *)evs[i].data.ptr;
        if (e && e->h)
            e->h(e->fd, evs[i].events, e->ctx);
    }
    return 0;
}
