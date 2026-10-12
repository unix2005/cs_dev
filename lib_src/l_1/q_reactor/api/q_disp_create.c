/**
 * @file q_disp_create.c
 * @brief q_disp_create —— 创建通用任务分发核心（worker 池 + 唤醒通道 + 事件源容器）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_disp_int.h"

q_disp_t *q_disp_create(int nworkers)
{
    q_disp_t *d = (q_disp_t *)malloc(sizeof(*d));
    if (!d)
        return NULL;

    d->nworkers = nworkers > 0 ? nworkers : 0;
    d->pool = d->nworkers > 0 ? q_tpool_create(d->nworkers, 0) : NULL;
    if (d->nworkers > 0 && !d->pool)
    {
        free(d);
        return NULL;
    }

    d->srcs = NULL;
    d->nsrc = 0;
    d->capsrc = 0;
    d->stop = 0;
    d->lhead = d->ltail = NULL;
    pthread_mutex_init(&d->lmtx, NULL);

    /* 唤醒通道：用于跨线程唤醒阻塞中的 poll（停止 / 提交本地任务） */
#ifdef Q_SYS_WINDOWS
    /* Windows 无 pipe()/poll()：用“自回环 UDP socket”作为唤醒通道（WSAPoll 可监听） */
    {
        SOCKET sk = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sk == INVALID_SOCKET)
            goto fail;
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sa.sin_port = 0;
        if (bind(sk, (struct sockaddr *)&sa, sizeof(sa)) != 0)
        {
            closesocket(sk);
            goto fail;
        }
        struct sockaddr_in local;
        int llen = sizeof(local);
        getsockname(sk, (struct sockaddr *)&local, &llen);
        if (connect(sk, (struct sockaddr *)&local, sizeof(local)) != 0)
        {
            closesocket(sk);
            goto fail;
        }
        u_long nb = 1;
        ioctlsocket(sk, FIONBIO, &nb);   /* 非阻塞 */
        d->wake_r = (q_fd_t)sk;   /* Windows 下 wake_r/wake_w 为 64 位 SOCKET，q_fd_t 原样保存不截断 */
        d->wake_w = (q_fd_t)sk;
    }
#else
    int fds[2];
#ifdef __linux__
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0)
        goto fail;
#else
    if (pipe(fds) != 0)
        goto fail;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);
#endif
    d->wake_r = fds[0];
    d->wake_w = fds[1];
#endif

    return d;

fail:
    if (d->pool)
        q_tpool_destroy(d->pool);
    pthread_mutex_destroy(&d->lmtx);
    free(d);
    return NULL;
}
