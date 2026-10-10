/**
 * @file q_codec_pkt_hdr_unpack.c
 * @brief q_codec_pkt_hdr_unpack —— 帧头反序列化（网络序 -> 主机序）
 */
#include "headers.h"
#include "q_codec.h"

/* 大端字节序 -> 64 位主机序（逐字节，避免强转别名/对齐 UB） */
static uint64_t get_u64(const uint8_t **pp)
{
    const uint8_t *p = *pp;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v = (v << 8) | p[i];
    *pp = p + 8;
    return v;
}

int q_codec_pkt_hdr_unpack(const uint8_t *buf, size_t buflen, q_codec_pkt_hdr_t *h)
{
    if (!buf || buflen < Q_CODEC_HDR_MIN || !h)
        return -1;

    const uint8_t *p = buf;
    memcpy(&h->magic, p, 4);     h->magic = ntohl(h->magic);              p += 4;
    h->version = *p++;
    memcpy(&h->cmd, p, 2);       h->cmd = ntohs(h->cmd);                 p += 2;
    memcpy(&h->key_id, p, 2);    h->key_id = ntohs(h->key_id);           p += 2;
    h->flags = *p++;
    memcpy(&h->hdr_len, p, 2);   h->hdr_len = ntohs(h->hdr_len);         p += 2;
    h->seq = get_u64(&p);
    h->timestamp = get_u64(&p);
    memcpy(h->nonce, p, 12);                                             p += 12;
    memcpy(&h->body_len, p, 4);  h->body_len = ntohl(h->body_len);       p += 4;
    return 0;
}
