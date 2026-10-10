/**
 * @file q_disp_create.c
 * @brief q_disp_create —— 创建通用任务分发核心（worker 池 + 唤醒管道 + 事件源容器）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_disp_int.h"

q_disp_t *q_disp_create(int nworkers)
{
    q_disp_t *d = (q_disp_t *)malloc(sizeof(*d));
    if (!d)
        return NULL;

    d->nworkers = nworkers > 0 ? nworkers : 0;
    d->pool = d->nworkers > 0 ? q_tpool_create(d->nworkers, 0) : NULL;
    if (d->nworkers > 0 && !d->pool)
    {
        free(d);
        return NULL;
    }

    d->srcs = NULL;
    d->nsrc = 0;
    d->capsrc = 0;
    d->stop = 0;
    d->lhead = d->ltail = NULL;
    pthread_mutex_init(&d->lmtx, NULL);

    /* 唤醒管道：用于跨线程唤醒阻塞中的 poll（停止 / 提交本地任务） */
    int fds[2];
#ifdef __linux__
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0)
    {
        q_tpool_destroy(d->pool);
        pthread_mutex_destroy(&d->lmtx);
        free(d);
        return NULL;
    }
#else
    if (pipe(fds) != 0)
    {
        q_tpool_destroy(d->pool);
        pthread_mutex_destroy(&d->lmtx);
        free(d);
        return NULL;
    }
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);
#endif
    d->wake_r = fds[0];
    d->wake_w = fds[1];
    return d;
}
