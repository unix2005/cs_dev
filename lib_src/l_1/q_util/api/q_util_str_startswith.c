/**
 * @file q_util_str_startswith.c
 * @brief q_util_str_startswith —— 判断字符串是否以 prefix 开头
 */
#include "headers.h"
#include "q_util.h"

int q_util_str_startswith(const char *s, const char *prefix)
{
    if (!s || !prefix)
        return 0;
    return strncmp(s, prefix, strlen(prefix)) == 0 ? 1 : 0;
}
