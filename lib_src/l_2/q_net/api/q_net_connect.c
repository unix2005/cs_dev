/**
 * @file q_net_connect.c
 * @brief q_net_connect —— 连接 IPv4 主机
 */
#include "headers.h"
#include "q_net.h"

int q_net_connect(const char *host, int port, q_fd_t *fd_out)
{
    if (!host || port <= 0 || !fd_out)
        return -1;
    q_fd_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == (q_fd_t)-1)
        return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0)
    {
        Q_SOCK_CLOSE(fd);
        return -1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        Q_SOCK_CLOSE(fd);
        return -1;
    }
    *fd_out = fd;
    return 0;
}
