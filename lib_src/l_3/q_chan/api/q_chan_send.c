/**
 * @file q_chan_send.c
 * @brief q_chan_send —— 加密编码（SM4-GCM，IV=nonce XOR seq，AAD=包头全部字段）
 */
#include "headers.h"
#include "q_chan.h"
#include "q_chan_int.h"

int q_chan_send(q_chan_t *c, const uint8_t *plain, size_t plen, uint16_t cmd, uint8_t *out, size_t outcap,
                size_t *outlen)
{
    if (!c || !c->established || !out || !outlen)
        return -1;
    if (outcap < (size_t)Q_CODEC_HDR_MIN + plen + Q_CHAN_TAG_LEN)
        return -1;

    q_codec_pkt_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.magic = Q_CODEC_MAGIC;
    h.version = Q_CODEC_VERSION;
    h.cmd = cmd;
    h.key_id = c->key_id;
    h.flags = 0;
    h.hdr_len = Q_CODEC_HDR_MIN;
    h.seq = c->seq_send;
    h.timestamp = (uint64_t)time(NULL);
    memcpy(h.nonce, c->nonce_base, Q_CHAN_NONCE_LEN);
    h.body_len = (uint32_t)plen;

    size_t hlen = 0;
    if (q_codec_pkt_hdr_pack(&h, out, outcap, &hlen) != 0)
        return -1;

    uint8_t iv[Q_CHAN_IV_LEN];
    q_chan_iv_derive(c->nonce_base, c->seq_send, iv);

    /* AAD = 包头全部字段（已打包的 hlen 字节） */
    size_t ctlen = 0;
    if (q_crypto_sm4_gcm_encrypt(c->session_key, Q_CHAN_SM4_KEY_LEN, iv, Q_CHAN_IV_LEN, out, hlen, plain, plen,
                                 out + hlen, out + hlen + plen, &ctlen) != 0)
        return -1;

    *outlen = hlen + ctlen + Q_CHAN_TAG_LEN;
    c->seq_send += 1;
    return 0;
}
