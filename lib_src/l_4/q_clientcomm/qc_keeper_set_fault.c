/**
 * @file qc_keeper_set_fault.c
 * @brief M4 会话治理：故障注入开关（仅供联调 / UT）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 没有它，重连/重登这两条路径在生产环境里几乎没法验证 ——
 * 总不能为了测重连去拔网线。注入点只有心跳与重登两处，覆盖面足够：
 *   QC_FAULT_HB_FAIL       心跳视为断连        -> 验证退避重连
 *   QC_FAULT_HB_RC3        心跳返回 3          -> 验证 Token 失效自动重登
 *   QC_FAULT_RELOGIN_FAIL  重登必定失败        -> 验证 OFFLINE 降级
 * 生产必须恒为 QC_FAULT_NONE。
 */
#include "qc_keeper.h"

void qc_keeper_set_fault(qc_session_t *s, uint32_t mask)
{
    if (!s)
        return;
    qc_mutex_lock(&s->st_lock);
    s->fault = mask;
    qc_mutex_unlock(&s->st_lock);
}
