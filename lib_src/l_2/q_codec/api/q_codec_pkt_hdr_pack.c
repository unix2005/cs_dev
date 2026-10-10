/**
 * @file q_codec_pkt_hdr_pack.c
 * @brief q_codec_pkt_hdr_pack —— 帧头序列化（主机序 -> 网络序）
 */
#include "headers.h"
#include "q_codec.h"

static void put_u16(uint8_t **pp, uint16_t v)
{
    uint16_t n = htons(v);
    memcpy(*pp, &n, 2);
    *pp += 2;
}

/* 64 位主机序 -> 大端字节序（逐字节，避免强转别名/对齐 UB） */
static void put_u64(uint8_t **pp, uint64_t v)
{
    uint8_t *p = *pp;
    for (int i = 7; i >= 0; i--)
        *p++ = (uint8_t)(v >> (8 * i));
    *pp = p;
}

int q_codec_pkt_hdr_pack(const q_codec_pkt_hdr_t *h, uint8_t *out, size_t outcap, size_t *outlen)
{
    if (!h || !out || outcap < Q_CODEC_HDR_MIN || !outlen)
        return -1;

    uint8_t *p = out;
    uint32_t magic = htonl(h->magic);
    memcpy(p, &magic, 4); p += 4;
    *p++ = h->version;
    put_u16(&p, h->cmd);
    put_u16(&p, h->key_id);
    *p++ = h->flags;
    put_u16(&p, h->hdr_len);
    put_u64(&p, h->seq);
    put_u64(&p, h->timestamp);
    memcpy(p, h->nonce, 12); p += 12;
    uint32_t bl = htonl(h->body_len);
    memcpy(p, &bl, 4); p += 4;

    *outlen = (size_t)(p - out);
    return 0;
}
