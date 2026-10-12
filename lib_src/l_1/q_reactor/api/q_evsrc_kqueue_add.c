/**
 * @file q_evsrc_kqueue_add.c
 * @brief q_evsrc_kqueue_add —— 在 kqueue 插件中注册 fd 与回调（macOS/BSD）
 * @note  events 为便携 EPOLLIN/EPOLLOUT 等；内部转换为 kqueue 的 EVFILT_READ / EVFILT_WRITE。
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_evsrc_int.h"

int q_evsrc_kqueue_add(q_evsrc_kqueue_t *e, q_fd_t fd, uint32_t events,
                       q_reactor_handler_t h, void *ctx)
{
#ifdef Q_SYS_MACOS
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

    struct kevent ch[2];
    int cnt = 0;
    if (events & EPOLLIN)
        EV_SET(&ch[cnt++], (uintptr_t)fd, EVFILT_READ,  EV_ADD, 0, 0, en);
    if (events & EPOLLOUT)
        EV_SET(&ch[cnt++], (uintptr_t)fd, EVFILT_WRITE, EV_ADD, 0, 0, en);
    if (cnt && kevent(e->kqfd, ch, cnt, NULL, 0, NULL) != 0)
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
