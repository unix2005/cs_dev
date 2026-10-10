/**
 * @file q_chan_destroy.c
 * @brief q_chan_destroy —— 销毁安全通道并清零敏感材料
 */
#include "headers.h"
#include "q_chan.h"
#include "q_chan_int.h"

void q_chan_destroy(q_chan_t *c)
{
    if (!c)
        return;
    if (c->local_priv)
    {
        q_mem_zero(c->local_priv, c->local_priv_len);
        free(c->local_priv);
    }
    if (c->local_pub)
        free(c->local_pub);
    if (c->peer_pub_pinned)
        free(c->peer_pub_pinned);
    q_mem_zero(c->session_key, sizeof(c->session_key));
    q_mem_zero(c->nonce_base, sizeof(c->nonce_base));
    if (c->win)
        q_sec_seq_window_destroy(c->win);
    free(c);
}
