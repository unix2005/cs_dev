/**
 * @file q_chan_sm_transition.c
 * @brief 连接状态机：事件驱动的合法态迁移
 */
#include "headers.h"
#include "q_chan.h"

int q_chan_sm_transition(q_chan_st_t *st, q_chan_ev_t ev)
{
    if (!st)
        return -1;
    q_chan_st_t cur = *st;
    q_chan_st_t next = cur;

    switch (ev)
    {
    case Q_CHAN_EV_HANDSHAKE_OK:
        /* 仅 NEW/HELLO 可进入 KEYED（密钥交换完成） */
        if (cur == Q_CHAN_ST_NEW || cur == Q_CHAN_ST_HELLO)
            next = Q_CHAN_ST_KEYED;
        else
            return -1;
        break;
    case Q_CHAN_EV_HELLO:
        /* HELLO 可在 NEW/HELLO/KEYED 收到，不改变已 KEYED 的事实 */
        if (cur == Q_CHAN_ST_NEW || cur == Q_CHAN_ST_HELLO)
            next = Q_CHAN_ST_HELLO;
        else if (cur == Q_CHAN_ST_KEYED)
            next = Q_CHAN_ST_KEYED;
        else
            return -1;
        break;
    case Q_CHAN_EV_LOGIN_OK:
        if (cur == Q_CHAN_ST_KEYED)
            next = Q_CHAN_ST_AUTHED;
        else
            return -1;
        break;
    case Q_CHAN_EV_LOGIN_FAIL:
        if (cur == Q_CHAN_ST_KEYED)
            next = Q_CHAN_ST_CLOSED;
        else
            return -1;
        break;
    case Q_CHAN_EV_AUTH_CMD:
        if (cur == Q_CHAN_ST_AUTHED)
            next = Q_CHAN_ST_AUTHED;
        else
            return -1;
        break;
    case Q_CHAN_EV_TIMEOUT:
        next = Q_CHAN_ST_CLOSED;
        break;
    default:
        return -1;
    }

    *st = next;
    return 0;
}
