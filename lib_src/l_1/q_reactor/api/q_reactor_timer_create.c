/**
 * @file q_reactor_timer_create.c
 * @brief q_reactor_timer_create —— 创建通用定时器（仅 Linux timerfd）
 * @note  定时器在本模块中仅作为“产生一个可被 epoll 源监听的 fd”的辅助，
 *        本身不绑定事件循环；由调用方 q_reactor_add(tfd, EPOLLIN, ...) 纳入调度。
 */
#include "headers.h"
#include "q_reactor.h"

int q_reactor_timer_create(int *tfd_out)
{
#ifdef __linux__
    if (!tfd_out)
        return -1;
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0)
        return -1;
    *tfd_out = fd;
    return 0;
#else
    (void)tfd_out;
    return -1;
#endif
}
