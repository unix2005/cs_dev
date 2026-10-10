/**
 * @file q_xcfg_init.c
 * @brief q_xcfg_init —— 打开并解析 XML 配置文件
 */
#include "headers.h"
#include "q_xcfg.h"

q_xcfg_ctx_t *q_xcfg_init(const char *config_file)
{
    if (!config_file)
        return NULL;

    LIBXML_TEST_VERSION

    xmlDocPtr doc = xmlReadFile(config_file, NULL, 0);
    if (!doc)
    {
        fprintf(stderr, "q_xcfg: failed to parse config file: %s\n", config_file);
        return NULL;
    }

    xmlXPathContextPtr xpath = xmlXPathNewContext(doc);
    if (!xpath)
    {
        xmlFreeDoc(doc);
        return NULL;
    }

    q_xcfg_ctx_t *ctx = (q_xcfg_ctx_t *)calloc(1, sizeof(q_xcfg_ctx_t));
    if (!ctx)
    {
        xmlXPathFreeContext(xpath);
        xmlFreeDoc(doc);
        return NULL;
    }

    ctx->doc = doc;
    ctx->xpath = xpath;
    ctx->error_msg = NULL;
    return ctx;
}
