/**
 * @file q_evsrc_kqueue_del.c
 * @brief q_evsrc_kqueue_del —— 注销 fd（macOS/BSD）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_evsrc_int.h"

int q_evsrc_kqueue_del(q_evsrc_kqueue_t *e, q_fd_t fd)
{
#ifdef Q_SYS_MACOS
    if (!e || fd < 0)
        return -1;
    struct kevent ch;
    for (size_t i = 0; i < e->n; i++)
    {
        if (e->ents[i] && e->ents[i]->fd == fd)
        {
            struct q_reactor_ent *en = e->ents[i];
            EV_SET(&ch, (uintptr_t)fd, EVFILT_READ,  EV_DELETE, 0, 0, NULL);
            kevent(e->kqfd, &ch, 1, NULL, 0, NULL);
            EV_SET(&ch, (uintptr_t)fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
            kevent(e->kqfd, &ch, 1, NULL, 0, NULL);
            free(en);
            /* swap-remove：末尾元素前移，保持数组稠密（ent 结构地址不变，udata 仍有效） */
            e->ents[i] = e->ents[--e->n];
            return 0;
        }
    }
    return -1;
#else
    (void)e; (void)fd;
    return -1;
#endif
}
