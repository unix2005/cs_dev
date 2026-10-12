/**
 * @file q_evsrc_kqueue_create.c
 * @brief q_evsrc_kqueue_create —— 创建 kqueue 事件源插件（macOS/BSD，epoll 的等价替身）
 * @note  kqueue 已作为“可选的事件源插件”：核心 q_disp 不依赖它，本插件可独立存在。
 *        kqueue 实例本身是可 poll 的 fd（base.fd 返回 kqfd），由 q_disp_run 监听可读后调 ready 派发。
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_evsrc_int.h"

#ifdef Q_SYS_MACOS
static int kq_src_fd(q_evsrc_t *s)
{
    return ((q_evsrc_kqueue_t *)s)->kqfd;
}

/* kqfd 就绪（poll 可读）时取出事件并内联执行各 handler（语义同 epoll：在循环线程执行） */
static void kq_src_ready(q_evsrc_t *s, q_disp_t *d)
{
    (void)d;
    q_evsrc_kqueue_t *e = (q_evsrc_kqueue_t *)s;
    struct kevent evs[64];
    struct timespec z = {0, 0};
    int n = kevent(e->kqfd, NULL, 0, evs, 64, &z);
    if (n <= 0)
        return;
    for (int i = 0; i < n; i++)
    {
        struct q_reactor_ent *en = (struct q_reactor_ent *)evs[i].udata;
        if (!en || !en->h)
            continue;
        uint32_t ev = 0;
        if (evs[i].filter == EVFILT_READ)
            ev |= EPOLLIN;
        else if (evs[i].filter == EVFILT_WRITE)
            ev |= EPOLLOUT;
        if (evs[i].flags & EV_EOF)
            ev |= EPOLLHUP;
        if (evs[i].flags & EV_ERROR)
            ev |= EPOLLERR;
        en->h(en->fd, ev, en->ctx);
    }
}

static void kq_src_destroy(q_evsrc_t *s)
{
    q_evsrc_kqueue_t *e = (q_evsrc_kqueue_t *)s;
    struct kevent ch;
    for (size_t i = 0; i < e->n; i++)
    {
        if (e->ents[i])
        {
            EV_SET(&ch, e->ents[i]->fd, EVFILT_READ,  EV_DELETE, 0, 0, NULL);
            kevent(e->kqfd, &ch, 1, NULL, 0, NULL);
            EV_SET(&ch, e->ents[i]->fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
            kevent(e->kqfd, &ch, 1, NULL, 0, NULL);
            free(e->ents[i]);
        }
    }
    free(e->ents);
    if (e->kqfd >= 0)
        close(e->kqfd);
    free(e);
}
#endif /* Q_SYS_MACOS */

q_evsrc_kqueue_t *q_evsrc_kqueue_create(void)
{
#ifdef Q_SYS_MACOS
    q_evsrc_kqueue_t *e = (q_evsrc_kqueue_t *)malloc(sizeof(*e));
    if (!e)
        return NULL;
    e->kqfd = kqueue();
    if (e->kqfd < 0)
    {
        free(e);
        return NULL;
    }
    e->base.fd = kq_src_fd;
    e->base.ready = kq_src_ready;
    e->base.tick = NULL;
    e->base.destroy = kq_src_destroy;
    e->base.priv = e;
    e->ents = NULL;
    e->n = 0;
    e->cap = 0;
    return e;
#else
    return NULL;   /* 非 macOS/BSD 平台无 kqueue，插件不可用 */
#endif
}
