/**
 * @file q_reactor_destroy.c
 * @brief q_reactor_destroy —— 销毁通用反应器
 */
#include "headers.h"
#include "q_reactor.h"
#include "q_reactor_int.h"

void q_reactor_destroy(q_reactor_t *r)
{
    if (!r)
        return;
    for (size_t i = 0; i < r->n; i++)
        free(r->ents[i]);
    free(r->ents);
    if (r->epfd >= 0)
        close(r->epfd);
    free(r);
}
