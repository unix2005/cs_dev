/**
 * @file q_evsrc_poll_add.c
 * @brief q_evsrc_poll_add —— 在 WSAPoll 插件中登记 socket 与回调（Windows）
 * @note  events 为便携 EPOLLIN/EPOLLOUT 等；tick() 时转换为 WSAPoll 的 POLLIN/POLLOUT。
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_evsrc_int.h"

int q_evsrc_poll_add(q_evsrc_poll_t *e, q_fd_t fd, uint32_t events,
                     q_reactor_handler_t h, void *ctx)
{
#ifdef Q_SYS_WINDOWS
    if (!e || fd == (q_fd_t)INVALID_SOCKET || !h)
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
    return 0;   /* 注册仅登记；真正的轮询在 tick() 的 WSAPoll 中进行 */
#else
    (void)e; (void)fd; (void)events; (void)h; (void)ctx;
    return -1;
#endif
}
