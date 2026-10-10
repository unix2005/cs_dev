/**
 * @file q_util_hex_decode.c
 * @brief q_util_hex_decode —— 十六进制字符串转二进制
 */
#include "headers.h"
#include "q_util.h"

void *q_util_hex_decode(const char *hex, size_t *out_len)
{
    if (!hex || !out_len)
        return NULL;

    size_t len = strlen(hex);
    if (len % 2 != 0)
        return NULL;

    unsigned char *out = (unsigned char *)malloc(len / 2 + 1);
    if (!out)
        return NULL;

    for (size_t i = 0; i < len; i += 2)
    {
        int hi = q_util_hex_val(hex[i]);
        int lo = q_util_hex_val(hex[i + 1]);
        if (hi < 0 || lo < 0)
        {
            free(out);
            return NULL;
        }
        out[i / 2] = (unsigned char)((hi << 4) | lo);
    }
    out[len / 2] = '\0';
    *out_len = len / 2;
    return out;
}
