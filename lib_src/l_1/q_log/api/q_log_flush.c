/**
 * @file q_log_flush.c
 * @brief q_log_flush —— 刷新所有缓冲
 */
#include "headers.h"
#include "q_log.h"

void q_log_flush(q_log_t *log)
{
    if (!log)
        return;
    (void)pthread_mutex_lock(&log->lock);
    if (log->file)
        fflush(log->file);
    if (log->console)
        fflush(stderr);
    (void)pthread_mutex_unlock(&log->lock);
}
