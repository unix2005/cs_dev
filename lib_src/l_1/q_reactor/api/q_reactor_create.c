/**
 * @file q_reactor_create.c
 * @brief q_reactor_create —— 创建 reactor（= 通用分发核心 q_disp + epoll 事件源插件）
 * @note  改造后 reactor 不再是独立 epoll 结构，而是 q_disp 挂载 epoll 插件后的便捷封装，
 *        handler 仍在该循环线程内联执行（语义同改造前）。epoll 仅作为“可选插件”存在。
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

q_reactor_t *q_reactor_create(void)
{
    q_reactor_t *r = (q_reactor_t *)malloc(sizeof(*r));
    if (!r)
        return NULL;

    r->disp = q_disp_create(0);   /* reactor 不自带 worker 池（并行工作由 q_net 单独的 q_tpool 负责） */
    if (!r->disp)
    {
        free(r);
        return NULL;
    }

    r->ep = q_evsrc_epoll_create();
    if (!r->ep)
    {
        q_disp_destroy(r->disp);
        free(r);
        return NULL;
    }

    if (q_disp_add_src(r->disp, q_evsrc_epoll_as_src(r->ep)) != 0)
    {
        q_evsrc_epoll_destroy(r->ep);
        q_disp_destroy(r->disp);
        free(r);
        return NULL;
    }
    return r;
}
