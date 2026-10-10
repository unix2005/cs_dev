/**
 * @file q_disp_submit.c
 * @brief q_disp_submit —— 提交任务到 worker 线程池并行执行（可跨线程调用）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_disp_int.h"

int q_disp_submit(q_disp_t *d, q_task_fn fn, void *arg)
{
    if (!d || !fn)
        return -1;
    if (!d->pool)          /* 无 worker（nworkers==0）时本核心无并行执行器 */
        return -1;
    return q_tpool_dispatch(d->pool, fn, arg);
}
