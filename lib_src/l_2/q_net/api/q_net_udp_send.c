/**
 * @file q_net_udp_send.c
 * @brief q_net_udp_send —— UDP 发送（v4/v6 通用，基于 sockaddr）
 * @note to 为 sockaddr_in / sockaddr_in6；to 为 NULL 且 tolen=0 时按已 connect 的对端发送
 */
#include "headers.h"
#include "q_net.h"

ssize_t q_net_udp_send(int fd, const void *buf, size_t len,
                       const struct sockaddr *to, socklen_t tolen)
{
    if (fd < 0 || (!buf && len))
        return -1;
    return sendto(fd, buf, len, 0, to, tolen);
}
