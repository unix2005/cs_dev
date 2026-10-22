/**
 * @file qc_keeper_main.c
 * @brief M4 会话治理：keeper 线程主循环（心跳 / 退避重连 / Token 失效重登）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 状态机：
 *   ONLINE        --(心跳 ok)--> ONLINE
 *                 --(心跳 result=3)--> REAUTHING   : 原连接上重发 SALT+LOGIN
 *                 --(心跳失败/断连)--> RECONNECTING: 退避后 rebuild（含重新敲门）
 *   REAUTHING     --(重登 ok)--> ONLINE
 *                 --(重登失败)--> RECONNECTING     : 退避后走完整 rebuild
 *   RECONNECTING  --(rebuild ok)--> ONLINE
 *                 --(失败且未超限)--> RECONNECTING （退避递增，带抖动）
 *                 --(超 max_retry / 缺凭据)--> OFFLINE：停止自动重试，等 UI 介入
 *   OFFLINE       --(qc_keeper_wake)--> 清零 attempts，重新进入 RECONNECTING
 *
 * 心跳与业务互斥：心跳取 s->io_lock（与 qc_keeper_lock() 同一把锁），
 * 因此业务线程发请求时心跳会等待，反之亦然 —— lockstep 协议不被并发破坏。
 *
 * 停止：thread_stop 置位后，循环最多再睡一个 50ms 分片就退出；
 *       睡眠切成 50ms 小片是为了让 qc_keeper_stop() 不必等满一个 tick 才返回。
 */
#include "qc_keeper.h"

#include "include_ipc_proto.h"   /* Q_IPC_R_UNAUTHORIZED */

/* 分片睡眠：便于 stop 快速生效（每段 50ms 检查一次停止标志） */
static void qc_sleep_slice(struct qc_session *s, uint32_t ms)
{
    uint32_t left = ms;
    while (left > 0u && !s->thread_stop)
    {
        uint32_t seg = (left > 50u) ? 50u : left;
        qc_sleep_ms(seg);
        left -= seg;
    }
}

QC_THREAD_RET qc_keeper_main(QC_THREAD_ARG arg)
{
    struct qc_session *s = (struct qc_session *)arg;
    uint32_t           tick;
    uint32_t           interval_ms;

    if (!s)
        return QC_THREAD_RET_VAL;

    tick        = s->cfg.tick_ms ? s->cfg.tick_ms : 500u;
    interval_ms = (s->cfg.hb_interval_sec ? s->cfg.hb_interval_sec : 60u) * 1000u;

    qc_keeper_set_state(s, QC_STATE_ONLINE, 0, "会话已建立，心跳已启动");

    while (!s->thread_stop)
    {
        uint64_t now   = qc_now_ms();
        int      state = 0;
        int      do_hb = 0;
        int      do_retry = 0;

        qc_mutex_lock(&s->st_lock);
        state = s->state;
        if (s->wake)
        {
            s->wake = 0;
            /* OFFLINE / IDLE 被唤醒 = UI 想再给一轮机会（用户点了重试 / 重新登录）：
               清零失败计数并转 RECONNECTING，否则下面的分支都不会命中。 */
            if (state == QC_STATE_OFFLINE || state == QC_STATE_IDLE)
            {
                s->attempts      = 0u;
                s->state         = QC_STATE_RECONNECTING;
                s->next_retry_ms = now;
                state            = QC_STATE_RECONNECTING;
            }
            do_hb    = 1;
            do_retry = 1;
        }
        if (state == QC_STATE_ONLINE && now + 20u >= s->next_hb_ms)
            do_hb = 1;
        if ((state == QC_STATE_RECONNECTING || state == QC_STATE_REAUTHING) &&
            now + 20u >= s->next_retry_ms)
            do_retry = 1;
        qc_mutex_unlock(&s->st_lock);

        if (state == QC_STATE_ONLINE && do_hb)
        {
            int rc;

            qc_mutex_lock(&s->io_lock);
            if (s->fault & QC_FAULT_HB_FAIL)
                rc = -1;                                  /* 注入：连接断 */
            else if (s->fault & QC_FAULT_HB_RC3)
                rc = (int)Q_IPC_R_UNAUTHORIZED;           /* 注入：Token 过期 */
            else
                rc = qc_heartbeat(s);
            qc_mutex_unlock(&s->io_lock);

            qc_mutex_lock(&s->st_lock);
            s->hb_sent++;
            if (rc == 0)
            {
                s->last_ok_ms = now;
                s->attempts   = 0u;
                s->next_hb_ms = now + interval_ms;
                qc_mutex_unlock(&s->st_lock);
                if (state != QC_STATE_ONLINE)
                    qc_keeper_set_state(s, QC_STATE_ONLINE, 0, "心跳恢复正常");
            }
            else
            {
                s->hb_failed++;
                if (rc == (int)Q_IPC_R_UNAUTHORIZED)
                    s->next_retry_ms = now;               /* Token 过期：立即重登，不退避 */
                else
                {
                    s->attempts++;
                    s->next_retry_ms = now + qc_keeper_backoff_ms(s, s->attempts);
                }
                qc_mutex_unlock(&s->st_lock);

                if (rc == (int)Q_IPC_R_UNAUTHORIZED)
                    qc_keeper_set_state(s, QC_STATE_REAUTHING, rc, "Token 失效，正在重新登录");
                else
                    qc_keeper_set_state(s, QC_STATE_RECONNECTING, rc, "心跳失败，准备重连");
            }
        }
        else if (do_retry && (state == QC_STATE_RECONNECTING || state == QC_STATE_REAUTHING))
        {
            int rc;
            int was_reauth = (state == QC_STATE_REAUTHING);

            qc_mutex_lock(&s->io_lock);
            rc = was_reauth ? qc_keeper_reauth(s) : qc_keeper_rebuild(s);
            qc_mutex_unlock(&s->io_lock);

            if (rc == 0)
            {
                qc_mutex_lock(&s->st_lock);
                s->attempts   = 0u;
                s->last_ok_ms = now;
                s->next_hb_ms = now + interval_ms;
                if (was_reauth)
                    s->relogins++;
                else
                    s->reconnects++;
                qc_mutex_unlock(&s->st_lock);

                qc_keeper_set_state(s, QC_STATE_ONLINE, 0,
                                    was_reauth ? "已重新登录（Token 续期）" : "已重建连接并登录");
            }
            else
            {
                int offline = 0;

                qc_mutex_lock(&s->st_lock);
                s->attempts++;
                s->next_retry_ms = now + qc_keeper_backoff_ms(s, s->attempts);
                if (rc == -2 || (s->cfg.max_retry > 0u && s->attempts >= s->cfg.max_retry))
                    offline = 1;
                qc_mutex_unlock(&s->st_lock);

                if (offline)
                    qc_keeper_set_state(s, QC_STATE_OFFLINE, rc,
                                        rc == -2 ? "缺少登录凭据，需用户重新登录"
                                                 : "重连次数超限，已停止自动重连");
                else
                    qc_keeper_set_state(s, QC_STATE_RECONNECTING, rc,
                                        was_reauth ? "重新登录失败，改为重建连接" : "重连失败，等待退避后重试");
            }
        }

        qc_sleep_slice(s, tick);
    }

    return QC_THREAD_RET_VAL;
}
