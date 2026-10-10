/**
 * @file q_disp_submit_local.c
 * @brief q_disp_submit_local —— 提交“本地任务”，在事件循环线程内联执行
 * @note  用于让 reactor 调度“非 fd / 非 IO”的回调：任务入队后唤醒循环，
 *        由 q_disp_run 在循环线程内联执行（与 epoll handler 同线程）。
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_disp_int.h"

int q_disp_submit_local(q_disp_t *d, q_task_fn fn, void *arg)
{
    if (!d || !fn)
        return -1;
    struct q_local_task *t = (struct q_local_task *)malloc(sizeof(*t));
    if (!t)
        return -1;
    t->fn = fn;
    t->arg = arg;
    t->next = NULL;

    pthread_mutex_lock(&d->lmtx);
    if (d->ltail)
        d->ltail->next = t;
    else
        d->lhead = t;
    d->ltail = t;
    pthread_mutex_unlock(&d->lmtx);

    q_disp_wake(d);   /* 唤醒阻塞中的循环，使其尽快调度该本地任务 */
    return 0;
}
