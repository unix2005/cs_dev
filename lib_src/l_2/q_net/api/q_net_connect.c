/**
 * @file q_net_connect.c
 * @brief q_net_connect —— 连接主机（IPv4 / IPv6 字面量 / **域名**，M5 起支持域名）
 *
 * 域名支持的必要性：推送服务器地址要求可写成域名（换机房只改 DNS，三端 XML 配置不动）。
 * 实现分两层：
 *   1) 字面量 IP：inet_pton 直连（零系统调用、零 DNS，DNS 故障不影响）；
 *   2) 域名：getaddrinfo 后**遍历全部结果**逐个 connect —— 域名常对应多 A/AAAA 记录，
 *      只试第一条会在某条路由不可达时直接失败，遍历是客户端连接的常规做法。
 */
#include "headers.h"
#include "q_net.h"

/* 字面量探测：v4 或 v6 字面量返回 1（并填好 ss/slen），域名返回 0 */
static int q_net_literal_addr(const char *host, int port,
                              struct sockaddr_storage *ss, socklen_t *slen)
{
    struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)ss;
    struct sockaddr_in  *s4 = (struct sockaddr_in *)ss;

    memset(ss, 0, sizeof(*ss));
    if (inet_pton(AF_INET6, host, &s6->sin6_addr) == 1)
    {
        s6->sin6_family = AF_INET6;
        s6->sin6_port   = htons((uint16_t)port);
        *slen = sizeof(*s6);
        return 1;
    }
    if (inet_pton(AF_INET, host, &s4->sin_addr) == 1)
    {
        s4->sin_family = AF_INET;
        s4->sin_port   = htons((uint16_t)port);
        *slen = sizeof(*s4);
        return 1;
    }
    return 0;
}

int q_net_connect(const char *host, int port, q_fd_t *fd_out)
{
    struct sockaddr_storage ss;
    socklen_t slen = 0;
    struct addrinfo hints, *res = NULL, *ai;
    char portstr[16];
    int rc = -1;

    if (!host || port <= 0 || !fd_out)
        return -1;
    Q_WINSOCK_INIT();   /* Windows：inet_pton / getaddrinfo 依赖 Winsock 已初始化 */

    /* 1) 字面量快路径：直连，不碰 DNS */
    if (q_net_literal_addr(host, port, &ss, &slen))
    {
        q_fd_t fd = Q_SOCKET(ss.ss_family, SOCK_STREAM, 0);
        if (fd == (q_fd_t)-1)
            return -1;
        if (connect(fd, (struct sockaddr *)&ss, slen) < 0)
        {
            Q_SOCK_CLOSE(fd);
            return -1;
        }
        *fd_out = fd;
        return 0;
    }

    /* 2) 域名：解析后遍历全部候选地址逐个 connect。
          AI_ADDRCONFIG：本机无 IPv6 出口时不解析 AAAA，少一次无用的尝试。 */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
#ifdef AI_ADDRCONFIG
    hints.ai_flags    = AI_ADDRCONFIG;
#endif
    snprintf(portstr, sizeof(portstr), "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res)
        return -1;

    for (ai = res; ai; ai = ai->ai_next)
    {
        q_fd_t fd;
        if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6)
            continue;
        fd = Q_SOCKET(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == (q_fd_t)-1)
            continue;
        if (connect(fd, ai->ai_addr, (socklen_t)ai->ai_addrlen) == 0)
        {
            *fd_out = fd;
            rc = 0;
            break;
        }
        Q_SOCK_CLOSE(fd);
    }
    freeaddrinfo(res);
    return rc;
}
