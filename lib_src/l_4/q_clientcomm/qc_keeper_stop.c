/**
 * @file qc_keeper_stop.c
 * @brief M4 会话治理：停止 keeper 线程并等待其退出
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 只停线程，**不关连接**（连接归 qc_close / qc_session_destroy 管）。
 * 幂等，可重复调用；destroy 前必须调用，否则线程会摸到已释放的 session。
 */
#include "qc_keeper.h"

void qc_keeper_stop(qc_session_t *s)
{
    int on;

    if (!s)
        return;

    qc_mutex_lock(&s->st_lock);
    on = s->keeper_on;
    qc_mutex_unlock(&s->st_lock);

    if (!on)
        return;

    s->thread_stop = 1;
    qc_thread_join(&s->thread);

    qc_mutex_lock(&s->st_lock);
    s->keeper_on = 0;
    s->state     = QC_STATE_IDLE;
    s->detail[0] = '\0';
    qc_mutex_unlock(&s->st_lock);
}
