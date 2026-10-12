/**
 * @file q_evsrc_poll_create.c
 * @brief q_evsrc_poll_create —— 创建 WSAPoll 事件源插件（Windows，epoll 的等价替身）
 * @note  Windows 没有“可 poll 的多路复用 fd”，故 base.fd 返回 -1，由 q_disp_run 经 tick 路径驱动：
 *        tick() 内 WSAPoll 一组已注册 socket，就绪后内联执行 handler（语义同 epoll，在循环线程执行）。
 *        events 为便携 EPOLLIN/EPOLLOUT 等；内部转换为 WSAPoll 的 POLLIN/POLLOUT。
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_evsrc_int.h"

#ifdef Q_SYS_WINDOWS
static int poll_src_fd(q_evsrc_t *s)
{
    (void)s;
    return -1;   /* 无独立 fd，走 tick 路径 */
}

/* 在循环线程内联执行各就绪 socket 的 handler（events 归一化为便携 EPOLLIN/EPOLLOUT 等） */
static void poll_src_tick(q_evsrc_t *s, q_disp_t *d, int timeout_ms)
{
    (void)d;
    q_evsrc_poll_t *e = (q_evsrc_poll_t *)s;
    if (e->n == 0)
        return;

    WSAPOLLFD *fds = (WSAPOLLFD *)malloc(e->n * sizeof(*fds));
    if (!fds)
        return;
    for (size_t i = 0; i < e->n; i++)
    {
        fds[i].fd = (SOCKET)e->ents[i]->fd;   /* 注意：Windows 下 fd 实为 SOCKET 值 */
        fds[i].events = 0;
        if (e->ents[i]->events & EPOLLIN)
            fds[i].events |= POLLIN;
        if (e->ents[i]->events & EPOLLOUT)
            fds[i].events |= POLLOUT;
        fds[i].revents = 0;
    }

    int n = WSAPoll(fds, (ULONG)e->n, timeout_ms);
    if (n > 0)
    {
        for (size_t i = 0; i < e->n; i++)
        {
            if (fds[i].revents == 0)
                continue;
            struct q_reactor_ent *en = e->ents[i];
            uint32_t ev = 0;
            if (fds[i].revents & POLLIN)
                ev |= EPOLLIN;
            if (fds[i].revents & POLLOUT)
                ev |= EPOLLOUT;
            if (fds[i].revents & POLLERR)
                ev |= EPOLLERR;
            if (fds[i].revents & POLLHUP)
                ev |= EPOLLHUP;
            if (en && en->h)
                en->h(en->fd, ev, en->ctx);
        }
    }
    free(fds);
}

static void poll_src_destroy(q_evsrc_t *s)
{
    q_evsrc_poll_t *e = (q_evsrc_poll_t *)s;
    for (size_t i = 0; i < e->n; i++)
        free(e->ents[i]);
    free(e->ents);
    free(e);
}
#endif /* Q_SYS_WINDOWS */

q_evsrc_poll_t *q_evsrc_poll_create(void)
{
#ifdef Q_SYS_WINDOWS
    q_evsrc_poll_t *e = (q_evsrc_poll_t *)malloc(sizeof(*e));
    if (!e)
        return NULL;
    e->base.fd = poll_src_fd;
    e->base.ready = NULL;
    e->base.tick = poll_src_tick;
    e->base.destroy = poll_src_destroy;
    e->base.priv = e;
    e->ents = NULL;
    e->n = 0;
    e->cap = 0;
    return e;
#else
    return NULL;   /* 非 Windows 平台无 WSAPoll，插件不可用 */
#endif
}
