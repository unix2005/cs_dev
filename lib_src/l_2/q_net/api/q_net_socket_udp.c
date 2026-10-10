/**
 * @file q_net_socket_udp.c
 * @brief q_net_socket_udp —— 创建 IPv4 UDP socket
 */
#include "headers.h"
#include "q_net.h"

int q_net_socket_udp(int *fd_out)
{
    if (!fd_out)
        return -1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    *fd_out = fd;
    return 0;
}
