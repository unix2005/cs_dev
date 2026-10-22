/**
 * @file qc_keeper_note_result.c
 * @brief M4 会话治理：把业务请求的结果码反馈给 keeper（3 = 触发自动重登）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 为什么需要它：Token TTL(1800s) 到期时，心跳**仍能成功**（心跳不校验 Token 过期？
 * 取决于服务端实现），真正暴露问题的是业务请求返回 Q_IPC_R_UNAUTHORIZED(3)。
 * 若只有心跳能触发重登，就会出现"UI 偶发报 3、要 30 分钟后才好"的怪象。
 * 所以业务结果必须回灌给会话治理。
 *
 * 调用约定：**必须在 gate 内调用**（qc_keeper_lock/unlock 之间）。
 * 本函数内部直接同步重登（不再取 io_lock，故不会自锁），返回重登的结果：
 *   0  = 无需重登（rc 不是 3）或重登成功 —— 调用方可原样重试一次业务请求
 *   >0 = 重登后仍返回的结果码（如口令错误 3）
 *   <0 = 重登失败：-1 断连/参数非法，-2 UI 无法提供凭据（应提示用户重新登录）
 */
#include "qc_keeper.h"

#include "include_ipc_proto.h"   /* Q_IPC_R_UNAUTHORIZED */

int qc_keeper_note_result(qc_session_t *s, int rc)
{
    int r;

    if (!s)
        return -1;
    if (rc != (int)Q_IPC_R_UNAUTHORIZED)
        return 0;                       /* 不是 Token 问题，交给调用方按普通错误处理 */

    if (!s->cfg.on_relogin || !s->rc.user[0])
        return -2;                      /* 没有凭据来源，无法自动重登 */

    qc_mutex_lock(&s->st_lock);
    s->state         = QC_STATE_REAUTHING;
    s->next_retry_ms = qc_now_ms();
    qc_mutex_unlock(&s->st_lock);

    r = qc_keeper_reauth(s);

    qc_mutex_lock(&s->st_lock);
    if (r == 0)
    {
        s->relogins++;
        s->last_ok_ms    = qc_now_ms();
        s->attempts      = 0u;
        s->next_hb_ms    = s->last_ok_ms +
                           (s->cfg.hb_interval_sec ? s->cfg.hb_interval_sec : 60u) * 1000u;
    }
    else
        s->attempts++;
    qc_mutex_unlock(&s->st_lock);

    qc_keeper_set_state(s, (r == 0) ? QC_STATE_ONLINE : QC_STATE_RECONNECTING, r,
                        (r == 0) ? "业务请求触发重新登录成功"
                                 : "业务请求触发重新登录失败");
    return r;
}
