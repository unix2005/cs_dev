/**
 * @file q_util_hex_encode.c
 * @brief q_util_hex_encode —— 二进制转小写十六进制字符串
 */
#include "headers.h"
#include "q_util.h"

char *q_util_hex_encode(const void *bin, size_t len)
{
    if (!bin)
        return NULL;

    char *out = (char *)malloc(len * 2 + 1);
    if (!out)
        return NULL;

    const unsigned char *p = (const unsigned char *)bin;
    for (size_t i = 0; i < len; i++)
    {
        out[i * 2] = Q_UTIL_HEX[p[i] >> 4];
        out[i * 2 + 1] = Q_UTIL_HEX[p[i] & 0x0F];
    }
    out[len * 2] = '\0';
    return out;
}
