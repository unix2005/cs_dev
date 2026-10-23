/**
 * @file push_util.c
 * @brief 推送微服务工具：大端序列化 / hex 编码 / IO 超时 / 定长帧收发 / 时间串
 *
 * 分帧格式与 netd<->bizd IPC 一致：[4 字节大端长度][TLV 序列]。
 * 帧内 TLV 用 q_codec（l_2），本文件只负责"长度前缀 + 整帧收发"。
 */
#include "srv_push.h"

#include <time.h>

void push_put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

uint32_t push_get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

int push_hex_encode(const uint8_t *b, size_t len, char *out, size_t outcap)
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

int push_set_io_timeout(q_fd_t fd, int sec)
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

int push_send_frame(q_fd_t fd, const uint8_t *buf, size_t len)
{
    uint8_t pfx[PUSH_LENPFX];

    if (len > 0xFFFFFFFFu)
        return -1;
    push_put_be32(pfx, (uint32_t)len);
    if (q_ipc_send(fd, pfx, PUSH_LENPFX) != (ssize_t)PUSH_LENPFX)
        return -1;
    if (len > 0u && q_ipc_send(fd, buf, len) != (ssize_t)len)
        return -1;
    return 0;
}

/* 把推送域 TLV 序列包装成 netd_B 约定的外层帧：
   TLV(CMD=0x0052|0x0053, uint16 网络序) + TLV(BODY=inner)。
   pushd -> netd_B 的每一帧都必须走这里 —— 曾漏掉外层直接发 inner，
   探针按契约解析时表现为"一帧都收不到"（tag 对不上），且极难定位。
   返回外层总长度；0 = 失败（空间不足 / inner 超 64KB）。 */
size_t push_wrap_cmd_body(uint16_t cmd, const uint8_t *inner, size_t inner_len,
                          uint8_t *out, size_t outcap)
{
    uint8_t cb[2];
    size_t  off = 0u, n = 0u;

    if (!out || outcap < 12u || inner_len > 0xFFFFu)
        return 0u;
    cb[0] = (uint8_t)(cmd >> 8);
    cb[1] = (uint8_t)(cmd & 0xffu);
    if (q_codec_tlv_encode(out, outcap, Q_IPC_T_CMD, cb, 2u, &n) != 0)
        return 0u;
    off = n;
    if (inner_len > 0u &&
        q_codec_tlv_encode(out + off, outcap - off, Q_IPC_T_BODY, inner,
                           (uint16_t)inner_len, &n) != 0)
        return 0u;
    off += n;
    return off;
}

/* 整帧读取：长度前缀 + 载荷。超时/EOF/错误一律 -1（调用方据此拆链）。 */
/* 精确读满 len 字节：q_ipc_recv 是单次 recv（可能短读），
   长度前缀与载荷都必须循环读满，否则一次部分到达就会把帧界读错。 */
static int ipc_read_exact(q_fd_t fd, uint8_t *buf, size_t len)
{
    size_t got = 0u;

    while (got < len)
    {
        ssize_t n = q_ipc_recv(fd, buf + got, len - got);
        if (n <= 0)
            return -1;               /* 0=对端关闭，<0 出错/超时 */
        got += (size_t)n;
    }
    return 0;
}

int push_recv_frame(q_fd_t fd, uint8_t *buf, size_t cap, size_t *outlen)
{
    uint8_t  pfx[PUSH_LENPFX];
    uint32_t need;

    if (ipc_read_exact(fd, pfx, PUSH_LENPFX) != 0)
        return -1;                      /* EOF / 超时 / 错误 */
    need = push_get_be32(pfx);
    if (need == 0u || need > cap)
        return -1;                      /* 长度非法：直接断，防缓冲溢出 */
    if (ipc_read_exact(fd, buf, need) != 0)
        return -1;
    if (outlen)
        *outlen = need;
    return 0;
}

void push_now_str(char *dst, size_t cap)
{
    time_t    t;
    struct tm tm;

    if (!dst || cap == 0u)
        return;
    t = time(NULL);
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    if (strftime(dst, cap, "%Y-%m-%d %H:%M:%S", &tm) == 0u)
        snprintf(dst, cap, "1970-01-01 00:00:00");
}
