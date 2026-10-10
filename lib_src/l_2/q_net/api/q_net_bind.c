/**
 * @file q_net_bind.c
 * @brief q_net_bind —— IPv4 UDP 绑定 INADDR_ANY（不 listen，UDP 无需监听）
 */
#include "headers.h"
#include "q_net.h"

int q_net_bind(int port, int *fd_out)
{
    if (port <= 0 || !fd_out)
        return -1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return -1;
    }
    *fd_out = fd;
    return 0;
}
