/**
 * @file qc_keeper_start.c
 * @brief M4 会话治理：启动 keeper 线程
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 前置：qc_login 已成功（未登录时心跳没有意义，返回 -1）。
 * 幂等：已启动时再次调用返回 0，不会起第二个线程。
 * 失败：线程创建失败返回 -2 —— 此时门面（qc_request/qc_biz_query）仍完全可用，
 *       只是失去自动心跳/重连/重登，属于"降级但不致命"。
 */
#include "qc_keeper.h"

int qc_keeper_start(qc_session_t *s, const qc_keeper_cfg_t *cfg)
{
    uint32_t interval_ms;
    int      already;

    if (!s || !s->authed)
        return -1;
    if (!s->io_inited || !s->st_inited)
        return -1;

    qc_mutex_lock(&s->st_lock);
    already = s->keeper_on;
    if (!already)
    {
        memset(&s->cfg, 0, sizeof(s->cfg));
        if (cfg)
            memcpy(&s->cfg, cfg, sizeof(s->cfg));

        interval_ms = (s->cfg.hb_interval_sec ? s->cfg.hb_interval_sec : 60u) * 1000u;

        s->state         = QC_STATE_ONLINE;
        s->attempts      = 0u;
        s->hb_sent       = 0u;
        s->hb_failed     = 0u;
        s->reconnects    = 0u;
        s->relogins      = 0u;
        s->last_ok_ms    = qc_now_ms();
        s->next_hb_ms    = s->last_ok_ms + interval_ms;
        s->next_retry_ms = 0u;
        s->detail[0]     = '\0';
        s->thread_stop   = 0;
        s->wake          = 0;
        s->keeper_on     = 1;               /* 先置位：避免启线程与 stop 竞态 */
    }
    qc_mutex_unlock(&s->st_lock);

    if (already)
        return 0;                           /* 幂等：已启动就不再起第二个线程 */

    if (qc_thread_start(&s->thread, (qc_thread_fn_t)qc_keeper_main, s) != 0)
    {
        qc_mutex_lock(&s->st_lock);
        s->keeper_on = 0;
        s->state     = QC_STATE_IDLE;
        qc_mutex_unlock(&s->st_lock);
        return -2;
    }
    return 0;
}
