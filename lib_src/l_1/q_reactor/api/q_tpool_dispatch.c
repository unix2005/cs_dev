/**
 * @file q_tpool_dispatch.c
 * @brief q_tpool_dispatch —— 提交任务到有界工作线程池
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_tpool_int.h"

int q_tpool_dispatch(q_tpool_t *p, q_task_fn fn, void *arg)
{
    if (!p || !fn)
        return -1;
    struct q_tpool_task *t = (struct q_tpool_task *)malloc(sizeof(*t));
    if (!t)
        return -1;
    t->fn = fn;
    t->arg = arg;
    t->next = NULL;

    pthread_mutex_lock(&p->mtx);
    if (p->shutdown)
    {
        free(t);
        pthread_mutex_unlock(&p->mtx);
        return -1;
    }
    if (p->max_queue > 0)
    {
        while (p->count >= p->max_queue && !p->shutdown)
            pthread_cond_wait(&p->not_full, &p->mtx);
    }
    if (p->shutdown)
    {
        free(t);
        pthread_mutex_unlock(&p->mtx);
        return -1;
    }
    if (p->tail)
        p->tail->next = t;
    else
        p->head = t;
    p->tail = t;
    p->count++;
    pthread_cond_signal(&p->not_empty);
    pthread_mutex_unlock(&p->mtx);
    return 0;
}
