/**
 * @file qc_keeper_reauth.c
 * @brief M4 会话治理：在**现有连接**上重新登录（Token 失效时的快速路径）
 * @layer  lib_src/l_4/q_clientcomm
 *
 * 与 qc_keeper_rebuild() 的分工：
 *   - 心跳/业务回 Q_IPC_R_UNAUTHORIZED(3) 说明「连接还在，但 Token 过期/不匹配」，
 *     此时没必要拆 TCP + 重做 SM2 握手（那是毫秒级开销 + 一次完整 ECDH），
 *     直接在原连接上重发 SALT_QUERY + LOGIN 即可。
 *   - 若重登仍失败（例如口令失效、服务端把连接也清了），才由调用方退化为 rebuild。
 *
 * 口令红线：本函数**不持有**口令。pwd_hash 由 cfg.on_relogin 回调向 UI 索取，
 *           用完立即清零，函数返回后栈上不残留。
 *
 * 返回：0 成功；>0 服务端结果码；-1 断连/参数非法；-2 UI 无法提供凭据。
 */
#include "qc_keeper.h"

#include "include_ipc_proto.h"   /* Q_PBKDF2_ITER_DEFAULT / Q_IPC_R_* */
#include "q_mem.h"               /* q_mem_zero */

int qc_keeper_reauth(struct qc_session *s)
{
    uint8_t  salt[64];
    size_t   slen = 0u;
    uint8_t  pwd[32];
    uint32_t iter;
    int      rc;

    if (!s || !s->cfg.on_relogin || !s->rc.user[0])
        return -1;

    /* 故障注入：模拟"口令已失效"，用于验证 UI 侧的降级路径 */
    if (s->fault & QC_FAULT_RELOGIN_FAIL)
        return -1;

    iter = s->rc.pbkdf2_iter ? s->rc.pbkdf2_iter : (uint32_t)Q_PBKDF2_ITER_DEFAULT;

    memset(salt, 0, sizeof(salt));
    rc = qc_fetch_salt(s, s->rc.user, salt, sizeof(salt), &slen);
    if (rc != 0 || slen == 0u)
    {
        q_mem_zero(salt, sizeof(salt));
        return (rc > 0) ? rc : -1;
    }

    memset(pwd, 0, sizeof(pwd));
    if (s->cfg.on_relogin(s->cfg.relogin_ud, salt, slen, iter, pwd) != 0)
    {
        q_mem_zero(pwd, sizeof(pwd));
        q_mem_zero(salt, sizeof(salt));
        return -2;                 /* UI 无法提供口令：转 OFFLINE 让 UI 弹登录框 */
    }

    /*
     * OTP：重登**不复用**旧的一次性口令 —— 它早已失效，且把 OTP 缓存在内核里
     * 会让"OTP 种子严禁落盘/长驻"的约束形同虚设。服务端若强制校验 OTP，
     * 重登会失败并转 OFFLINE，由 UI 重新走一次完整登录（此时用户可输新 OTP）。
     */
    rc = qc_login(s, s->rc.user, pwd, "000000",
                  s->rc.devid, s->rc.devid_len,
                  s->rc.cver[0] ? s->rc.cver : "qc_client/1.1",
                  s->token);

    q_mem_zero(pwd, sizeof(pwd));
    q_mem_zero(salt, sizeof(salt));
    return rc;
}
