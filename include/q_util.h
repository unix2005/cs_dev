/**
 * @file q_util.h
 * @brief l_1 基础层 通用工具库（libqutil）对外头文件
 * @platform Windows/Linux/macOS
 * @layer  lib_src/l_1/q_util  ->  产出 libqutil
 * @note   本文件自包含：仅依赖公共基础头 include_stdio.h 与标准 <time.h>，
 *         故外部模块 `#include <q_util.h>` 可独立编译，无需依赖内部聚合头 headers.h。
 *         api/ 下源文件仍只需 `#include "headers.h"` + `#include "q_util.h"`。
 *
 * 提供与项目安全/网络场景相关的通用工具（刻意不重复 include_stdio.h 的宏）：
 *   - 十六进制 / Base64(RFC4648) 编解码（返回调用方须 free 的缓冲区）
 *   - 字符串辅助：startswith / endswith / trim
 *   - 时间格式化：time_t -> "YYYY-MM-DD HH:MM:SS"
 */
#ifndef L_1_Q_UTIL_H
#define L_1_Q_UTIL_H

#include "include_stdio.h"
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* 十六进制编解码（返回调用方须 free 的缓冲区） */
    char *q_util_hex_encode(const void *bin, size_t len);
    void *q_util_hex_decode(const char *hex, size_t *out_len);

    /* Base64 编解码（RFC4648，含 '=' 填充；返回调用方须 free 的缓冲区） */
    char *q_util_b64_encode(const void *bin, size_t len);
    void *q_util_b64_decode(const char *b64, size_t *out_len);

    /* 字符串辅助 */
    int q_util_str_startswith(const char *s, const char *prefix);
    int q_util_str_endswith(const char *s, const char *suffix);
    /* 原地去除首尾空白（不分配内存，直接改写入参）；s 须为可变且调用方拥有的缓冲区，
       不可传字符串字面量/const；返回原始缓冲起点，调用方按原指针 free。 */
    char *q_util_str_trim(char *s);
    /* 原地去除尾部空白（保留前导与中间空白）；约束同上。 */
    char *q_util_str_rtrim(char *s);

    /* 时间格式化：写入 "YYYY-MM-DD HH:MM:SS" 到 buf，返回写入字符数（不含 NUL）；buf 空返回 0 */
    size_t q_util_time_format(time_t t, char *buf, size_t buf_size);

    /* 时间字符串比较：格式 yyyymmddhhmmss（24h）。返回 strcmp 语义（<0/0/>0）；
       入参非法（NULL/长度非14/含非数字/非合法日历时间）一律返回 INT_MIN。 */
    int q_util_time_cmp(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif /* L_1_Q_UTIL_H */
