/**
 * @file q_net_socket_tcp.c
 * @brief q_net_socket_tcp —— 创建 TCP socket
 */
#include "headers.h"
#include "q_net.h"

int q_net_socket_tcp(int *fd_out)
{
    if (!fd_out)
        return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    *fd_out = fd;
    return 0;
}
