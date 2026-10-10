/**
 * @file q_xcfg_get_bool.c
 * @brief q_xcfg_get_bool —— 读取布尔配置
 */
#include "headers.h"
#include "q_xcfg.h"

bool q_xcfg_get_bool(q_xcfg_ctx_t *ctx, const char *key, bool default_value)
{
    if (!ctx || !key)
        return default_value;

    xmlNodePtr node = q_xcfg_find_node(ctx, key);
    if (!node || !node->content)
        return default_value;

    const char *str = (const char *)node->content;
    if (strcasecmp(str, "true") == 0 ||
        strcasecmp(str, "yes") == 0 ||
        strcmp(str, "1") == 0)
        return true;

    return false;
}
