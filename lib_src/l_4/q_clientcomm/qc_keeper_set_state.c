/**
 * @file qc_keeper_set_state.c
 * @brief M4 会话治理：设置会话状态并记录最近事件描述
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 要点：状态回调必须在**锁外**调用。
 *       回调是 UI 提供的（C# 会编组到 UI 线程，GTK 会 g_idle_add），
 *       回调里很可能有 UI 反过来再调 qc_keeper_state() 的动作 ——
 *       若在持 st_lock 时回调，就会自锁死。这是跨层回调最经典的坑。
 */
#include "qc_keeper.h"

#include <stdio.h>

void qc_keeper_set_state(struct qc_session *s, int state, int rc, const char *msg)
{
    qc_state_cb cb = NULL;
    void       *ud = NULL;
    int         changed;

    if (!s)
        return;

    qc_mutex_lock(&s->st_lock);
    changed = (s->state != state);
    s->state = state;
    if (msg)
        snprintf(s->detail, sizeof(s->detail), "%s", msg);
    cb = s->cfg.on_state;
    ud = s->cfg.state_ud;
    qc_mutex_unlock(&s->st_lock);

    if (cb && changed)
        cb(ud, state, rc, s->detail);
}
