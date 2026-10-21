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

#ifdef Q_SYS_WINDOWS
    /* Windows：CNG 系统级 CSPRNG（BCryptGenRandom，系统首选 RNG）。
       len 超 ULONG 范围时分批取，避免截断。 */
    unsigned char *p = (unsigned char *)buf;
    size_t got = 0;
    while (got < len)
    {
        ULONG chunk = (len - got > (size_t)0xFFFFFFFFu) ? 0xFFFFFFFFu : (ULONG)(len - got);
        NTSTATUS st = BCryptGenRandom(NULL, (PUCHAR)(p + got), chunk,
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (st < 0)          /* NTSTATUS：负值即失败 */
            return -1;
        got += (size_t)chunk;
    }
    return 0;
#else
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
#endif
}
