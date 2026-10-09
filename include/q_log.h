/**
 * @file q_log.h
 * @brief l_1 基础层 日志库（libqlog）对外头文件
 * @platform Windows/Linux/macOS
 * @layer  lib_src/l_1/q_log  → 产出 libqlog
 * @note   本文件自包含：直接引入所需公共基础头（include_stdio.h / include_time.h / include_thd.h），
 *         故外部模块 `#include <l_1/q_log.h>` 可独立编译，无需依赖内部聚合头 headers.h。
 *         api/ 下源文件仍只需 `#include "headers.h"` + `#include "q_log.h"`。
 *
 * 安全约束（依据 AI_SKILL_RULE & 技术方案 V2 §10/§3.2.10）：
 *   - 审计日志仅追加、权限受限（0640），审计管理员只可读不能删
 *   - 严禁记录口令 / OTP / Token 明文；敏感字段统一经 q_log_redact() 脱敏
 *   - 审计记录建议配合 SM3 链式哈希（chain_prev / chain_cur）防篡改
 *
 * 国密边界：Tongsuo 只允许 q_crypto 调用，故本库【不直接计算 SM3 链式哈希】，
 *          审计链哈希由上层 q_crypto / bizd 计算后通过 q_log_audit() 的 hash_* 参数传入。
 */
#ifndef L_1_Q_LOG_H
#define L_1_Q_LOG_H

/* 本文件自包含：直接引入所需公共基础头，便于外部模块独立编译。
   headers.h 仅作 api/ 源文件的聚合入口（不安装到 include/l_N/）。
   注：<stdarg.h> / <stdint.h> 等已由 include_stdio.h 统一提供，无需单独引入。 */
#define _POSIX_C_SOURCE 200809L   /* 启用 fdopen/open/localtime_r 等 POSIX 声明 */
#include "include_stdio.h"
#include "include_time.h"
#include "include_thd.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* 日志级别（数值越小越详细；Q_LOG_OFF 关闭全部输出） */
    typedef enum
    {
        Q_LOG_TRACE = 0,
        Q_LOG_DEBUG,
        Q_LOG_INFO,
        Q_LOG_WARN,
        Q_LOG_ERROR,
        Q_LOG_FATAL,
        Q_LOG_AUDIT, /* 审计专用等级，独立于级别过滤，始终记录 */
        Q_LOG_OFF
    } q_log_level_t;

    /* 句柄结构体（实现细节随头文件公开，支撑下方静态内联辅助函数） */
    struct q_log
    {
        q_log_level_t min_level;
        FILE *file;        /* 文件输出流，NULL 表示仅控制台 */
        char path[1024];   /* 文件路径，用于轮转；空表示无文件 */
        uint64_t max_size; /* 单文件轮转上限，0=不轮转 */
        uint64_t cur_size; /* 当前文件已写入字节数 */
        q_mutex_t lock;    /* 线程安全互斥锁 */
        int console;       /* 是否同时输出到 stderr，默认 1 */
    };
    typedef struct q_log q_log_t;

    /**
     * @brief 打开日志系统
     * @param path       日志文件路径；为 NULL 或 "" 时仅输出到控制台(stderr)
     * @param min_level  最低输出级别（低于该级别的常规日志被丢弃；审计不受限）
     * @param max_size   单文件轮转上限（字节），0 表示不按大小轮转
     * @return           日志句柄，失败返回 NULL
     */
    q_log_t *q_log_open(const char *path, q_log_level_t min_level, uint64_t max_size);

    /** 设置最低输出级别 */
    void q_log_set_level(q_log_t *log, q_log_level_t level);

    /** 允许/禁止同时输出到控制台(stderr)，默认开启 */
    void q_log_set_console(q_log_t *log, int enable);

    /**
     * @brief 核心写日志（线程安全）
     * @param file/line  调用位置（可用下方便捷宏自动填充）
     */
    void q_log_write(q_log_t *log, q_log_level_t level, const char *file, int line, const char *fmt, ...);

    /**
     * @brief 审计记录写入（仅追加，不受 min_level 过滤）
     * @param category   分类，如 "AUTH" / "LOGIN" / "SPA" / "PERM"
     * @param action     动作，如 "login_ok" / "login_fail" / "lock"
     * @param msg        描述；【禁止包含明文口令/OTP/Token】，调用方须先脱敏
     * @param hash_prev_hex 上一条审计链哈希(hex)，可空（首条传 "0"）
     * @param hash_cur_hex  本条审计链哈希(hex)，由上层 q_crypto(SM3) 计算，可空
     */
    void q_log_audit(q_log_t *log, const char *category, const char *action, const char *msg, const char *hash_prev_hex,
                     const char *hash_cur_hex);

    /**
     * @brief 敏感信息脱敏：返回定长掩码副本，调用方负责 free()
     *        用于确保口令 / OTP / Token 绝不落明文（Token 仅记前 8 位哈希）
     */
    char *q_log_redact(const char *p);

    /** 手动触发日志轮转（写满时由 q_log_write 自动调用），返回 0 成功 */
    int q_log_rotate(q_log_t *log);

    /** 刷新所有缓冲 */
    void q_log_flush(q_log_t *log);

    /** 关闭并释放日志句柄 */
    void q_log_close(q_log_t *log);

    /* ================= 静态内联辅助（仅本库使用） ================= */

    static inline const char *q_log_level_name(q_log_level_t l)
    {
        switch (l)
        {
        case Q_LOG_TRACE:
            return "TRACE";
        case Q_LOG_DEBUG:
            return "DEBUG";
        case Q_LOG_INFO:
            return "INFO";
        case Q_LOG_WARN:
            return "WARN";
        case Q_LOG_ERROR:
            return "ERROR";
        case Q_LOG_FATAL:
            return "FATAL";
        case Q_LOG_AUDIT:
            return "AUDIT";
        default:
            return "?";
        }
    }

    /* 以追加模式打开日志文件，权限 0640（非 Windows 下），失败返回 NULL */
    static inline FILE *q_log_file_open(const char *path)
    {
        if (!path || !*path)
            return NULL;
#ifdef Q_SYS_WINDOWS
        int fd = _open(path, _O_WRONLY | _O_CREAT | _O_APPEND, _S_IREAD | _S_IWRITE);
        if (fd < 0)
            return NULL;
        FILE *f = fdopen(fd, "a");
        if (!f)
        {
            _close(fd);
            return NULL;
        }
        return f;
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0640);
    if (fd < 0)
        return NULL;
    FILE *f = fdopen(fd, "a");
    if (!f)
    {
        close(fd);
        return NULL;
    }
    return f;
#endif
    }

    /* 不加锁的轮转：关闭当前文件、重命名为时间戳备份、重新以追加模式打开。
       调用方须已持有 log->lock。 */
    static inline int q_log_rotate_unlocked(q_log_t *log)
    {
        if (!log || !log->file || log->path[0] == '\0')
            return -1;

        fflush(log->file);
        fclose(log->file);
        log->file = NULL;

        time_t t = time(NULL);
        struct tm tm;
#ifdef Q_SYS_WINDOWS
        localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
        char stamp[32];
        strftime(stamp, sizeof(stamp), "%Y%m%d%H%M%S", &tm);

        char dst[2048];
        snprintf(dst, sizeof(dst), "%s.%s", log->path, stamp);
        rename(log->path, dst);

        log->file = q_log_file_open(log->path);
        log->cur_size = 0;
        return (log->file != NULL) ? 0 : -1;
    }

/* 便捷宏：自动填充文件/行号 */
#define QLOG_TRACE(log, ...) q_log_write((log), Q_LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define QLOG_DEBUG(log, ...) q_log_write((log), Q_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define QLOG_INFO(log, ...) q_log_write((log), Q_LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define QLOG_WARN(log, ...) q_log_write((log), Q_LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define QLOG_ERROR(log, ...) q_log_write((log), Q_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define QLOG_FATAL(log, ...) q_log_write((log), Q_LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* L_1_Q_LOG_H */
