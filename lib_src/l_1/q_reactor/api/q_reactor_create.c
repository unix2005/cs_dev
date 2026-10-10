/**
 * @file q_reactor_create.c
 * @brief q_reactor_create —— 创建通用反应器（内含 epoll）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

q_reactor_t *q_reactor_create(void)
{
    q_reactor_t *r = (q_reactor_t *)malloc(sizeof(*r));
    if (!r)
        return NULL;
    r->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (r->epfd < 0)
    {
        free(r);
        return NULL;
    }
    r->ents = NULL;
    r->n = 0;
    r->cap = 0;
    return r;
}
