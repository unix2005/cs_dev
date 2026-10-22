/**
 * @file qc_keeper_unlock.c
 * @brief M4 会话治理：串行化 gate 解锁（与 qc_keeper_lock 成对）
 * @layer  lib_src/l_4/q_clientcomm
 */
#include "qc_keeper.h"

int qc_keeper_unlock(qc_session_t *s)
{
    if (!s || !s->io_inited)
        return -1;
    qc_mutex_unlock(&s->io_lock);
    return 0;
}
