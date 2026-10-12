/**
 * @file q_evsrc_kqueue_destroy.c
 * @brief q_evsrc_kqueue_destroy —— 销毁 kqueue 事件源插件
 * @note  通常由 q_disp_destroy 经 base.destroy 调用；此处提供显式销毁入口。
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_evsrc_int.h"

void q_evsrc_kqueue_destroy(q_evsrc_kqueue_t *e)
{
#ifdef Q_SYS_MACOS
    if (!e)
        return;
    if (e->base.destroy)
        e->base.destroy(&e->base);
#else
    (void)e;
#endif
}
