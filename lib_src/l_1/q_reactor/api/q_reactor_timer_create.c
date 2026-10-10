/**
 * @file q_reactor_timer_create.c
 * @brief q_reactor_timer_create —— 创建通用定时器（timerfd）
 */
#include "headers.h"
#include "q_reactor.h"

int q_reactor_timer_create(int *tfd_out)
{
    if (!tfd_out)
        return -1;
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0)
        return -1;
    *tfd_out = fd;
    return 0;
}
