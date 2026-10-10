/**
 * @file q_util_ut.c
 * @brief q_util 单元测试小程序（构建后由 make ut 运行）
 */
#include "headers.h"
#include "q_util.h"

#include <string.h>
#include <limits.h>

static int g_fail = 0;

#define CHECK(cond, msg)                              \
    do                                                \
    {                                                 \
        if (!(cond))                                  \
        {                                             \
            printf("FAIL: %s\n", msg);                \
            g_fail++;                                 \
        }                                             \
        else                                          \
        {                                             \
            printf("PASS: %s\n", msg);               \
        }                                             \
    } while (0)

int main(void)
{
    /* ============ hex 往返 ============ */
    const unsigned char raw[] = {0x00, 0x01, 0xab, 0xff, 0x10};
    char *hex = q_util_hex_encode(raw, sizeof(raw));
    CHECK(hex != NULL, "hex_encode");
    CHECK(strcmp(hex, "0001abff10") == 0, "hex_encode value == 0001abff10");
    size_t dlen = 0;
    unsigned char *dec = q_util_hex_decode(hex, &dlen);
    CHECK(dec != NULL && dlen == sizeof(raw) && memcmp(dec, raw, dlen) == 0, "hex roundtrip");
    free(dec);
    free(hex);
    size_t bad = 0;
    CHECK(q_util_hex_decode("xyz", &bad) == NULL, "hex_decode invalid -> NULL");

    /* ============ base64 往返 ============ */
    const char *msg = "Hello, 国密!";
    char *b64 = q_util_b64_encode(msg, strlen(msg));
    CHECK(b64 != NULL, "b64_encode");
    size_t blen = 0;
    unsigned char *bdec = q_util_b64_decode(b64, &blen);
    CHECK(bdec != NULL && blen == strlen(msg) && memcmp(bdec, msg, blen) == 0, "b64 roundtrip");
    free(bdec);
    free(b64);
    char *b2 = q_util_b64_encode("ab", 2);
    CHECK(strcmp(b2, "YWI=") == 0, "b64 'ab' -> YWI=");
    free(b2);

    /* ============ 字符串辅助 ============ */
    CHECK(q_util_str_startswith("hello world", "hello") == 1, "startswith true");
    CHECK(q_util_str_startswith("hello", "world") == 0, "startswith false");
    CHECK(q_util_str_endswith("file.txt", ".txt") == 1, "endswith true");
    CHECK(q_util_str_endswith("file.txt", ".csv") == 0, "endswith false");
    char trim_buf[] = "   \t hello \n  ";
    char *trim = q_util_str_trim(trim_buf);
    CHECK(trim != NULL && strcmp(trim, "hello") == 0, "trim -> hello");
    /* 零分配：trim 即 trim_buf 本身，禁止 free */
    CHECK(q_util_str_trim(NULL) == NULL, "trim NULL -> NULL");
    char in_place[] = "  ab";
    CHECK(strcmp(q_util_str_trim(in_place), "ab") == 0, "trim 原地 -> ab");
    char allws[] = "   ";
    CHECK(strcmp(q_util_str_trim(allws), "") == 0, "trim 全空白 -> 空串");

    /* rtrim：仅去尾部空白 */
    char rtrim_buf[] = "hello \t\n ";
    char *rt = q_util_str_rtrim(rtrim_buf);
    CHECK(rt != NULL && strcmp(rt, "hello") == 0, "rtrim -> hello");
    char rtrim_none[] = "hello";
    CHECK(strcmp(q_util_str_rtrim(rtrim_none), "hello") == 0, "rtrim 无尾部空白不变");
    char rtrim_lead[] = "  hello";
    CHECK(strcmp(q_util_str_rtrim(rtrim_lead), "  hello") == 0, "rtrim 保留前导空白");
    char rtrim_all[] = "   ";
    CHECK(strcmp(q_util_str_rtrim(rtrim_all), "") == 0, "rtrim 全空白 -> 空串");
    CHECK(q_util_str_rtrim(NULL) == NULL, "rtrim NULL -> NULL");

    /* ============ 时间字符串比较 ============ */
    CHECK(q_util_time_cmp("20240101120000", "20240101120001") < 0, "time_cmp a<b");
    CHECK(q_util_time_cmp("20240101120001", "20240101120000") > 0, "time_cmp a>b");
    CHECK(q_util_time_cmp("20240101120000", "20240101120000") == 0, "time_cmp 相等");
    CHECK(q_util_time_cmp("20231231000000", "20240101000000") < 0, "time_cmp 跨年 a<b");
    CHECK(q_util_time_cmp("20240229000000", "20240228000000") > 0, "time_cmp 2/29 晚于 2/28");
    CHECK(q_util_time_cmp(NULL, "20240101120000") == INT_MIN, "time_cmp NULL -> INT_MIN");
    CHECK(q_util_time_cmp("2024010112000", "20240101120000") == INT_MIN, "time_cmp 长度不足 -> INT_MIN");
    CHECK(q_util_time_cmp("2024-01-01-0000", "20240101120000") == INT_MIN, "time_cmp 含非数字 -> INT_MIN");

    /* 非法日历值校验 */
    CHECK(q_util_time_cmp("20251301000000", "20240101120000") == INT_MIN, "time_cmp 13月 -> INT_MIN");
    CHECK(q_util_time_cmp("20250230000000", "20240101120000") == INT_MIN, "time_cmp 2/30 -> INT_MIN");
    CHECK(q_util_time_cmp("20250229000000", "20240101120000") == INT_MIN, "time_cmp 2025非闰年2/29 -> INT_MIN");
    CHECK(q_util_time_cmp("20250101240000", "20240101120000") == INT_MIN, "time_cmp 24时 -> INT_MIN");
    CHECK(q_util_time_cmp("20250101122460", "20240101120000") == INT_MIN, "time_cmp 60秒 -> INT_MIN");
    /* 闰年 2024-02-29 合法，仍可正常比较 */
    CHECK(q_util_time_cmp("20240229000000", "20240228000000") > 0, "time_cmp 闰年2/29 合法且晚于2/28");

    /* ============ 时间格式化 ============ */
    char tb[32];
    size_t n = q_util_time_format(1000000000, tb, sizeof(tb));
    CHECK(n == 19 && tb[4] == '-' && tb[7] == '-' && tb[10] == ' ' &&
          tb[13] == ':' && tb[16] == ':', "time_format shape YYYY-MM-DD HH:MM:SS");
    CHECK(q_util_time_format(0, NULL, 0) == 0, "time_format null buf -> 0");

    if (g_fail == 0)
        printf("\nALL PASS\n");
    else
        printf("\n%d FAILED\n", g_fail);

    return g_fail == 0 ? 0 : 1;
}
