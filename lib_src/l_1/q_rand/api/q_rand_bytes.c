/**
 * @file q_rand_bytes.c
 * @brief q_rand_bytes —— 从 /dev/urandom 读取高强度随机字节
 */
#include "headers.h"
#include "q_rand.h"

int q_rand_bytes(void *buf, size_t len)
{
    if (!buf || len == 0)
        return -1;

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return -1;

    unsigned char *p = (unsigned char *)buf;
    size_t got = 0;
    while (got < len)
    {
        ssize_t r = read(fd, p + got, len - got);
        if (r < 0)
        {
            if (errno == EINTR)
                continue;
            close(fd);
            return -1;
        }
        if (r == 0)
            break;
        got += (size_t)r;
    }
    close(fd);
    return (got == len) ? 0 : -1;
}
