/**
 * @file qc_keeper_state.c
 * @brief M4 会话治理：读取会话状态与统计（供 UI 状态栏 / 排障）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 全部字段在 st_lock 下快照拷贝 —— 调用方拿到的是一致的一瞬间，
 * 不会出现"state 是 ONLINE 但 last_ok_ms 还是旧值"这种半新半旧的组合。
 */
#include "qc_keeper.h"

int qc_keeper_state(qc_session_t *s, qc_keeper_stat_t *out)
{
    if (!s)
        return -1;

    qc_mutex_lock(&s->st_lock);
    if (out)
    {
        memset(out, 0, sizeof(*out));
        out->state        = s->state;
        out->attempts     = s->attempts;
        out->hb_sent      = s->hb_sent;
        out->hb_failed    = s->hb_failed;
        out->reconnects   = s->reconnects;
        out->relogins     = s->relogins;
        out->last_ok_ms   = s->last_ok_ms;
        out->next_hb_ms   = s->next_hb_ms;
        out->next_retry_ms = s->next_retry_ms;
        memcpy(out->detail, s->detail, sizeof(out->detail));
    }
    qc_mutex_unlock(&s->st_lock);
    return 0;
}
