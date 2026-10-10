/**
 * @file q_xcfg_get_string.c
 * @brief q_xcfg_get_string —— 读取字符串配置（调用方负责 free）
 */
#include "headers.h"
#include "q_xcfg.h"

char *q_xcfg_get_string(q_xcfg_ctx_t *ctx, const char *key, const char *default_value)
{
    if (!ctx || !key)
        return default_value ? strdup(default_value) : NULL;

    xmlNodePtr node = q_xcfg_find_node(ctx, key);
    if (!node || !node->content)
        return default_value ? strdup(default_value) : NULL;

    return strdup((const char *)node->content);
}
