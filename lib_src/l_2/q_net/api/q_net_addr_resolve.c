/**
 * @file q_net_addr_resolve.c
 * @brief q_net_addr_resolve —— 解析 host:port 为 sockaddr_storage（自动识别 v4/v6）
 */
#include "headers.h"
#include "q_net.h"

int q_net_addr_resolve(const char *host, int port,
                       struct sockaddr_storage *ss, socklen_t *len)
{
    if (!host || !ss || !len)
        return -1;
    memset(ss, 0, sizeof(*ss));

    struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)ss;
    if (inet_pton(AF_INET6, host, &s6->sin6_addr) == 1)
    {
        s6->sin6_family = AF_INET6;
        s6->sin6_port = htons((uint16_t)port);
        *len = sizeof(*s6);
        return 0;
    }
    struct sockaddr_in *s4 = (struct sockaddr_in *)ss;
    if (inet_pton(AF_INET, host, &s4->sin_addr) == 1)
    {
        s4->sin_family = AF_INET;
        s4->sin_port = htons((uint16_t)port);
        *len = sizeof(*s4);
        return 0;
    }
    return -1;
}
