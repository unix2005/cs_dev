/**
 * @file q_evsrc_epoll_del.c
 * @brief q_evsrc_epoll_del —— 注销 fd（仅 Linux）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_evsrc_int.h"

int q_evsrc_epoll_del(q_evsrc_epoll_t *e, int fd)
{
#ifdef __linux__
    if (!e || fd < 0)
        return -1;
    for (size_t i = 0; i < e->n; i++)
    {
        if (e->ents[i] && e->ents[i]->fd == fd)
        {
            struct q_reactor_ent *en = e->ents[i];
            epoll_ctl(e->epfd, EPOLL_CTL_DEL, fd, NULL);
            free(en);
            /* swap-remove：末尾元素前移，保持数组稠密（ent 结构地址不变，data.ptr 仍有效） */
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
