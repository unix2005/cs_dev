/**
 * @file q_util_b64_decode.c
 * @brief q_util_b64_decode —— Base64 转二进制（支持标准 '=' 填充）
 */
#include "headers.h"
#include "q_util.h"

void *q_util_b64_decode(const char *b64, size_t *out_len)
{
    if (!b64 || !out_len)
        return NULL;

    size_t len = strlen(b64);
    if (len == 0 || (len % 4) != 0)
        return NULL;

    /* 统计尾部 '=' 填充数（仅允许 1 或 2 个） */
    size_t pad = 0;
    while (pad < len && b64[len - 1 - pad] == '=')
        pad++;
    if (pad > 2)
        return NULL;

    size_t dec_len = len / 4 * 3 - pad;
    unsigned char *out = (unsigned char *)malloc(dec_len + 1);
    if (!out)
        return NULL;

    size_t o = 0;
    for (size_t i = 0; i < len; i += 4)
    {
        int v0 = q_util_b64_val(b64[i]);
        int v1 = q_util_b64_val(b64[i + 1]);
        int v2 = (b64[i + 2] == '=') ? 0 : q_util_b64_val(b64[i + 2]);
        int v3 = (b64[i + 3] == '=') ? 0 : q_util_b64_val(b64[i + 3]);

        if (v0 < 0 || v1 < 0 ||
            (b64[i + 2] != '=' && v2 < 0) ||
            (b64[i + 3] != '=' && v3 < 0))
        {
            free(out);
            return NULL;
        }

        unsigned int n = ((unsigned int)v0 << 18) | ((unsigned int)v1 << 12) |
                         ((unsigned int)v2 << 6) | (unsigned int)v3;
        out[o++] = (unsigned char)((n >> 16) & 0xFF);
        if (o < dec_len) out[o++] = (unsigned char)((n >> 8) & 0xFF);
        if (o < dec_len) out[o++] = (unsigned char)(n & 0xFF);
    }
    out[dec_len] = '\0';
    *out_len = dec_len;
    return out;
}
