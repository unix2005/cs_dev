/**
 * @file q_reactor_timer_arm.c
 * @brief q_reactor_timer_arm —— 设置首次/周期触发时间（仅 Linux timerfd）
 */
#include "headers.h"
#include "q_reactor.h"

int q_reactor_timer_arm(int tfd, uint64_t first_ms, uint64_t interval_ms)
{
#ifdef __linux__
    if (tfd < 0)
        return -1;
    struct itimerspec its;
    its.it_value.tv_sec = (time_t)(first_ms / 1000);
    its.it_value.tv_nsec = (long)(first_ms % 1000) * 1000000L;
    its.it_interval.tv_sec = (time_t)(interval_ms / 1000);
    its.it_interval.tv_nsec = (long)(interval_ms % 1000) * 1000000L;
    if (timerfd_settime(tfd, 0, &its, NULL) != 0)
        return -1;
    return 0;
#else
    (void)tfd; (void)first_ms; (void)interval_ms;
    return -1;
#endif
}
