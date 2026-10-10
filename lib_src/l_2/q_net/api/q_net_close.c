/**
 * @file q_net_close.c
 * @brief q_net_close —— 关闭 fd
 */
#include "headers.h"
#include "q_net.h"

int q_net_close(int fd)
{
    if (fd < 0)
        return -1;
    return close(fd);
}
