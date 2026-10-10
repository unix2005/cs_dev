/**
 * @file q_chan_binding.c
 * @brief q_chan_binding —— 通道绑定材料（SM3(session_key)）
 */
#include "headers.h"
#include "q_chan.h"
#include "q_chan_int.h"

int q_chan_binding(q_chan_t *c, uint8_t out[32])
{
    if (!c || !c->established || !out) return -1;
    return q_crypto_sm3(c->session_key, Q_CHAN_KEY_LEN, out);
}
