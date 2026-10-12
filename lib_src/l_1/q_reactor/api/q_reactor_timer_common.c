/**
 * @file q_reactor_timer_common.c
 * @brief 定时器内部注册表 + 周期线程（供 timer_create/arm/destroy 共用）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_timer_int.h"

#include <pthread.h>

static pthread_mutex_t g_tmr_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct q_timer_state *g_tmr_head = NULL;

struct q_timer_state *q_timer_find(q_fd_t tfd)
{
    pthread_mutex_lock(&g_tmr_mtx);
    for (struct q_timer_state *s = g_tmr_head; s; s = s->next)
    {
        if (s->tfd == tfd)
        {
            pthread_mutex_unlock(&g_tmr_mtx);
            return s;
        }
    }
    pthread_mutex_unlock(&g_tmr_mtx);
    return NULL;
}

void q_timer_register(struct q_timer_state *s)
{
    pthread_mutex_lock(&g_tmr_mtx);
    s->next = g_tmr_head;
    g_tmr_head = s;
    pthread_mutex_unlock(&g_tmr_mtx);
}

void q_timer_unregister(q_fd_t tfd)
{
    pthread_mutex_lock(&g_tmr_mtx);
    struct q_timer_state **pp = &g_tmr_head;
    while (*pp)
    {
        if ((*pp)->tfd == tfd)
        {
            *pp = (*pp)->next;
            break;
        }
        pp = &(*pp)->next;
    }
    pthread_mutex_unlock(&g_tmr_mtx);
}

/* 周期线程：首延迟后触发一次，之后按 interval 重复（interval==0 表示单次） */
void *q_timer_thread(void *arg)
{
    struct q_timer_state *s = (struct q_timer_state *)arg;

#if defined(Q_SYS_WINDOWS)
    #define TM_SLEEP(ms) Sleep((DWORD)(ms))
#else
    #define TM_SLEEP(ms) do { \
        struct timespec _ts; \
        _ts.tv_sec = (time_t)((ms) / 1000); \
        _ts.tv_nsec = (long)((ms) % 1000) * 1000000L; \
        nanosleep(&_ts, NULL); \
    } while (0)
#endif

    TM_SLEEP(s->first_ms);
    while (!s->stop)
    {
        if (s->wfd >= 0)
        {
#ifdef Q_SYS_WINDOWS
            send((SOCKET)s->wfd, "t", 1, 0);   /* UDP 回环：向自身发送 1 字节 */
#else
            /* 非阻塞写，缓冲区满（消费方未及时读）时忽略，fd 仍可读故 handler 不受影响 */
            (void)!write(s->wfd, "t", 1);
#endif
        }
        if (s->interval_ms == 0)
            break;
        TM_SLEEP(s->interval_ms);
    }
    return NULL;
#undef TM_SLEEP
}
