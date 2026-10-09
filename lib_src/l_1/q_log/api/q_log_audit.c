/**
 * @file q_log_audit.c
 * @brief q_log_audit —— 审计记录写入（仅追加，不受级别过滤）
 * @note  审计内容依据技术方案 V2 §10：连接/SPA/登录/权限变更/异常包/限流拉黑等，
 *        留存 ≥180 天，SM3 链式哈希防篡改；严禁明文口令/OTP/Token。
 *        链式哈希由上层 q_crypto(SM3) 计算后通过 hash_* 传入，本库不碰 Tongsuo。
 */
#include "headers.h"
#include "q_log.h"

void q_log_audit(q_log_t *log, const char *category, const char *action, const char *msg, const char *hash_prev_hex,
                 const char *hash_cur_hex)
{
    if (!log)
        return;
    if (!category)
        category = "-";
    if (!action)
        action = "-";
    if (!msg)
        msg = ""; /* 调用方须已脱敏 */
    if (!hash_prev_hex)
        hash_prev_hex = "0";
    if (!hash_cur_hex)
        hash_cur_hex = "0";

    time_t t = time(NULL);
    struct tm tm;
#ifdef Q_SYS_WINDOWS
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char date[32];
    strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &tm);

    char buf[2048];
    int n = snprintf(buf, sizeof(buf), "%s AUDIT category=%s action=%s msg=%s chain_prev=%s chain_cur=%s\n", date,
                     category, action, msg, hash_prev_hex, hash_cur_hex);
    if (n < 0)
        return;
    size_t len = strlen(buf);

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
        q_log_rotate_unlocked(log);
    }
    (void)pthread_mutex_unlock(&log->lock);
}
