/**
 * @file qc_push_stop.c
 * @brief M5：qc_push_stop —— 停止推送长连接（幂等）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 唤醒阻塞 recv 的手段：shutdown(fd) —— 对端视角就是一次正常关闭，
 * 阻塞中的 recv 立即返回，线程随后发现 stop 标志退出。
 * 不用 CloseHandle/close 直接关：在另一个线程正 recv 的 fd 上 close，
 * Windows 下行为未定义（可能崩在 recv 内部），POSIX 下也是实现相关。
 */
#include "qc_push_int.h"   /* 公开 API + 内部结构体 */

#include "q_net.h"

#ifdef Q_SYS_WINDOWS
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

int qc_push_stop(qc_push_t *p)
{
    if (!p)
        return -1;

    p->stop = 1;

    if (p->thread_on)
    {
        /* shutdown 唤醒阻塞中的 recv（线程随后自行退出） */
        if (p->sess && p->sess->fd != (q_fd_t)-1)
        {
#ifdef Q_SYS_WINDOWS
            shutdown((SOCKET)p->sess->fd, SD_BOTH);
#else
            shutdown((int)p->sess->fd, SHUT_RDWR);
#endif
        }
        qc_thread_join(&p->thread);
        p->thread_on = 0;
    }

    if (p->sess)
        qc_close(p->sess);
    p->connected = 0;
    return 0;
}
