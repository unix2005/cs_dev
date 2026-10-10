/**
 * @file q_util_b64_encode.c
 * @brief q_util_b64_encode —— 二进制转 Base64（RFC4648，含 '=' 填充）
 */
#include "headers.h"
#include "q_util.h"

char *q_util_b64_encode(const void *bin, size_t len)
{
    if (!bin)
        return NULL;

    if (len == 0)
    {
        char *r = (char *)malloc(1);
        if (r)
            r[0] = '\0';
        return r;
    }

    size_t out_len = ((len + 2) / 3) * 4;
    char *out = (char *)malloc(out_len + 1);
    if (!out)
        return NULL;

    const unsigned char *p = (const unsigned char *)bin;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3)
    {
        unsigned int n = ((unsigned int)p[i]) << 16;
        if (i + 1 < len) n |= ((unsigned int)p[i + 1]) << 8;
        if (i + 2 < len) n |= (unsigned int)p[i + 2];

        out[o++] = Q_UTIL_B64[(n >> 18) & 0x3F];
        out[o++] = Q_UTIL_B64[(n >> 12) & 0x3F];
        out[o++] = (i + 1 < len) ? Q_UTIL_B64[(n >> 6) & 0x3F] : '=';
        out[o++] = (i + 2 < len) ? Q_UTIL_B64[n & 0x3F] : '=';
    }
    out[out_len] = '\0';
    return out;
}
