/**
 * @file q_xcfg_dump.c
 * @brief q_xcfg_dump —— 打印全部配置（调试用）
 */
#include "headers.h"
#include "q_xcfg.h"

void q_xcfg_dump(q_xcfg_ctx_t *ctx)
{
    if (!ctx)
        return;

    printf("=== q_xcfg Configuration Dump ===\n");

    xmlNodePtr root = xmlDocGetRootElement(ctx->doc);
    if (!root)
        return;

    xmlXPathObjectPtr result = xmlXPathEvalExpression(BAD_CAST "//*/*/text()", ctx->xpath);
    if (result && result->nodesetval)
    {
        for (int i = 0; i < result->nodesetval->nodeNr; i++)
        {
            xmlNodePtr node = result->nodesetval->nodeTab[i];
            printf("%s = %s\n", (const char *)node->parent->name, (const char *)node->content);
        }
    }

    xmlXPathFreeObject(result);
    printf("=================================\n");
}
