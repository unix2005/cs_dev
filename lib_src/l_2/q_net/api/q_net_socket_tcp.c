/**
 * @file q_net_socket_tcp.c
 * @brief q_net_socket_tcp —— 创建 TCP socket
 */
#include "headers.h"
#include "q_net.h"

int q_net_socket_tcp(q_fd_t *fd_out)
{
    if (!fd_out)
        return -1;
    q_fd_t fd = Q_SOCKET(AF_INET, SOCK_STREAM, 0);
    if (fd == (q_fd_t)-1)
        return -1;
    *fd_out = fd;
    return 0;
}
