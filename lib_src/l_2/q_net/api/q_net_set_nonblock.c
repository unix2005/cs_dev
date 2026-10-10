/**
 * @file q_net_set_nonblock.c
 * @brief q_net_set_nonblock —— 设置非阻塞
 */
#include "headers.h"
#include "q_net.h"

int q_net_set_nonblock(int fd)
{
    if (fd < 0)
        return -1;
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0)
        return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}
