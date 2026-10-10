/**
 * @file q_reactor_destroy.c
 * @brief q_reactor_destroy —— 销毁 reactor
 * @note  事件源（epoll 插件）由 q_disp_destroy 经 base.destroy 回收，此处无需单独释放 r->ep。
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

void q_reactor_destroy(q_reactor_t *r)
{
    if (!r)
        return;
    q_disp_destroy(r->disp);   /* 内部会销毁已挂载的 epoll 事件源插件 */
    free(r);
}
