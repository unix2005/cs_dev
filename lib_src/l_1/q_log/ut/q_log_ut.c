/**
 * @file q_log_ut.c
 * @brief q_log 库单元测试 / 冒烟小程序（ut/）
 * @note  编译：见模块 Makefile 的 `make ut` 目标（自动链接 libqlog.a）
 *        本程序覆盖：级别过滤、文件/控制台双写、敏感脱敏、审计链式哈希、轮转、关闭。
 */
#include "headers.h"
#include "q_log.h"
#include <stdlib.h>

#define QLOG_UT_FILE "/tmp/q_log_ut.log"
#define QLOG_UT_FILE2 "/tmp/q_log_ut2.log"

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  [FAIL] %s\n", msg); g_fail++; } \
    else { printf("  [ OK ] %s\n", msg); } \
} while (0)

static void dump_file(const char *path)
{
    char line[4096];
    FILE *f = fopen(path, "r");
    if (!f) { printf("  (无法打开 %s)\n", path); return; }
    while (fgets(line, sizeof(line), f)) fputs(line, stdout);
    fclose(f);
}

int main(void)
{
    printf("=== q_log 单元测试 ===\n");

    /* 1. 打开（文件 + 控制台），级别 INFO */
    q_log_t *log = q_log_open(QLOG_UT_FILE, Q_LOG_INFO, 0);
    CHECK(log != NULL, "q_log_open 返回非空句柄");

    /* 2. 级别过滤：DEBUG 应被丢弃，INFO/WARN 应保留 */
    QLOG_DEBUG(log, "should-be-filtered");
    QLOG_INFO(log, "info-shown value=%d", 7);
    QLOG_WARN(log, "warn-shown");

    /* 3. 敏感脱敏：口令不得落明文 */
    char *pw = q_log_redact("s3cr3t");
    CHECK(pw != NULL, "q_log_redact 返回非空");
    CHECK(strcmp(pw, "****") == 0, "q_log_redact 输出固定掩码");
    QLOG_WARN(log, "login user=admin pw=%s", pw);
    free(pw);

    /* 4. 审计记录（含链式哈希占位） */
    q_log_audit(log, "AUTH", "login_ok", "user=admin", "0", "deadbeefcafe");
    q_log_flush(log);

    printf("--- 日志文件内容 ---\n");
    dump_file(QLOG_UT_FILE);

    /* 5. 文件内容校验：不应出现明文口令，应出现审计行 */
    {
        char buf[4096]; int found_audit = 0, leak_pwd = 0;
        FILE *f = fopen(QLOG_UT_FILE, "r");
        while (fgets(buf, sizeof(buf), f)) {
            if (strstr(buf, "AUDIT")) found_audit++;
            if (strstr(buf, "s3cr3t")) leak_pwd++;
        }
        fclose(f);
        CHECK(found_audit >= 1, "审计记录已写入");
        CHECK(leak_pwd == 0, "明文口令未泄露");
    }

    /* 6. 轮转：设置极小上限触发自动轮转，并手动轮转 */
    q_log_t *log2 = q_log_open(QLOG_UT_FILE2, Q_LOG_INFO, 50);
    for (int i = 0; i < 5; i++) QLOG_INFO(log2, "rotate-trigger-line-%d padding-padding", i);
    int rc = q_log_rotate(log2);
    CHECK(rc == 0, "q_log_rotate 手动轮转成功");
    QLOG_INFO(log2, "after-manual-rotate");
    q_log_flush(log2);
    q_log_close(log2);

    /* 7. 关闭 */
    q_log_close(log);
    printf("=== 结果：%s ===\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURE");
    return g_fail == 0 ? 0 : 1;
}
