/**
 * @file q_reactor_create.c
 * @brief q_reactor_create —— 创建 reactor（= 通用分发核心 q_disp + 平台事件源插件）
 * @note  reactor 不再是独立 epoll 结构，而是 q_disp 挂载一个【按平台选择】的事件源插件后的便捷封装，
 *        handler 仍在该循环线程内联执行（语义同改造前）。事件源（epoll/kqueue/poll）作为“可选插件”存在。
 *        平台选择：
 *          Linux   -> epoll
 *          macOS/  -> kqueue
 *          Windows -> WSAPoll
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

    /* 按平台选择事件源插件（仅其一可用，其余返回 NULL） */
#if defined(Q_SYS_LINUX)
    r->src = q_evsrc_epoll_as_src(q_evsrc_epoll_create());
#elif defined(Q_SYS_MACOS)
    r->src = q_evsrc_kqueue_as_src(q_evsrc_kqueue_create());
#elif defined(Q_SYS_WINDOWS)
    r->src = q_evsrc_poll_as_src(q_evsrc_poll_create());
#else
    r->src = NULL;
#endif

    if (!r->src)
    {
        q_disp_destroy(r->disp);
        free(r);
        return NULL;
    }

    if (q_disp_add_src(r->disp, r->src) != 0)
    {
        q_disp_destroy(r->disp);   /* 内部会销毁已挂载的事件源插件 */
        free(r);
        return NULL;
    }
    return r;
}
