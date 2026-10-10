/**
 * @file q_ipc_connect.c
 * @brief q_ipc_connect —— 连接 UDS 监听端
 */
#include "headers.h"
#include "q_ipc.h"

int q_ipc_connect(const char *path, int *fd_out)
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

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(fd);
        return -1;
    }
    *fd_out = fd;
    return 0;
}
