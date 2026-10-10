/**
 * @file q_ipc_listen.c
 * @brief q_ipc_listen —— 创建并监听 UDS
 */
#include "headers.h"
#include "q_ipc.h"

int q_ipc_listen(const char *path, int backlog, int *fd_out)
{
    if (!path || !fd_out)
        return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(addr.sun_path))
    {
        close(fd);
        return -1;
    }
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    unlink(path);   /* 允许进程重启：先清理残留 socket 文件 */
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return -1;
    }
    if (listen(fd, backlog > 0 ? backlog : 8) < 0)
    {
        close(fd);
        return -1;
    }
    *fd_out = fd;
    return 0;
}
