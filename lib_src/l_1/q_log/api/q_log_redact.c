/**
 * @file q_log_redact.c
 * @brief q_log_redact —— 敏感信息脱敏
 * @note  确保口令 / OTP / Token 绝不落明文。Token 仅记前 8 位哈希，
 *        调用方应在传入审计/日志前完成哈希与脱敏。
 */
#include "headers.h"
#include "q_log.h"

char *q_log_redact(const char *p)
{
    const char *src = (!p || *p == '\0') ? "-" : "****";
    size_t n = strlen(src);
    char *r = (char *)malloc(n + 1);
    if (!r)
        return NULL;
    memcpy(r, src, n + 1);
    return r;
}
