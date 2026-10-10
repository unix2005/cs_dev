/**
 * @file q_util_str_trim.c
 * @brief q_util_str_trim —— 原地去除首尾空白（不分配内存，直接修改入参）
 *
 * 设计要点：
 *   - 零分配、原地修改：直接对调用方提供的可变缓冲区裁剪，不申请新内存，
 *     彻底规避 malloc/free 配对导致的内存问题，调用方也无需准备额外 dst 缓冲。
 *   - 入参 s 必须是【可变、且调用方拥有】的缓冲区（栈数组或堆分配均可）；
 *     禁止传入字符串字面量或 const 缓冲区（会触发未定义行为）。
 *   - 返回值为【原始缓冲区起点】（而非跳过前导空白后的偏移），便于调用方按原指针 free。
 */
#include "headers.h"
#include "q_util.h"

char *q_util_str_trim(char *s)
{
    if (!s)
        return NULL;

    char *base = s;                 /* 原始缓冲起点，最终返回以保可释放 */

    /* 跳过前导空白 */
    while (*s && isspace((unsigned char)*s))
        s++;

    /* 整体左移到缓冲起点（覆盖前导空白） */
    char *dst = base;
    const char *src = s;
    while (*src)
        *dst++ = *src++;

    /* 回退去除尾部空白并补结尾 NUL */
    while (dst > base && isspace((unsigned char)*(dst - 1)))
        dst--;
    *dst = '\0';

    return base;
}
