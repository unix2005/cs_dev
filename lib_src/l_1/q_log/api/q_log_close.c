/**
 * @file q_log_close.c
 * @brief q_log_close —— 关闭并释放日志句柄
 */
#include "headers.h"
#include "q_log.h"

void q_log_close(q_log_t *log)
{
    if (!log)
        return;
    (void)pthread_mutex_lock(&log->lock);
    if (log->file)
    {
        fflush(log->file);
        fclose(log->file);
        log->file = NULL;
    }
    (void)pthread_mutex_unlock(&log->lock);
    (void)pthread_mutex_destroy(&log->lock);
    free(log);
}
