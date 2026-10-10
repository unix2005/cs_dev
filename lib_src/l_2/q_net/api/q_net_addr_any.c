/**
 * @file q_net_addr_any.c
 * @brief q_net_addr_any —— 生成通配绑定地址（v6!=0 用 IN6ADDR_ANY，否则 INADDR_ANY）
 */
#include "headers.h"
#include "q_net.h"

int q_net_addr_any(int v6, int port, struct sockaddr_storage *ss, socklen_t *len)
{
    if (!ss || !len)
        return -1;
    memset(ss, 0, sizeof(*ss));
    if (v6)
    {
        struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)ss;
        s6->sin6_family = AF_INET6;
        s6->sin6_addr = in6addr_any;
        s6->sin6_port = htons((uint16_t)port);
        *len = sizeof(*s6);
    }
    else
    {
        struct sockaddr_in *s4 = (struct sockaddr_in *)ss;
        s4->sin_family = AF_INET;
        s4->sin_addr.s_addr = htonl(INADDR_ANY);
        s4->sin_port = htons((uint16_t)port);
        *len = sizeof(*s4);
    }
    return 0;
}
