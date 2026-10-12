/**
 * @file q_net_socket_tcp6.c
 * @brief q_net_socket_tcp6 —— 创建 IPv6 TCP socket（显式 v6-only）
 */
#include "headers.h"
#include "q_net.h"

int q_net_socket_tcp6(q_fd_t *fd_out)
{
    if (!fd_out)
        return -1;
    q_fd_t fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd == (q_fd_t)-1)
        return -1;
    int one = 1;
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
    *fd_out = fd;
    return 0;
}
