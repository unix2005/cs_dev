/**
 * @file q_evsrc_epoll_add.c
 * @brief q_evsrc_epoll_add —— 在 epoll 插件中注册 fd 与回调（仅 Linux）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_evsrc_int.h"

int q_evsrc_epoll_add(q_evsrc_epoll_t *e, int fd, uint32_t events,
                      q_reactor_handler_t h, void *ctx)
{
#ifdef __linux__
    if (!e || fd < 0 || !h)
        return -1;
    struct q_reactor_ent *en = (struct q_reactor_ent *)malloc(sizeof(*en));
    if (!en)
        return -1;
    en->fd = fd;
    en->events = events;
    en->h = h;
    en->ctx = ctx;

    if (e->n == e->cap)
    {
        size_t nc = e->cap ? e->cap * 2 : 8;
        struct q_reactor_ent **ne =
            (struct q_reactor_ent **)realloc(e->ents, nc * sizeof(*ne));
        if (!ne)
        {
            free(en);
            return -1;
        }
        e->ents = ne;
        e->cap = nc;
    }
    e->ents[e->n++] = en;

    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = en;
    if (epoll_ctl(e->epfd, EPOLL_CTL_ADD, fd, &ev) != 0)
    {
        free(en);
        e->n--;
        e->ents[e->n] = NULL;   /* 撤销刚追加的槽位，保持数组稠密 */
        return -1;
    }
    return 0;
#else
    (void)e; (void)fd; (void)events; (void)h; (void)ctx;
    return -1;
#endif
}
