/**
 * @file q_net_accept.c
 * @brief q_net_accept —— 接受连接（v4/v6 通用，不填对端地址）
 */
#include "headers.h"
#include "q_net.h"

int q_net_accept(int listen_fd, int *fd_out)
{
    if (listen_fd < 0 || !fd_out)
        return -1;
    int c = accept(listen_fd, NULL, NULL);
    if (c < 0)
        return -1;
    *fd_out = c;
    return 0;
}
