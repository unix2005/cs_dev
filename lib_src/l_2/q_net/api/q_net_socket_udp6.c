/**
 * @file q_net_socket_udp6.c
 * @brief q_net_socket_udp6 —— 创建 IPv6 UDP socket（显式 v6-only）
 */
#include "headers.h"
#include "q_net.h"

int q_net_socket_udp6(int *fd_out)
{
    if (!fd_out)
        return -1;
    int fd = socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    int one = 1;
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof(one));
    *fd_out = fd;
    return 0;
}
