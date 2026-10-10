/**
 * @file q_net_srv_set_handler.c
 * @brief q_net_srv_set_handler —— 设置业务处理回调
 */
#include "headers.h"
#include "q_net.h"
#include "q_net_srv_int.h"

void q_net_srv_set_handler(q_net_srv_t *s, q_net_srv_on_data on_data, void *ctx)
{
    if (!s)
        return;
    s->on_data = on_data;
    s->ctx = ctx;
}
