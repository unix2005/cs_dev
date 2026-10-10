/**
 * @file q_net_connect6.c
 * @brief q_net_connect6 —— 连接 IPv6 主机
 */
#include "headers.h"
#include "q_net.h"

int q_net_connect6(const char *host, int port, int *fd_out)
{
    if (!host || port <= 0 || !fd_out)
        return -1;
    int fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons((uint16_t)port);
    if (inet_pton(AF_INET6, host, &addr.sin6_addr) <= 0)
    {
        close(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return -1;
    }
    *fd_out = fd;
    return 0;
}
