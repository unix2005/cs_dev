/**
 * @file q_util_str_endswith.c
 * @brief q_util_str_endswith —— 判断字符串是否以 suffix 结尾
 */
#include "headers.h"
#include "q_util.h"

int q_util_str_endswith(const char *s, const char *suffix)
{
    if (!s || !suffix)
        return 0;
    size_t ls = strlen(s);
    size_t lsu = strlen(suffix);
    if (lsu > ls)
        return 0;
    return strcmp(s + ls - lsu, suffix) == 0 ? 1 : 0;
}
