/**
 * @file q_reactor_timer_arm.c
 * @brief q_reactor_timer_arm —— 设置首次/周期触发时间（跨平台）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_timer_int.h"

int q_reactor_timer_arm(q_fd_t tfd, uint64_t first_ms, uint64_t interval_ms)
{
    struct q_timer_state *s = q_timer_find(tfd);
    if (!s)
        return -1;

#if defined(Q_SYS_LINUX)
    if (s->is_threaded)
        return -1;   /* 不应发生：Linux 走 timerfd */
    struct itimerspec its;
    its.it_value.tv_sec = (time_t)(first_ms / 1000);
    its.it_value.tv_nsec = (long)(first_ms % 1000) * 1000000L;
    its.it_interval.tv_sec = (time_t)(interval_ms / 1000);
    its.it_interval.tv_nsec = (long)(interval_ms % 1000) * 1000000L;
    if (timerfd_settime(s->tfd, 0, &its, NULL) != 0)
        return -1;
    return 0;

#else
    /* macOS/BSD/Windows：线程模型。记录参数；若已运行则先停旧线程再重启。 */
    s->first_ms = first_ms;
    s->interval_ms = interval_ms;

    if (s->running)
    {
        s->stop = 1;
        pthread_join(s->thr, NULL);
        s->running = 0;
        s->stop = 0;
    }
    if (pthread_create(&s->thr, NULL, q_timer_thread, s) != 0)
        return -1;
    s->running = 1;
    return 0;
#endif
}
