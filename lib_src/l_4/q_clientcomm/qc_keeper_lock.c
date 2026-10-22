/**
 * @file qc_keeper_lock.c
 * @brief M4 会话治理：串行化 gate 加锁（业务线程用）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 业务线程在 qc_request / qc_biz_query **之前**调本函数，返回后 qc_keeper_unlock()。
 * keeper 的心跳取同一把锁，故「一次网络往返」期间不会被另一路请求插入 ——
 * 严格 lockstep 的协议因此不会被多线程破坏（否则表现为偶发解包错位/卡死）。
 *
 * 注意：持锁期间不要做重活（UI 渲染、几万行解析），否则心跳会被拖到超时，
 *       反而制造"心跳失败"的假象。
 */
#include "qc_keeper.h"

int qc_keeper_lock(qc_session_t *s)
{
    if (!s || !s->io_inited)
        return -1;
    qc_mutex_lock(&s->io_lock);
    return 0;
}
