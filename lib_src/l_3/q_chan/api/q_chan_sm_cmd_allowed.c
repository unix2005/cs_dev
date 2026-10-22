/**
 * @file q_chan_sm_cmd_allowed.c
 * @brief 未认证态指令白名单（防越权命令）
 */
#include "headers.h"
#include "q_chan.h"

int q_chan_sm_cmd_allowed(q_chan_st_t st, uint16_t cmd)
{
    switch (st)
    {
    case Q_CHAN_ST_NEW:
    case Q_CHAN_ST_HELLO:
        /* 密钥交换前仅允许 HELLO（KEY_EXCHANGE 由 q_chan_handshake 带外完成） */
        return (cmd == Q_CHAN_CMD_HELLO) ? 1 : 0;
    case Q_CHAN_ST_KEYED:
        /* 已协商密钥、未登录：允许 HELLO / LOGIN / SALT_QUERY（登录前拉 salt） */
        return (cmd == Q_CHAN_CMD_HELLO || cmd == Q_CHAN_CMD_LOGIN ||
                cmd == Q_CHAN_CMD_SALT_QUERY) ? 1 : 0;
    case Q_CHAN_ST_AUTHED:
        /* 已认证：业务指令 + **重新登录**（SALT_QUERY / LOGIN）。
         *
         * 为什么 AUTHED 下还要允许 LOGIN：后台 Token 有 TTL（1800s）且心跳不续期，
         * 长连接客户端必然要定期重新认证。若规定"重登必须先拆 TCP 再重做 SM2 握手"，
         * 护网场景下还要重新过一次 SPA 敲门（放行条目有 TTL），代价大且更易失败 ——
         * 于是客户端倾向于"不重登、将就着用"，反而制造更难排查的偶发 401。
         * 允许在原连接上重认证不等于放宽鉴权：命令内容仍然是完整口令校验，
         * 未认证态本来就允许 LOGIN，此处并没有扩大任何攻击面。 */
        return (cmd == Q_CHAN_CMD_HEARTBEAT || cmd == Q_CHAN_CMD_BIZ_QUERY ||
                cmd == Q_CHAN_CMD_BIZ_UPDATE || cmd == Q_CHAN_CMD_VIDEO_OPEN ||
                cmd == Q_CHAN_CMD_LOGOUT || cmd == Q_CHAN_CMD_SALT_QUERY ||
                cmd == Q_CHAN_CMD_LOGIN) ? 1 : 0;
    case Q_CHAN_ST_CLOSED:
    default:
        return 0;
    }
}
