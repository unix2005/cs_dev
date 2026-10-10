/**
 * @file q_tpool_create.c
 * @brief q_tpool_create —— 创建有界工作线程池
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_tpool_int.h"

static void *tpool_worker(void *arg)
{
    q_tpool_t *p = (q_tpool_t *)arg;
    for (;;)
    {
        pthread_mutex_lock(&p->mtx);
        while (p->head == NULL && !p->shutdown)
            pthread_cond_wait(&p->not_empty, &p->mtx);
        if (p->head == NULL && p->shutdown)
        {
            pthread_mutex_unlock(&p->mtx);
            break;
        }
        struct q_tpool_task *t = p->head;
        p->head = t->next;
        if (p->head == NULL)
            p->tail = NULL;
        p->count--;
        if (p->max_queue > 0)
            pthread_cond_signal(&p->not_full);
        pthread_mutex_unlock(&p->mtx);

        t->fn(t->arg);                 /* 在锁外执行任务 */
        free(t);
    }
    return NULL;
}

q_tpool_t *q_tpool_create(int nthreads, int max_queue)
{
    if (nthreads < 1)
        nthreads = 1;
    q_tpool_t *p = (q_tpool_t *)malloc(sizeof(*p));
    if (!p)
        return NULL;
    p->nthreads = nthreads;
    p->max_queue = max_queue;
    p->shutdown = 0;
    p->head = p->tail = NULL;
    p->count = 0;
    p->threads = (pthread_t *)malloc(sizeof(pthread_t) * (size_t)nthreads);
    if (!p->threads)
    {
        free(p);
        return NULL;
    }
    pthread_mutex_init(&p->mtx, NULL);
    pthread_cond_init(&p->not_empty, NULL);
    pthread_cond_init(&p->not_full, NULL);

    int created = 0;
    for (; created < nthreads; created++)
    {
        if (pthread_create(&p->threads[created], NULL, tpool_worker, p) != 0)
            break;
    }
    if (created < nthreads)
    {
        /* 部分创建失败：标记关闭、唤醒已建线程使其退出并回收 */
        p->shutdown = 1;
        pthread_cond_broadcast(&p->not_empty);
        for (int j = 0; j < created; j++)
            pthread_join(p->threads[j], NULL);
        pthread_mutex_destroy(&p->mtx);
        pthread_cond_destroy(&p->not_empty);
        pthread_cond_destroy(&p->not_full);
        free(p->threads);
        free(p);
        return NULL;
    }
    return p;
}
