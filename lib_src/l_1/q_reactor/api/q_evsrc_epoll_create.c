/**
 * @file q_evsrc_epoll_create.c
 * @brief q_evsrc_epoll_create —— 创建 epoll 事件源插件（仅 Linux）
 * @note  epoll 已降级为“可选的事件源插件”：核心 q_disp 不依赖它，本插件可独立存在。
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_evsrc_int.h"

#ifdef __linux__
static int ep_src_fd(q_evsrc_t *s)
{
    return ((q_evsrc_epoll_t *)s)->epfd;
}

/* epfd 就绪时取出事件并内联执行各 handler（与改造前语义一致：在循环线程执行） */
static void ep_src_ready(q_evsrc_t *s, q_disp_t *d)
{
    (void)d;
    q_evsrc_epoll_t *e = (q_evsrc_epoll_t *)s;
    struct epoll_event evs[64];
    int n = epoll_wait(e->epfd, evs, 64, 0);
    if (n <= 0)
        return;
    for (int i = 0; i < n; i++)
    {
        struct q_reactor_ent *en = (struct q_reactor_ent *)evs[i].data.ptr;
        if (en && en->h)
            en->h(en->fd, evs[i].events, en->ctx);
    }
}

static void ep_src_destroy(q_evsrc_t *s)
{
    q_evsrc_epoll_t *e = (q_evsrc_epoll_t *)s;
    for (size_t i = 0; i < e->n; i++)
    {
        if (e->ents[i])
        {
            epoll_ctl(e->epfd, EPOLL_CTL_DEL, e->ents[i]->fd, NULL);
            free(e->ents[i]);
        }
    }
    free(e->ents);
    if (e->epfd >= 0)
        close(e->epfd);
    free(e);
}
#endif /* __linux__ */

q_evsrc_epoll_t *q_evsrc_epoll_create(void)
{
#ifdef __linux__
    q_evsrc_epoll_t *e = (q_evsrc_epoll_t *)malloc(sizeof(*e));
    if (!e)
        return NULL;
    e->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (e->epfd < 0)
    {
        free(e);
        return NULL;
    }
    e->base.fd = ep_src_fd;
    e->base.ready = ep_src_ready;
    e->base.tick = NULL;
    e->base.destroy = ep_src_destroy;
    e->base.priv = e;
    e->ents = NULL;
    e->n = 0;
    e->cap = 0;
    return e;
#else
    return NULL;   /* 非 Linux 平台无 epoll，插件不可用 */
#endif
}
