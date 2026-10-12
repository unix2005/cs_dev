/**
 * @file q_evsrc_poll_del.c
 * @brief q_evsrc_poll_del —— 注销 socket（Windows）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_evsrc_int.h"

int q_evsrc_poll_del(q_evsrc_poll_t *e, q_fd_t fd)
{
#ifdef Q_SYS_WINDOWS
    if (!e || fd == (q_fd_t)INVALID_SOCKET)
        return -1;
    for (size_t i = 0; i < e->n; i++)
    {
        if (e->ents[i] && e->ents[i]->fd == fd)
        {
            free(e->ents[i]);
            /* swap-remove：末尾元素前移，保持数组稠密 */
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
