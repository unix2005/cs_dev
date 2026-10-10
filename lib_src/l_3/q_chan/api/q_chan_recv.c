/**
 * @file q_chan_recv.c
 * @brief q_chan_recv —— 解密校验（包头/字段/AAD/tag/seq 窗口/时间窗）
 */
#include "headers.h"
#include "q_chan.h"
#include "q_chan_int.h"

int q_chan_recv(q_chan_t *c, const uint8_t *pkt, size_t pktlen, uint8_t *plain, size_t plaincap, size_t *plainlen,
                uint16_t *cmd_out)
{
    if (!c || !c->established || !pkt || !plain || !plainlen)
        return -1;
    if (pktlen < (size_t)Q_CODEC_HDR_MIN + Q_CHAN_TAG_LEN)
        return -1;

    q_codec_pkt_hdr_t h;
    if (q_codec_pkt_hdr_unpack(pkt, pktlen, &h) != 0)
        return -1;
    if (q_codec_field_validate(&h) != 0)
        return -1;
    if (h.hdr_len != Q_CODEC_HDR_MIN)
        return -1; /* 暂仅支持固定 44 字节包头 */

    size_t need = (size_t)h.hdr_len + (size_t)h.body_len + Q_CHAN_TAG_LEN;
    if (pktlen < need)
        return -1;

    /* seq 防重放滑动窗口（仅接受 (last, last+span] 且未重放） */
    if (q_sec_seq_window_accept(c->win, h.seq) != 1)
        return -1;

    /* 时间戳粗筛 ±300s（防重放辅助，非严格时钟同步） */
    uint64_t now = (uint64_t)time(NULL);
    if (now + Q_CHAN_TS_WINDOW < h.timestamp || h.timestamp + Q_CHAN_TS_WINDOW < now)
        return -1;

    uint8_t iv[Q_CHAN_IV_LEN];
    q_chan_iv_derive(h.nonce, h.seq, iv);

    if (plaincap < (size_t)h.body_len)
        return -1;

    const uint8_t *ct = pkt + h.hdr_len;
    const uint8_t *tag = pkt + h.hdr_len + h.body_len;
    size_t ptlen = 0;
    if (q_crypto_sm4_gcm_decrypt(c->session_key, Q_CHAN_SM4_KEY_LEN, iv, Q_CHAN_IV_LEN, pkt, h.hdr_len, ct, h.body_len,
                                 tag, plain, &ptlen) != 0)
        return -1;

    *plainlen = ptlen;
    if (cmd_out)
        *cmd_out = h.cmd;
    return 0;
}
