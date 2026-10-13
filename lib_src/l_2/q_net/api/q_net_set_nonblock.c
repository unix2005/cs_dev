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
    /* 复用 include_net.h 的跨平台封装：POSIX 用 fcntl(O_NONBLOCK)，
       Windows 用 ioctlsocket(FIONBIO)，SOCKET 经 q_fd_t 原样传入不截断 */
    return Q_FD_SET_NONBLOCK(fd, 1);
}
