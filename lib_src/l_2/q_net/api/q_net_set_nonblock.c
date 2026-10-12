/**
 * @file q_net_set_nonblock.c
 * @brief q_net_set_nonblock —— 设置非阻塞
 */
#include "headers.h"
#include "q_net.h"

int q_net_set_nonblock(q_fd_t fd)
{
    if (fd == (q_fd_t)-1)
        return -1;
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0)
        return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}
