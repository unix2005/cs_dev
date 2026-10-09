/**
 * @file q_mem_zero.c
 * @brief q_mem_zero —— 安全清零（防编译器优化掉敏感数据擦除）
 */
#include "headers.h"
#include "q_mem.h"

void q_mem_zero(void *buf, size_t n)
{
    if (!buf || n == 0)
        return;

#if defined(Q_SYS_WINDOWS)
    SecureZeroMemory(buf, n);
#elif defined(__GLIBC__)
    explicit_bzero(buf, n);
#else
    /* 兜底：以 volatile 逐字节写 0，杜绝编译器把清零当作死存储抹除 */
    volatile unsigned char *p = (volatile unsigned char *)buf;
    size_t i;
    for (i = 0; i < n; i++)
        p[i] = 0;
#endif
}
