/**
 * @file q_disp_destroy.c
 * @brief q_disp_destroy —— 销毁通用任务分发核心（先销毁事件源，再回收 worker 池与本地任务）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_disp_int.h"

void q_disp_destroy(q_disp_t *d)
{
    if (!d)
        return;
    d->stop = 1;

    /* 事件源由分发核心负责销毁（插件各自 free 内部结构） */
    for (size_t i = 0; i < d->nsrc; i++)
    {
        if (d->srcs[i] && d->srcs[i]->destroy)
            d->srcs[i]->destroy(d->srcs[i]);
    }
    free(d->srcs);

    /* 丢弃尚未执行的本地任务（不执行，仅释放） */
    struct q_local_task *t = d->lhead;
    while (t)
    {
        struct q_local_task *nx = t->next;
        free(t);
        t = nx;
    }

    if (d->pool)
        q_tpool_destroy(d->pool);

    close(d->wake_r);
    close(d->wake_w);
    pthread_mutex_destroy(&d->lmtx);
    free(d);
}
