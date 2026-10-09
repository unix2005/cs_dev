/**
 * @file q_log_rotate.c
 * @brief q_log_rotate —— 手动触发日志轮转
 */
#include "headers.h"
#include "q_log.h"

int q_log_rotate(q_log_t *log)
{
    if (!log)
        return -1;
    (void)pthread_mutex_lock(&log->lock);
    int rc = q_log_rotate_unlocked(log);
    (void)pthread_mutex_unlock(&log->lock);
    return rc;
}
