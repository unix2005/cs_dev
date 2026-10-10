/**
 * @file q_net_send_blocking.c
 * @brief q_net_send_blocking —— 客户端阻塞完整发送（处理短写/EINTR）
 */
#include "headers.h"
#include "q_net.h"

ssize_t q_net_send_blocking(int fd, const void *buf, size_t len)
{
    if (fd < 0 || (!buf && len))
        return -1;
    size_t off = 0;
    while (off < len)
    {
        ssize_t n = send(fd, (const char *)buf + off, len - off, MSG_NOSIGNAL);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break;
        off += (size_t)n;
    }
    return (ssize_t)off;
}
