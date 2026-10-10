/**
 * @file q_xcfg_get_float.c
 * @brief q_xcfg_get_float —— 读取浮点数配置
 */
#include "headers.h"
#include "q_xcfg.h"

double q_xcfg_get_float(q_xcfg_ctx_t *ctx, const char *key, double default_value)
{
    if (!ctx || !key)
        return default_value;

    xmlNodePtr node = q_xcfg_find_node(ctx, key);
    if (!node || !node->content)
        return default_value;

    return atof((const char *)node->content);
}
