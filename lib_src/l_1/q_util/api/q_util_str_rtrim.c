/**
 * @file q_util_str_rtrim.c
 * @brief q_util_str_rtrim —— 原地去除尾部空白（不分配内存，直接修改入参）
 *
 * 仅裁剪末尾空白，保留前导与中间空白；与 q_util_str_trim 风格一致：
 *   - 零分配、原地修改，规避 malloc/free 配对内存问题；
 *   - 入参 s 须为可变且调用方拥有的缓冲区（栈数组或堆分配），不可传字面量/const；
 *   - 返回原始缓冲起点，调用方按原指针 free。
 */
#include "headers.h"
#include "q_util.h"

char *q_util_str_rtrim(char *s)
{
    if (!s)
        return NULL;

    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1)))
        end--;
    *end = '\0';
    return s;
}
