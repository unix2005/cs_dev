/**
 * @file qc_push_set_callbacks.c
 * @brief M5：qc_push_set_callbacks —— 注册轨迹/报警/重登回调
 * @layer  lib_src/l_4/q_clientcomm
 */
#include "qc_push_int.h"   /* 公开 API + 内部结构体 */

int qc_push_set_callbacks(qc_push_t *p, qc_traj_cb traj, qc_alarm_cb alarm,
                          qc_push_relogin_cb relogin, void *ud)
{
    if (!p)
        return -1;

    p->on_traj  = traj;
    p->on_alarm = alarm;
    p->cb_ud    = ud;
    p->on_relogin = relogin;
    p->relogin_ud = ud;   /* 同一 ud：UI 侧一个对象即可 */
    return 0;
}
