/**
 * @file q_util_time_cmp.c
 * @brief q_util_time_cmp —— 比较 yyyymmddhhmmss（24h）格式时间字符串
 *
 * 格式为定宽 14 位、零填充的数值串，按位字典序即时间先后序；但在字典序比较前
 * 先做合法性校验，拒绝非法日历值（如 13 月、2 月 30 日、24 时、60 秒等）。
 *
 * 返回 strcmp 语义：
 *   < 0  : a 早于 b
 *   = 0  : 相等
 *   > 0  : a 晚于 b
 * 入参非法（NULL / 长度非 14 / 含非数字 / 非合法日历时间）一律返回 INT_MIN（错误哨兵，
 * 与任何合法比较结果 [-1,1] 不冲突）。
 */
#include "headers.h"
#include "q_util.h"


/* 校验 14 位纯数字串是否为合法日历时间（m_1/q_util 内部辅助，不导出） */
static int time_str_valid(const char *s)
{
    int y  = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    int mo = (s[4] - '0') * 10 + (s[5] - '0');
    int d  = (s[6] - '0') * 10 + (s[7] - '0');
    int h  = (s[8] - '0') * 10 + (s[9] - '0');
    int mi = (s[10] - '0') * 10 + (s[11] - '0');
    int se = (s[12] - '0') * 10 + (s[13] - '0');

    if (mo < 1 || mo > 12) return 0;
    if (d  < 1 || d  > 31) return 0;
    if (h  < 0 || h  > 23) return 0;
    if (mi < 0 || mi > 59) return 0;
    if (se < 0 || se > 59) return 0;

    /* 每月天数，2 月按闰年修正 */
    static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maxd = days[mo - 1];
    if (mo == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0))
        maxd = 29;
    if (d > maxd) return 0;

    return 1;
}

int q_util_time_cmp(const char *a, const char *b)
{
    if (!a || !b)
        return INT_MIN;
    if (strlen(a) != 14 || strlen(b) != 14)
        return INT_MIN;
    for (int i = 0; i < 14; i++)
    {
        if (!isdigit((unsigned char)a[i]) || !isdigit((unsigned char)b[i]))
            return INT_MIN;
    }
    if (!time_str_valid(a) || !time_str_valid(b))
        return INT_MIN;
    return strcmp(a, b);
}
