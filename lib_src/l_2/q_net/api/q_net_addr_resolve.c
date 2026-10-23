/**
 * @file q_net_addr_resolve.c
 * @brief q_net_addr_resolve —— 解析 host:port 为 sockaddr_storage（IPv4 / IPv6 / **域名**）
 *
 * M5 起支持域名：推送服务器地址要能写成域名（换机房只改 DNS，三端配置不动）。
 * 顺序：先 inet_pton 试 IPv6 -> 再试 IPv4（字面量零系统调用、零 DNS）-> 都不是则
 * getaddrinfo 走系统解析。这样字面量 IP 路径完全不受 DNS 故障影响。
 */
#include "headers.h"
#include "q_net.h"

int q_net_addr_resolve(const char *host, int port,
                       struct sockaddr_storage *ss, socklen_t *len)
{
    struct addrinfo hints, *res = NULL, *ai;
    char portstr[16];
    int rc;

    if (!host || !ss || !len)
        return -1;
    Q_WINSOCK_INIT();   /* Windows：inet_pton / getaddrinfo 依赖 Winsock 已初始化 */
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

    /* 域名：走系统解析。不设 AI_NUMERICHOST（那正是要排除的）；
       AI_ADDRCONFIG 避免本机无 IPv6 出口时也去解析 AAAA 记录。 */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
#ifdef AI_ADDRCONFIG
    hints.ai_flags    = AI_ADDRCONFIG;
#endif
    snprintf(portstr, sizeof(portstr), "%d", port);
    rc = getaddrinfo(host, portstr, &hints, &res);
    if (rc != 0 || !res)
        return -1;

    /* 取第一条结果（getaddrinfo 已按 RFC 3484 排序；多 A 记录的负载/轮询交给系统）。 */
    for (ai = res; ai; ai = ai->ai_next)
    {
        if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
            continue;
        if ((socklen_t)ai->ai_addrlen > sizeof(*ss))
            continue;
        memcpy(ss, ai->ai_addr, (size_t)ai->ai_addrlen);
        *len = (socklen_t)ai->ai_addrlen;
        break;
    }
    freeaddrinfo(res);
    return (*len > 0) ? 0 : -1;
}
