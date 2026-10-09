/**
 * @file q_log_write.c
 * @brief q_log_write —— 核心写日志（线程安全、级别过滤、自动轮转）
 */
#include "headers.h"
#include "q_log.h"

void q_log_write(q_log_t *log, q_log_level_t level, const char *file, int line, const char *fmt, ...)
{
    if (!log)
        return;
    if (level < 0 || level >= Q_LOG_OFF)
        return;
    if (level < log->min_level)
        return; /* 低于阈值丢弃 */

    char buf[4096];

    /* 时间戳 YYYY-MM-DD HH:MM:SS.mmm */
    time_t t = time(NULL);
    struct tm tm;
#ifdef Q_SYS_WINDOWS
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char date[32];
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &tm);
    long long ms = (long long)(q_get_ms_timestamp() % 1000);

    int off = snprintf(buf, sizeof(buf), "%s.%03lld [%s] [%s:%d] ", date, ms, q_log_level_name(level),
                       (file ? file : "?"), line);
    if (off < 0)
        off = 0;
    if ((size_t)off >= sizeof(buf))
        off = (int)(sizeof(buf) - 1);

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + off, sizeof(buf) - (size_t)off, (fmt ? fmt : ""), ap);
    va_end(ap);

    size_t len = strlen(buf);
    if (len == 0 || buf[len - 1] != '\n')
    { /* 确保行尾换行 */
        if (len < sizeof(buf) - 1)
        {
            buf[len] = '\n';
            buf[len + 1] = '\0';
            len++;
        }
    }

    (void)pthread_mutex_lock(&log->lock);
    if (log->file)
    {
        fwrite(buf, 1, len, log->file);
        fflush(log->file);
    }
    if (log->console)
    {
        fwrite(buf, 1, len, stderr);
    }
    log->cur_size += (uint64_t)len;
    if (log->max_size && log->cur_size > log->max_size)
    {
        q_log_rotate_unlocked(log); /* 写满自动轮转（已持锁） */
    }
    (void)pthread_mutex_unlock(&log->lock);
}
