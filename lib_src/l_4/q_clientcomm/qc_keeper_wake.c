/**
 * @file qc_keeper_wake.c
 * @brief M4 会话治理：唤醒 keeper 线程立即动作
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 用途：
 *   - UI 从"断网"恢复到"有网"（系统网络变化事件）时立即重连，不必等满一个退避周期；
 *   - OFFLINE 状态下用户点了"重试"：本函数会把 attempts 清零，重新给一轮机会。
 */
#include "qc_keeper.h"

int qc_keeper_wake(qc_session_t *s)
{
    int on;

    if (!s)
        return -1;

    qc_mutex_lock(&s->st_lock);
    on = s->keeper_on;
    if (on)
        s->wake = 1;
    qc_mutex_unlock(&s->st_lock);

    return on ? 0 : -1;
}
