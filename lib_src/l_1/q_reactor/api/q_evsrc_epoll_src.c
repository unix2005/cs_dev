/**
 * @file q_evsrc_epoll_src.c
 * @brief q_evsrc_epoll_as_src —— 取插件接口，用于 q_disp_add_src 挂载
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_evsrc_int.h"

q_evsrc_t *q_evsrc_epoll_as_src(q_evsrc_epoll_t *e)
{
    return e ? &e->base : NULL;
}
