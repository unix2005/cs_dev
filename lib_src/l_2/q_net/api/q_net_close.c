/**
 * @file q_net_close.c
 * @brief q_net_close —— 关闭 fd
 */
#include "headers.h"
#include "q_net.h"

int q_net_close(q_fd_t fd)
{
    if (fd == (q_fd_t)-1)
        return -1;
    return Q_SOCK_CLOSE(fd);
}
