/**
 * @file netd_util.c
 * @brief netd 内部小工具：时间、大端序列化、socket 超时
 */
#include "srv_netd.h"

uint64_t now_sec(void)
{
    return (uint64_t)time(NULL);
}

void put_be16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xffu);
}

void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xffu);
}

/*
 * 设置 socket 收发超时。关键：worker 内是【阻塞式】握手与收包，
 * 若无超时，异常客户端可长期占用 worker 导致线程池耗尽（配合 §6.4 配额使用）。
 * 注：Windows 的 SO_RCVTIMEO/SO_SNDTIMEO 取 DWORD 毫秒，POSIX 取 struct timeval。
 */
int netd_set_io_timeout(q_fd_t fd, int sec)
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
    if (setsockopt((int)fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0)
        return -1;
    return setsockopt((int)fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}
