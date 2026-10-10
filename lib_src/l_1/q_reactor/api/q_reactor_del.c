/**
 * @file q_reactor_del.c
 * @brief q_reactor_del —— 注销 fd
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

int q_reactor_del(q_reactor_t *r, int fd)
{
    if (!r || fd < 0)
        return -1;
    for (size_t i = 0; i < r->n; i++)
    {
        if (r->ents[i] && r->ents[i]->fd == fd)
        {
            struct q_reactor_ent *e = r->ents[i];
            epoll_ctl(r->epfd, EPOLL_CTL_DEL, fd, NULL);
            free(e);
            /* swap-remove：末尾元素前移，保持数组稠密（ent 结构地址不变，data.ptr 仍有效） */
            r->ents[i] = r->ents[--r->n];
            return 0;
        }
    }
    return -1;
}
