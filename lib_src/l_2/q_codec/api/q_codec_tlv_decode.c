/**
 * @file q_codec_tlv_decode.c
 * @brief q_codec_tlv_decode —— TLV 解码单条
 */
#include "headers.h"
#include "q_codec.h"

int q_codec_tlv_decode(const uint8_t *buf, size_t buflen, uint16_t *tag,
                       const uint8_t **val, uint16_t *vlen, size_t *consumed)
{
    if (!buf || buflen < 4 || !tag || !val || !vlen || !consumed)
        return -1;

    const uint8_t *p = buf;
    memcpy(tag, p, 2); *tag = ntohs(*tag); p += 2;
    memcpy(vlen, p, 2); *vlen = ntohs(*vlen); p += 2;
    if ((size_t)4 + *vlen > buflen)
        return -1;
    *val = p;
    *consumed = (size_t)4 + *vlen;
    return 0;
}
