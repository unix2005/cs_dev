/**
 * @file qc_set_reconnect_info.c
 * @brief M4 会话治理：登记重连材料（地址 / 账号 / 敲门参数 / 设备指纹）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 登录成功后调一次。内容**不含口令、不含派生值** —— 断线自动重连时，
 * pwd_hash 仍要经 cfg.on_relogin 向 UI 索取。这样即使本结构被转储/落盘，
 * 也不会泄露凭据。
 */
#include "qc_keeper.h"

int qc_set_reconnect_info(qc_session_t *s, const qc_reconnect_t *info)
{
    if (!s || !info)
        return -1;

    qc_mutex_lock(&s->st_lock);
    memcpy(&s->rc, info, sizeof(s->rc));
    s->rc.host[sizeof(s->rc.host) - 1u]  = '\0';
    s->rc.user[sizeof(s->rc.user) - 1u]  = '\0';
    s->rc.knock[sizeof(s->rc.knock) - 1u] = '\0';
    s->rc.cver[sizeof(s->rc.cver) - 1u]  = '\0';
    qc_mutex_unlock(&s->st_lock);
    return 0;
}
