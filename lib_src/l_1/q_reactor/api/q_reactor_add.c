/**
 * @file q_reactor_add.c
 * @brief q_reactor_add —— 注册 fd 与回调
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

int q_reactor_add(q_reactor_t *r, int fd, uint32_t events,
                  q_reactor_handler_t h, void *ctx)
{
    if (!r || fd < 0 || !h)
        return -1;
    struct q_reactor_ent *e = (struct q_reactor_ent *)malloc(sizeof(*e));
    if (!e)
        return -1;
    e->fd = fd;
    e->events = events;
    e->h = h;
    e->ctx = ctx;

    if (r->n == r->cap)
    {
        size_t nc = r->cap ? r->cap * 2 : 8;
        struct q_reactor_ent **ne =
            (struct q_reactor_ent **)realloc(r->ents, nc * sizeof(*ne));
        if (!ne)
        {
            free(e);
            return -1;
        }
        r->ents = ne;
        r->cap = nc;
    }
    r->ents[r->n++] = e;

    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = e;
    if (epoll_ctl(r->epfd, EPOLL_CTL_ADD, fd, &ev) != 0)
    {
        free(e);
        r->n--;
        r->ents[r->n] = NULL;   /* 撤销刚追加的槽位，保持数组稠密 */
        return -1;
    }
    return 0;
}
