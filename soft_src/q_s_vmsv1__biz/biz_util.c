/**
 * @file biz_util.c
 * @brief 业务微服务工具：大端序列化 / hex 编解码 / IO 超时 / 定长帧收发
 *
 * 分帧格式与 bizd 下游契约一致：[4 字节大端长度][JSON]。
 */
#include "srv_biz.h"

void biz_put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

uint32_t biz_get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int biz_hex_encode(const uint8_t *b, size_t len, char *out, size_t outcap)
{
    static const char d[] = "0123456789abcdef";
    size_t i;

    if (!out || outcap < len * 2u + 1u)
        return -1;
    for (i = 0u; i < len; i++)
    {
        out[i * 2u]      = d[(b[i] >> 4) & 0x0fu];
        out[i * 2u + 1u] = d[b[i] & 0x0fu];
    }
    out[len * 2u] = '\0';
    return (int)(len * 2u);
}

int biz_hex_decode(const char *hex, uint8_t *out, size_t outcap)
{
    size_t n = 0u, i = 0u;

    if (!hex || !out)
        return -1;
    while (hex[i] != '\0' && hex[i + 1u] != '\0')
    {
        int hi = hexval((unsigned char)hex[i]);
        int lo = hexval((unsigned char)hex[i + 1u]);
        if (hi < 0 || lo < 0)
            return -1;
        if (n >= outcap)
            return -1;
        out[n++] = (uint8_t)((hi << 4) | lo);
        i += 2u;
    }
    if (hex[i] != '\0')   /* 奇数长度 */
        return -1;
    return (int)n;
}

int biz_set_io_timeout(q_fd_t fd, int sec)
{
    struct timeval tv;

    tv.tv_sec  = sec;
    tv.tv_usec = 0;
    if (setsockopt((int)fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0)
        return -1;
    if (setsockopt((int)fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0)
        return -1;
    return 0;
}

int biz_send_frame(q_fd_t fd, const uint8_t *buf, size_t len)
{
    uint8_t pfx[BIZ_LENPFX];

    if (len > 0xFFFFFFFFu)
        return -1;
    biz_put_be32(pfx, (uint32_t)len);
    if (q_net_send_blocking(fd, pfx, BIZ_LENPFX) != (ssize_t)BIZ_LENPFX)
        return -1;
    if (len > 0u && q_net_send_blocking(fd, buf, len) != (ssize_t)len)
        return -1;
    return 0;
}

int biz_recv_frame(q_fd_t fd, uint8_t *buf, size_t cap, size_t *outlen)
{
    uint8_t  pfx[BIZ_LENPFX];
    uint32_t need;
    ssize_t  r;

    r = q_net_recv_blocking(fd, pfx, BIZ_LENPFX);
    if (r != (ssize_t)BIZ_LENPFX)
        return -1;                      /* EOF / 错误 */
    need = biz_get_be32(pfx);
    if (need == 0u || need > cap)
        return -1;
    r = q_net_recv_blocking(fd, buf, need);
    if (r != (ssize_t)need)
        return -1;
    if (outlen)
        *outlen = need;
    return 0;
}
