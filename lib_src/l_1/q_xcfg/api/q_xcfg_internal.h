/**
 * @file q_xcfg_internal.h
 * @brief q_xcfg 内部辅助（仅被 headers.h 引入，外部不可见）
 * @note  本文件必须在 include_xml.h 与 q_xcfg.h 之后被包含，
 *        故此处不再重复引入它们；仅补充 <strings.h> 以获取 strcasecmp。
 */
#ifndef Q_XCFG_INTERNAL_H
#define Q_XCFG_INTERNAL_H

#include <strings.h> /* strcasecmp */

/* 完整上下文结构（外部通过 q_xcfg.h 的 q_xcfg_ctx_t 仅前向声明） */
struct q_xcfg
{
    xmlDocPtr doc;            /* XML 文档指针 */
    xmlXPathContextPtr xpath; /* XPath 上下文 */
    char *error_msg;          /* 错误信息（预留） */
};

/**
 * 将点分路径转换为 XPath 表达式。
 * 例如 "gateway.port" -> "/gateway/port/text()"
 * 调用方负责 free 返回值。
 */
static inline char *q_xcfg_key_to_xpath(const char *key)
{
    if (!key)
        return NULL;

    char *xpath = (char *)malloc(512);
    if (!xpath)
        return NULL;

    char *path = strdup(key);
    if (!path)
    {
        free(xpath);
        return NULL;
    }

    for (char *p = path; (p = strchr(p, '.')) != NULL; )
        *p++ = '/';

    snprintf(xpath, 512, "/%s/text()", path);
    free(path);
    return xpath;
}

/* 按点分键查找首个匹配的文本节点 */
static inline xmlNodePtr q_xcfg_find_node(q_xcfg_ctx_t *ctx, const char *key)
{
    if (!ctx || !key)
        return NULL;

    char *xpath_expr = q_xcfg_key_to_xpath(key);
    if (!xpath_expr)
        return NULL;

    xmlXPathObjectPtr result = xmlXPathEvalExpression(BAD_CAST xpath_expr, ctx->xpath);
    free(xpath_expr);
    if (!result)
        return NULL;

    xmlNodePtr node = NULL;
    if (result->nodesetval && result->nodesetval->nodeNr > 0)
        node = result->nodesetval->nodeTab[0];

    xmlXPathFreeObject(result);
    return node;
}

#endif /* Q_XCFG_INTERNAL_H */
