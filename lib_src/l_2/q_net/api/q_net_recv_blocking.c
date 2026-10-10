/**
 * @file q_net_recv_blocking.c
 * @brief q_net_recv_blocking —— 客户端阻塞接收直到 len 字节或对端关闭
 * @note 返回接收字节数；<len 表示对端已关闭（EOF）；出错 -1
 */
#include "headers.h"
#include "q_net.h"

ssize_t q_net_recv_blocking(int fd, void *buf, size_t len)
{
    if (fd < 0 || !buf || len == 0)
        return -1;
    size_t off = 0;
    while (off < len)
    {
        ssize_t n = recv(fd, (char *)buf + off, len - off, 0);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break;                      /* 对端关闭 */
        off += (size_t)n;
    }
    return (ssize_t)off;
}
