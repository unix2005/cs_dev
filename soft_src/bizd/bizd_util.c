/**
 * @file bizd_util.c
 * @brief bizd 内部小工具：大端序列化、分帧读写（UDS / TCP 通用）
 * @note 分帧格式与 include_ipc_proto.h 约定一致：[4 字节大端长度][负载]
 */
#include "srv_bizd.h"

#ifndef Q_SYS_WINDOWS
#define BIZD_SENDFLAG MSG_NOSIGNAL   /* 避免写已关闭 socket 触发 SIGPIPE */
#else
#define BIZD_SENDFLAG 0
#endif

uint64_t bizd_now_sec(void)
{
    return (uint64_t)time(NULL);
}

void bizd_put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xffu);
}

void bizd_put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xffu);
}

uint16_t bizd_get_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

uint32_t bizd_get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* 循环读取直到收满 len 字节；返回 0 成功，-1 失败（含对端关闭） */
static int recv_exact(int fd, uint8_t *buf, size_t len)
{
    size_t got = 0u;

    while (got < len)
    {
        ssize_t n = recv(fd, buf + got, len - got, 0);
        if (n <= 0)
            return -1;
        got += (size_t)n;
    }
    return 0;
}

/* 循环发送直到全部发完；返回 0 成功，-1 失败 */
static int send_all(int fd, const uint8_t *buf, size_t len)
{
    size_t sent = 0u;

    while (sent < len)
    {
        ssize_t n = send(fd, buf + sent, len - sent, BIZD_SENDFLAG);
        if (n <= 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int bizd_send_frame(int fd, const uint8_t *buf, size_t len)
{
    uint8_t *msg;
    int      rc;

    if (!buf || len == 0u || len > Q_IPC_MAXMSG)
        return -1;

    msg = (uint8_t *)malloc(Q_IPC_LENPFX + len);
    if (!msg)
        return -1;

    bizd_put_be32(msg, (uint32_t)len);
    memcpy(msg + Q_IPC_LENPFX, buf, len);

    rc = send_all(fd, msg, Q_IPC_LENPFX + len);
    free(msg);
    return rc;
}

int bizd_recv_frame(int fd, uint8_t *buf, size_t cap, size_t *outlen)
{
    uint8_t hdr[Q_IPC_LENPFX];
    size_t  need;

    if (!buf || !outlen || cap == 0u)
        return -1;

    if (recv_exact(fd, hdr, Q_IPC_LENPFX) != 0)
        return -1;

    need = (size_t)bizd_get_be32(hdr);
    if (need == 0u || need > cap || need > Q_IPC_MAXMSG)
        return -1;

    if (recv_exact(fd, buf, need) != 0)
        return -1;

    *outlen = need;
    return 0;
}

/*
 * 设置 socket 收发超时；UDS 与 TCP 通用。
 * 必要性：worker 内是阻塞式读写，若无超时，异常对端可长期占用 worker 导致线程池耗尽。
 * 平台差异：Windows 的 SO_RCVTIMEO/SO_SNDTIMEO 取 DWORD 毫秒，POSIX 取 struct timeval。
 */
int bizd_set_io_timeout(int fd, int sec)
{
#ifdef Q_SYS_WINDOWS
    DWORD ms = (DWORD)((sec > 0 ? sec : 0) * 1000);
    if (setsockopt((SOCKET)fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof(ms)) != 0)
        return -1;
    return setsockopt((SOCKET)fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof(ms));
#else
    struct timeval tv;

    tv.tv_sec  = (time_t)(sec > 0 ? sec : 0);
    tv.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0)
        return -1;
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}
