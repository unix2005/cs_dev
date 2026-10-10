/**
 * @file q_util_time_format.c
 * @brief q_util_time_format —— time_t 格式化为 "YYYY-MM-DD HH:MM:SS"
 */
#include "headers.h"
#include "q_util.h"

size_t q_util_time_format(time_t t, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0)
        return 0;

    struct tm tm_buf;
    if (localtime_r(&t, &tm_buf) == NULL)
        return 0;

    int n = snprintf(buf, buf_size, "%04d-%02d-%02d %02d:%02d:%02d",
                     tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                     tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    if (n < 0)
        return 0;
    return (size_t)n;
}
