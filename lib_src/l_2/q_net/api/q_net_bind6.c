/**
 * @file q_net_bind6.c
 * @brief q_net_bind6 —— IPv6 UDP 绑定 IN6ADDR_ANY（不 listen）
 */
#include "headers.h"
#include "q_net.h"

int q_net_bind6(int port, q_fd_t *fd_out)
{
    if (port <= 0 || !fd_out)
        return -1;
    q_fd_t fd = Q_SOCKET(AF_INET6, SOCK_DGRAM, 0);
    if (fd == (q_fd_t)-1)
        return -1;

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        Q_SOCK_CLOSE(fd);
        return -1;
    }
    *fd_out = fd;
    return 0;
}
