/**
 * @file q_log_open.c
 * @brief q_log_open —— 打开日志系统
 */
#include "headers.h"
#include "q_log.h"

q_log_t *q_log_open(const char *path, q_log_level_t min_level, uint64_t max_size)
{
    q_log_t *log = (q_log_t *)calloc(1, sizeof(q_log_t));
    if (!log)
        return NULL;

    log->min_level = (min_level < Q_LOG_OFF) ? min_level : Q_LOG_OFF;
    log->file = NULL;
    log->max_size = max_size;
    log->cur_size = 0;
    log->console = 1;
    memset(log->path, 0, sizeof(log->path));
    (void)pthread_mutex_init(&log->lock, NULL);

    if (path && *path)
    {
        strncpy(log->path, path, sizeof(log->path) - 1);
        log->file = q_log_file_open(log->path);
        if (!log->file)
        {
            log->path[0] = '\0'; /* 文件打开失败，回退仅控制台 */
        }
        else
        {
            fseek(log->file, 0, SEEK_END);
            long sz = ftell(log->file);
            if (sz > 0)
                log->cur_size = (uint64_t)sz;
        }
    }
    return log;
}
