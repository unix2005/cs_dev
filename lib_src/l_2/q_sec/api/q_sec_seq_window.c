/**
 * @file q_sec_seq_window.c
 * @brief q_sec_seq_window —— seq 滑动窗口防重放
 *
 * 依据 AI_SKILL_RULE §3.2.7：窗口为仅前向 [last+1, last+span]（span 建议 64），
 * 采用"高水位 last + 跨度"模型；禁止"缓存 nonce 若干分钟"方案。
 *   - seq <= last           : 重放或已过期 -> 拒绝（防重放核心）
 *   - seq > last + span     : 超前超出窗口 -> 拒绝（防止序号被恶意大跳）
 *   - 其余（last < seq <= last+span）: 接受并推进 last
 * 该模型下窗口内不会重复接受同一 seq（last 单调增），无需位图。
 */
#include "headers.h"
#include "q_sec.h"

struct q_sec_seq_window
{
    uint64_t last;     /* 已接受的最高 seq（高水位） */
    uint64_t span;     /* 裁剪后的窗口跨度 [1,64] */
    int      init;
};

q_sec_seq_window_t *q_sec_seq_window_create(uint64_t span)
{
    q_sec_seq_window_t *w = (q_sec_seq_window_t *)malloc(sizeof(*w));
    if (!w)
        return NULL;
    w->last = 0;
    w->span = (span < 1) ? 1 : (span > 64 ? 64 : span);
    w->init = 0;
    return w;
}

void q_sec_seq_window_destroy(q_sec_seq_window_t *w)
{
    free(w);
}

int q_sec_seq_window_accept(q_sec_seq_window_t *w, uint64_t seq)
{
    if (!w)
        return -1;

    if (!w->init)
    {
        w->init = 1;
        w->last = seq;
        return 1;
    }
    if (seq <= w->last)
        return 0;                       /* 重放或已过期 */
    if (seq > w->last + w->span)
        return 0;                       /* 超前超出窗口 */
    w->last = seq;
    return 1;
}
