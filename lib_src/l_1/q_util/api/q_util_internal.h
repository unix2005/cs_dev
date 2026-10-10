/**
 * @file q_util_internal.h
 * @brief q_util 内部表与辅助（仅被 headers.h 引入，外部不可见）
 */
#ifndef Q_UTIL_INTERNAL_H
#define Q_UTIL_INTERNAL_H

static const char Q_UTIL_B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char Q_UTIL_HEX[] = "0123456789abcdef";

static inline int q_util_b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static inline int q_util_hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

#endif /* Q_UTIL_INTERNAL_H */
