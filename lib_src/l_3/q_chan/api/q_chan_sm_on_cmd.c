/**
 * @file q_chan_sm_on_cmd.c
 * @brief 收包一站式状态推进：先查白名单，再按命令类型迁移
 */
#include "headers.h"
#include "q_chan.h"

int q_chan_sm_on_cmd(q_chan_st_t *st, uint16_t cmd, int login_ok)
{
    if (!st)
        return -1;
    if (!q_chan_sm_cmd_allowed(*st, cmd))
        return -1; /* 白名单外命令 -> 应断连 */

    switch (cmd)
    {
    case Q_CHAN_CMD_HELLO:
        return q_chan_sm_transition(st, Q_CHAN_EV_HELLO);
    case Q_CHAN_CMD_LOGIN:
        return q_chan_sm_transition(st, login_ok ? Q_CHAN_EV_LOGIN_OK : Q_CHAN_EV_LOGIN_FAIL);
    case Q_CHAN_CMD_HEARTBEAT:
    case Q_CHAN_CMD_BIZ_QUERY:
    case Q_CHAN_CMD_BIZ_UPDATE:
    case Q_CHAN_CMD_VIDEO_OPEN:
    case Q_CHAN_CMD_LOGOUT:
        return q_chan_sm_transition(st, Q_CHAN_EV_AUTH_CMD);
    default:
        return -1;
    }
}
