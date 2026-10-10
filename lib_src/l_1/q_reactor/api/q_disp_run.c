/**
 * @file q_disp_run.c
 * @brief q_disp_run —— 执行一轮调度：poll 所有事件源 fd + 分发就绪 + 内联本地任务
 * @note  事件源（epoll 等）通过 poll 监听其 fd；无 fd 的源走 tick。核心本身不依赖 epoll，
 *        因此即使不挂载任何源、也不创建 worker，也能靠“本地任务 + 唤醒管道”调度非 fd 回调。
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_disp_int.h"

/* 在循环线程内联执行本地任务队列（持锁取走、锁外执行） */
static void q_disp_local_drain(q_disp_t *d)
{
    struct q_local_task *h = NULL;
    pthread_mutex_lock(&d->lmtx);
    h = d->lhead;
    d->lhead = d->ltail = NULL;
    pthread_mutex_unlock(&d->lmtx);

    while (h)
    {
        struct q_local_task *nx = h->next;
        h->fn(h->arg);
        free(h);
        h = nx;
    }
}

int q_disp_run(q_disp_t *d, int timeout_ms)
{
    if (!d)
        return -1;

    /* 轮询集合：唤醒管道 + 各事件源 fd（fd<0 的源走 tick） */
    size_t nfd = 1 + d->nsrc;
    struct pollfd *pf = (struct pollfd *)malloc(nfd * sizeof(*pf));
    int *idxmap = (int *)malloc(nfd * sizeof(int));   /* pf 槽 -> 源下标；-1 表示唤醒管道 */
    if (!pf || !idxmap)
    {
        free(pf);
        free(idxmap);
        return -1;
    }

    int k = 0;
    pf[k].fd = d->wake_r;
    pf[k].events = POLLIN;
    pf[k].revents = 0;
    idxmap[k] = -1;
    k++;

    for (size_t i = 0; i < d->nsrc; i++)
    {
        int f = d->srcs[i]->fd ? d->srcs[i]->fd(d->srcs[i]) : -1;
        if (f >= 0)
        {
            pf[k].fd = f;
            pf[k].events = POLLIN;
            pf[k].revents = 0;
            idxmap[k] = (int)i;
            k++;
        }
    }

    int rc = poll(pf, k, timeout_ms);
    if (rc < 0)
    {
        int e = errno;
        free(pf);
        free(idxmap);
        if (e == EINTR)
            return 0;
        return -1;
    }

    /* 唤醒管道可读：排空，避免下次 poll 立即重复触发 */
    for (int i = 0; i < k; i++)
    {
        if (idxmap[i] == -1 && (pf[i].revents & POLLIN))
        {
            char buf[64];
            while (read(d->wake_r, buf, sizeof(buf)) > 0)
                ;
            break;
        }
    }

    /* 分发事件源就绪（handler 在循环线程内联执行） */
    for (int i = 0; i < k; i++)
    {
        int si = idxmap[i];
        if (si < 0)
            continue;
        if (pf[i].revents & (POLLIN | POLLERR | POLLHUP))
        {
            if (d->srcs[si]->ready)
                d->srcs[si]->ready(d->srcs[si], d);
        }
        else if (d->srcs[si]->tick &&
                 d->srcs[si]->fd && d->srcs[si]->fd(d->srcs[si]) < 0)
        {
            d->srcs[si]->tick(d->srcs[si], d, timeout_ms);
        }
    }

    free(pf);
    free(idxmap);

    /* 内联执行本地任务（非 fd 回调被 reactor 调度） */
    q_disp_local_drain(d);
    return 0;
}
