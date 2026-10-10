/**
 * @file q_tpool_destroy.c
 * @brief q_tpool_destroy —— 优雅关闭并销毁有界工作线程池
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_tpool_int.h"

void q_tpool_destroy(q_tpool_t *p)
{
    if (!p)
        return;
    pthread_mutex_lock(&p->mtx);
    p->shutdown = 1;
    pthread_cond_broadcast(&p->not_empty);
    pthread_mutex_unlock(&p->mtx);

    for (int i = 0; i < p->nthreads; i++)
        pthread_join(p->threads[i], NULL);

    struct q_tpool_task *t = p->head;
    while (t)
    {
        struct q_tpool_task *nx = t->next;
        free(t);
        t = nx;
    }
    pthread_mutex_destroy(&p->mtx);
    pthread_cond_destroy(&p->not_empty);
    pthread_cond_destroy(&p->not_full);
    free(p->threads);
    free(p);
}
