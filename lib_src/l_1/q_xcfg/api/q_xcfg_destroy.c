/**
 * @file q_xcfg_destroy.c
 * @brief q_xcfg_destroy —— 释放配置上下文
 */
#include "headers.h"
#include "q_xcfg.h"

void q_xcfg_destroy(q_xcfg_ctx_t *ctx)
{
    if (!ctx)
        return;

    if (ctx->xpath)
        xmlXPathFreeContext(ctx->xpath);
    if (ctx->doc)
        xmlFreeDoc(ctx->doc);
    if (ctx->error_msg)
        free(ctx->error_msg);

    free(ctx);

    /* 全局清理 libxml2 解析器状态（本模块为项目内唯一 libxml2 使用者） */
    xmlCleanupParser();
}
