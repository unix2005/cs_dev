/**
 * @file q_ipc_send.c
 * @brief q_ipc_send —— 完整发送（处理短写/EINTR）
 */
#include "headers.h"
#include "q_ipc.h"

ssize_t q_ipc_send(int fd, const void *buf, size_t len)
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
        off += (size_t)n;
    }
    return (ssize_t)off;
}
