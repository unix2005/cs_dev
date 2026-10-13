/* 守护进程信号处理：安装 SIGTERM/SIGINT，置停止标志（供主循环轮询） */
#include "headers.h"
#include "q_common.h"

static volatile sig_atomic_t g_stop = 0;

static void q_common_on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

int daemon_install_signal_handlers(void)
{
    struct sigaction sa;
    sa.sa_handler = q_common_on_signal;
    if (sigemptyset(&sa.sa_mask) != 0)
    {
        return -1;
    }
    sa.sa_flags = 0;
    if (sigaction(SIGTERM, &sa, NULL) != 0)
    {
        return -1;
    }
    if (sigaction(SIGINT, &sa, NULL) != 0)
    {
        return -1;
    }
    return 0;
}

int daemon_should_stop(void)
{
    return g_stop;
}
