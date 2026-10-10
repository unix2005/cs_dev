/**
 * @file q_disp_add_src.c
 * @brief q_disp_add_src —— 挂载一个事件源插件（epoll / timer / 自定义源等）
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_disp_int.h"

int q_disp_add_src(q_disp_t *d, q_evsrc_t *src)
{
    if (!d || !src)
        return -1;
    if (d->nsrc == d->capsrc)
    {
        size_t nc = d->capsrc ? d->capsrc * 2 : 4;
        q_evsrc_t **ns = (q_evsrc_t **)realloc(d->srcs, nc * sizeof(*ns));
        if (!ns)
            return -1;
        d->srcs = ns;
        d->capsrc = nc;
    }
    d->srcs[d->nsrc++] = src;
    return 0;
}
