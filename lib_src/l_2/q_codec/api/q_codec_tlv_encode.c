/**
 * @file q_codec_tlv_encode.c
 * @brief q_codec_tlv_encode —— TLV 编码（tag+len 网络序）
 */
#include "headers.h"
#include "q_codec.h"

int q_codec_tlv_encode(uint8_t *out, size_t outcap, uint16_t tag,
                       const uint8_t *val, uint16_t vlen, size_t *outlen)
{
    size_t need = (size_t)4 + vlen;
    if (!out || outcap < need || !outlen)
        return -1;

    uint8_t *p = out;
    uint16_t t = htons(tag);  memcpy(p, &t, 2); p += 2;
    uint16_t l = htons(vlen); memcpy(p, &l, 2); p += 2;
    if (vlen && val)
        memcpy(p, val, vlen);
    *outlen = need;
    return 0;
}
