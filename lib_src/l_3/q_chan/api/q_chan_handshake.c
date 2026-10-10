/**
 * @file q_chan_handshake.c
 * @brief q_chan_handshake —— SM2 ECDH 密钥协商时序
 *
 * 客户端：生成临时 SM2 密钥对，发送临时公钥；接收服务端静态公钥并校验与预置（pinned）
 *         公钥一致（服务端身份认证），随后 ECDH 派生会话密钥。
 * 服务端：使用静态密钥对；接收客户端临时公钥并派生会话密钥，回送静态公钥。
 * 双方以相同固定 seq0 经 q_sec_session_key 派生，得到一致会话密钥。
 */
#include "headers.h"
#include "q_chan.h"
#include "q_chan_int.h"

/* 以 2 字节大端长度 + DER 发送对端公钥 */
static int send_pub(int fd, const uint8_t *der, size_t len)
{
    uint8_t lb[2] = {(uint8_t)((len >> 8) & 0xff), (uint8_t)(len & 0xff)};
    if (q_net_send_blocking(fd, lb, 2) != 2)
        return -1;
    if (q_net_send_blocking(fd, der, len) != (ssize_t)len)
        return -1;
    return 0;
}

/* 读取 2 字节大端长度 + DER，调用方负责 free */
static uint8_t *recv_pub(int fd, size_t *out_len)
{
    uint8_t lb[2];
    if (q_net_recv_blocking(fd, lb, 2) != 2)
        return NULL;
    size_t len = ((size_t)lb[0] << 8) | lb[1];
    uint8_t *der = malloc(len ? len : 1);
    if (!der)
        return NULL;
    if (len && q_net_recv_blocking(fd, der, len) != (ssize_t)len)
    {
        free(der);
        return NULL;
    }
    *out_len = len;
    return der;
}

int q_chan_handshake(q_chan_t *c, int fd)
{
    if (!c || c->established)
        return -1;

    uint8_t *my_priv = c->local_priv, *my_pub = c->local_pub;
    size_t my_priv_len = c->local_priv_len, my_pub_len = c->local_pub_len;
    int ephemeral = 0;

    if (c->mode == Q_CHAN_MODE_CLIENT)
    {
        /* 客户端：生成临时密钥对（前向安全），不落地静态密钥 */
        if (q_crypto_sm2_keygen(&my_priv, &my_priv_len, &my_pub, &my_pub_len) != 0)
            return -1;
        ephemeral = 1;

        if (send_pub(fd, my_pub, my_pub_len) != 0)
            goto cleanup_ephemeral;

        size_t spub_len = 0;
        uint8_t *spub = recv_pub(fd, &spub_len);
        if (!spub)
            goto cleanup_ephemeral;

        /* 服务端身份认证：校验收到的静态公钥与预置（pinned）公钥一致 */
        if (c->peer_pub_pinned_len != spub_len || memcmp(c->peer_pub_pinned, spub, spub_len) != 0)
        {
            free(spub);
            goto cleanup_ephemeral;
        }
        if (q_sec_session_key(my_priv, my_priv_len, spub, spub_len, c->seq0, c->session_key) != 0)
        {
            free(spub);
            goto cleanup_ephemeral;
        }
        free(spub);
    }
    else
    {
        /* 服务端：使用静态密钥对 */
        if (my_priv == NULL || my_pub == NULL)
            return -1;
        size_t cpub_len = 0;
        uint8_t *cpub = recv_pub(fd, &cpub_len);
        if (!cpub)
            return -1;
        if (q_sec_session_key(my_priv, my_priv_len, cpub, cpub_len, c->seq0, c->session_key) != 0)
        {
            free(cpub);
            return -1;
        }
        if (send_pub(fd, my_pub, my_pub_len) != 0)
        {
            free(cpub);
            return -1;
        }
        free(cpub);
    }

    if (q_rand_bytes(c->nonce_base, Q_CHAN_NONCE_LEN) != 0)
    {
        if (ephemeral)
        {
            free(my_priv);
            free(my_pub);
        }
        return -1;
    }
    c->seq_send = c->seq0 + 1;
    c->win = q_sec_seq_window_create(Q_CHAN_SEQ_SPAN);
    if (!c->win)
    {
        if (ephemeral)
        {
            free(my_priv);
            free(my_pub);
        }
        return -1;
    }
    c->established = 1;
    if (ephemeral)
    {
        free(my_priv);
        free(my_pub);
    }
    return 0;

cleanup_ephemeral:
    if (ephemeral)
    {
        free(my_priv);
        free(my_pub);
    }
    return -1;
}
