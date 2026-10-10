/**
 * @file q_ipc_recv.c
 * @brief q_ipc_recv —— 接收（单次）
 */
#include "headers.h"
#include "q_ipc.h"

ssize_t q_ipc_recv(int fd, void *buf, size_t cap)
{
    if (fd < 0 || !buf || cap == 0)
        return -1;
    return recv(fd, buf, cap, 0);
}
