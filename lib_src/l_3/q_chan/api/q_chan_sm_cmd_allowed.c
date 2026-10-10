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
        /* 已协商密钥、未登录：允许 HELLO 与 LOGIN */
        return (cmd == Q_CHAN_CMD_HELLO || cmd == Q_CHAN_CMD_LOGIN) ? 1 : 0;
    case Q_CHAN_ST_AUTHED:
        /* 已认证：仅业务指令 */
        return (cmd == Q_CHAN_CMD_HEARTBEAT || cmd == Q_CHAN_CMD_BIZ_QUERY || cmd == Q_CHAN_CMD_BIZ_UPDATE ||
                cmd == Q_CHAN_CMD_VIDEO_OPEN || cmd == Q_CHAN_CMD_LOGOUT)
                   ? 1
                   : 0;
    case Q_CHAN_ST_CLOSED:
    default:
        return 0;
    }
}
