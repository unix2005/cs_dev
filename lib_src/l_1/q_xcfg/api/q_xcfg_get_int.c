/**
 * @file q_xcfg_get_int.c
 * @brief q_xcfg_get_int —— 读取整数配置
 */
#include "headers.h"
#include "q_xcfg.h"

int q_xcfg_get_int(q_xcfg_ctx_t *ctx, const char *key, int default_value)
{
    if (!ctx || !key)
        return default_value;

    xmlNodePtr node = q_xcfg_find_node(ctx, key);
    if (!node || !node->content)
        return default_value;

    return atoi((const char *)node->content);
}
