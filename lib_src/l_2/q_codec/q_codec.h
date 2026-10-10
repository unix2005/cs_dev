/**
 * @file q_codec.h
 * @brief l_2 能力层 帧编解码库（libqcodec）对外头文件
 * @layer  lib_src/l_2/q_codec  ->  产出 libqcodec
 * @note   本文件自包含：仅依赖 include_stdio.h（提供 uint8_t/size_t 等）。
 *         职责（AI_SKILL_RULE §3.1 l_2）：帧头(pkt_hdr)解析/序列化、TLV 编解码、
 *         字段校验表；**仅做结构编解码，不做加解密**（加解密由 q_crypto 负责）。
 */
#ifndef L_2_Q_CODEC_H
#define L_2_Q_CODEC_H

#include "include_stdio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define Q_CODEC_MAGIC      0x43534433u   /* "CSD3" */
#define Q_CODEC_HDR_MIN    44            /* 定长包头字节数（见 q_codec_pkt_hdr_t） */
#define Q_CODEC_VERSION    1
#define Q_CODEC_BODY_MAX  (16u * 1024u * 1024u)

/* 帧头（网络字节序存储）；与 AI_SKILL_RULE §3.2.6 AAD 字段一致：
   magic/version/cmd/key_id/flags/hdr_len/seq/timestamp/nonce/body_len */
typedef struct
{
    uint32_t magic;        /* 4  魔数 */
    uint8_t  version;      /* 1  版本 */
    uint16_t cmd;          /* 2  命令字 */
    uint16_t key_id;       /* 2  密钥标识 */
    uint8_t  flags;        /* 1  标志位 */
    uint16_t hdr_len;      /* 2  包头长度（>= Q_CODEC_HDR_MIN） */
    uint64_t seq;          /* 8  序列号 */
    uint64_t timestamp;    /* 8  时间戳（秒） */
    uint8_t  nonce[12];    /* 12 GCM nonce */
    uint32_t body_len;     /* 4  包体长度 */
} q_codec_pkt_hdr_t;

/* 序列化：out 需 >= Q_CODEC_HDR_MIN；成功 0，参数/空间不足 -1 */
int q_codec_pkt_hdr_pack(const q_codec_pkt_hdr_t *h, uint8_t *out, size_t outcap, size_t *outlen);

/* 反序列化（不做业务校验，仅长度/magic 粗筛由 field_validate 负责）；成功 0，失败 -1 */
int q_codec_pkt_hdr_unpack(const uint8_t *buf, size_t buflen, q_codec_pkt_hdr_t *h);

/* TLV 编码：布局 tag(2,网络序)+len(2,网络序)+val(vlen)；成功 0，空间不足 -1 */
int q_codec_tlv_encode(uint8_t *out, size_t outcap, uint16_t tag,
                       const uint8_t *val, uint16_t vlen, size_t *outlen);

/* TLV 解码单条；consumed 返回本条占用字节；成功 0，参数/越界 -1 */
int q_codec_tlv_decode(const uint8_t *buf, size_t buflen, uint16_t *tag,
                       const uint8_t **val, uint16_t *vlen, size_t *consumed);

/* 字段合法性校验（magic/版本/长度/cmd/flags/body 上限）；合法 0，非法 -1 */
int q_codec_field_validate(const q_codec_pkt_hdr_t *h);

#ifdef __cplusplus
}
#endif

#endif /* L_2_Q_CODEC_H */
