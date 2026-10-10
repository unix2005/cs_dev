/**
 * @file q_net_udp_recv.c
 * @brief q_net_udp_recv —— UDP 接收并填对端地址（v4/v6 通用）
 * @note from 可为 NULL；成功返回字节数，出错 -1
 */
#include "headers.h"
#include "q_net.h"

ssize_t q_net_udp_recv(int fd, void *buf, size_t cap,
                       struct sockaddr *from, socklen_t *fromlen)
{
    if (fd < 0 || !buf || cap == 0)
        return -1;
    return recvfrom(fd, buf, cap, 0, from, fromlen);
}
