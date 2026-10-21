/**
 * @file q_reactor_timer_create.c
 * @brief q_reactor_timer_create —— 创建跨平台定时器
 * @note  产出“一个可读 fd”，由调用方 q_reactor_add(tfd, EPOLLIN, ...) 纳入调度（handler 内需读 1 字节清空）。
 *        Linux: timerfd；macOS/BSD: 自管道；Windows: 回环 UDP socket。
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_timer_int.h"

int q_reactor_timer_create(q_fd_t *tfd_out)
{
    if (!tfd_out)
        return -1;

    struct q_timer_state *s =
        (struct q_timer_state *)calloc(1, sizeof(*s));
    if (!s)
        return -1;
    s->tfd = -1;
    s->wfd = -1;
    s->stop = 0;
    s->running = 0;
    s->is_threaded = 0;

#if defined(Q_SYS_LINUX)
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0)
    {
        free(s);
        return -1;
    }
    s->tfd = fd;
    s->wfd = -1;            /* timerfd 无需写端 */
    s->is_threaded = 0;

#elif defined(Q_SYS_MACOS)
    int fds[2];
    if (pipe(fds) != 0)
    {
        free(s);
        return -1;
    }
    /* 读/写端均设为非阻塞 + close-on-exec */
    for (int i = 0; i < 2; i++)
    {
        int fl = fcntl(fds[i], F_GETFL, 0);
        fcntl(fds[i], F_SETFL, fl | O_NONBLOCK);
        int fc = fcntl(fds[i], F_GETFD, 0);
        fcntl(fds[i], F_SETFD, fc | FD_CLOEXEC);
    }
    s->tfd = fds[0];        /* 读端：可被 reactor 监听 */
    s->wfd = fds[1];        /* 写端：周期线程写入 */
    s->is_threaded = 1;

#elif defined(Q_SYS_WINDOWS)
    SOCKET sk = Q_SOCKET(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sk == INVALID_SOCKET)
    {
        free(s);
        return -1;
    }
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;
    if (bind(sk, (struct sockaddr *)&sa, sizeof(sa)) != 0)
    {
        closesocket(sk);
        free(s);
        return -1;
    }
    struct sockaddr_in local;
    int llen = sizeof(local);
    getsockname(sk, (struct sockaddr *)&local, &llen);
    /* 连接到自身，使该 socket 既可写也可读（自回环） */
    if (connect(sk, (struct sockaddr *)&local, sizeof(local)) != 0)
    {
        closesocket(sk);
        free(s);
        return -1;
    }
    u_long nb = 1;
    ioctlsocket(sk, FIONBIO, &nb);   /* 非阻塞 */
    s->tfd = (q_fd_t)sk;   /* Windows 下 fd 为 64 位 SOCKET，q_fd_t 原样保存不截断 */
    s->wfd = (q_fd_t)sk;
    s->is_threaded = 1;

#else
    free(s);
    return -1;
#endif

    q_timer_register(s);
    *tfd_out = s->tfd;
    return 0;
}
