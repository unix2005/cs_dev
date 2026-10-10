/**
 * @file q_xcfg_has_key.c
 * @brief q_xcfg_has_key —— 检查配置键是否存在
 */
#include "headers.h"
#include "q_xcfg.h"

bool q_xcfg_has_key(q_xcfg_ctx_t *ctx, const char *key)
{
    if (!ctx || !key)
        return false;

    xmlNodePtr node = q_xcfg_find_node(ctx, key);
    return (node != NULL);
}
