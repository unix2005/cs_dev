/**
 * @file q_net_addr_ntop.c
 * @brief q_net_addr_ntop —— 把端点（地址族 + 地址 + 端口）格式化为 "ip:port"（端口为本机序），供日志定位
 */
#include "headers.h"
#include "q_net.h"

const char *q_net_addr_ntop(int fam, const void *addr, uint16_t port,
                            char *buf, size_t buflen)
{
    char ipb[INET6_ADDRSTRLEN];

    Q_WINSOCK_INIT();   /* Windows：inet_ntop 依赖 Winsock 已初始化 */
    if (!buf || buflen < INET6_ADDRSTRLEN + 8)
        return "?";
    if (addr == NULL)
    {
        (void)snprintf(buf, buflen, "?:%u", (unsigned)port);
        return buf;
    }
    if (fam == AF_INET)
    {
        if (inet_ntop(AF_INET, addr, ipb, sizeof(ipb)) != NULL)
            (void)snprintf(buf, buflen, "%s:%u", ipb, (unsigned)port);
        else
            (void)snprintf(buf, buflen, "?:%u", (unsigned)port);
    }
    else if (fam == AF_INET6)
    {
        if (inet_ntop(AF_INET6, addr, ipb, sizeof(ipb)) != NULL)
            (void)snprintf(buf, buflen, "[%s]:%u", ipb, (unsigned)port);
        else
            (void)snprintf(buf, buflen, "?:%u", (unsigned)port);
    }
    else
    {
        (void)snprintf(buf, buflen, "?:%u", (unsigned)port);
    }
    return buf;
}
