/**
 * @file q_codec_field_validate.c
 * @brief q_codec_field_validate —— 帧头字段合法性校验（结构层，不含加解密）
 */
#include "headers.h"
#include "q_codec.h"

int q_codec_field_validate(const q_codec_pkt_hdr_t *h)
{
    if (!h)
        return -1;
    if (h->magic != Q_CODEC_MAGIC)
        return -1;
    if (h->version != Q_CODEC_VERSION)
        return -1;
    if (h->hdr_len < Q_CODEC_HDR_MIN)
        return -1;
    /* cmd 约定为非零、且不超过 15 位（业务层进一步细分） */
    if (h->cmd == 0 || h->cmd > 0x7fff)
        return -1;
    /* 最高位为保留标志，禁止置位（示例约束） */
    if (h->flags & 0x80)
        return -1;
    if (h->body_len > Q_CODEC_BODY_MAX)
        return -1;
    return 0;
}
