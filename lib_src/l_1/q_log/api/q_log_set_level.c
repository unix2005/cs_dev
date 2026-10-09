/**
 * @file q_log_set_level.c
 * @brief q_log_set_level —— 设置最低输出级别
 */
#include "headers.h"
#include "q_log.h"

void q_log_set_level(q_log_t *log, q_log_level_t level)
{
    if (!log) return;
    (void)pthread_mutex_lock(&log->lock);
    log->min_level = (level < Q_LOG_OFF) ? level : Q_LOG_OFF;
    (void)pthread_mutex_unlock(&log->lock);
}
