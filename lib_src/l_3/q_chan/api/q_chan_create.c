/**
 * @file q_chan_create.c
 * @brief q_chan_create —— 创建安全通道句柄
 */
#include "headers.h"
#include "q_chan.h"
#include "q_chan_int.h"

q_chan_t *q_chan_create(int mode, const uint8_t *local_priv, size_t local_priv_len, const uint8_t *local_pub,
                        size_t local_pub_len, const uint8_t *peer_pub, size_t peer_len, uint64_t seq0, uint16_t key_id)
{
    if (mode != Q_CHAN_MODE_CLIENT && mode != Q_CHAN_MODE_SERVER)
        return NULL;
    /* 服务端必须持有静态密钥对；客户端必须预置服务端公钥用于身份认证 */
    if (mode == Q_CHAN_MODE_SERVER && (local_priv == NULL || local_pub == NULL))
        return NULL;
    if (mode == Q_CHAN_MODE_CLIENT && (peer_pub == NULL || peer_len == 0))
        return NULL;

    q_chan_t *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;

    c->mode = mode;
    c->seq0 = seq0;
    c->key_id = key_id;
    c->established = 0;

    if (local_priv && local_priv_len)
    {
        c->local_priv = malloc(local_priv_len);
        if (!c->local_priv)
        {
            free(c);
            return NULL;
        }
        memcpy(c->local_priv, local_priv, local_priv_len);
        c->local_priv_len = local_priv_len;
    }
    if (local_pub && local_pub_len)
    {
        c->local_pub = malloc(local_pub_len);
        if (!c->local_pub)
        {
            free(c->local_priv);
            free(c);
            return NULL;
        }
        memcpy(c->local_pub, local_pub, local_pub_len);
        c->local_pub_len = local_pub_len;
    }
    if (peer_pub && peer_len)
    {
        c->peer_pub_pinned = malloc(peer_len);
        if (!c->peer_pub_pinned)
        {
            free(c->local_pub);
            free(c->local_priv);
            free(c);
            return NULL;
        }
        memcpy(c->peer_pub_pinned, peer_pub, peer_len);
        c->peer_pub_pinned_len = peer_len;
    }
    return c;
}
