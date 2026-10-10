/**
 * @file q_ipc_close.c
 * @brief q_ipc_close —— 关闭 fd 并可选清理 socket 文件
 */
#include "headers.h"
#include "q_ipc.h"

void q_ipc_close(int fd, const char *path)
{
    if (fd >= 0)
        close(fd);
    if (path)
        unlink(path);
}
