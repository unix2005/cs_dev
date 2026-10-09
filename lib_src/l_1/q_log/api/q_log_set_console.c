/**
 * @file q_log_set_console.c
 * @brief q_log_set_console —— 允许/禁止同时输出到控制台
 */
#include "headers.h"
#include "q_log.h"

void q_log_set_console(q_log_t *log, int enable)
{
    if (!log)
        return;
    (void)pthread_mutex_lock(&log->lock);
    log->console = enable ? 1 : 0;
    (void)pthread_mutex_unlock(&log->lock);
}
