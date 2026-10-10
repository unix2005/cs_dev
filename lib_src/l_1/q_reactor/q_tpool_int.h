#ifndef Q_TPOOL_INT_H
#define Q_TPOOL_INT_H
/* 有界线程池内部结构体（不安装到 include/，仅供本模块 tpool_*.c 使用） */
#include "q_reactor.h"
#include "include_thd.h"          /* 跨平台聚合：pthread 等线程原语 */

struct q_tpool_task
{
    void (*fn)(void *arg);
    void *arg;
    struct q_tpool_task *next;
};

struct q_tpool
{
    pthread_t *threads;
    int nthreads;
    int max_queue;             /* <=0 表示无界 */
    int shutdown;
    struct q_tpool_task *head;
    struct q_tpool_task *tail;
    int count;
    pthread_mutex_t mtx;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
};

#endif /* Q_TPOOL_INT_H */
