/**
 * @file q_reactor_timer_destroy.c
 * @brief q_reactor_timer_destroy —— 停止周期线程并关闭 fd（跨平台）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_timer_int.h"

int q_reactor_timer_destroy(q_fd_t tfd)
{
    struct q_timer_state *s = q_timer_find(tfd);
    if (!s)
        return -1;

    if (s->running)
    {
        s->stop = 1;
        pthread_join(s->thr, NULL);
        s->running = 0;
    }

#if defined(Q_SYS_WINDOWS)
    if (s->tfd >= 0)
        closesocket((SOCKET)s->tfd);
#else
    if (s->tfd >= 0)
        close(s->tfd);
    if (s->wfd >= 0 && s->wfd != s->tfd)
        close(s->wfd);
#endif

    q_timer_unregister(tfd);
    free(s);
    return 0;
}
