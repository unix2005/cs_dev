/**
 * @file q_codec_ut.c
 * @brief q_codec 单元测试：pkt_hdr 往返、TLV、字段校验
 */
#include "headers.h"
#include "q_codec.h"

static int g_fail = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); g_fail++; } \
                      else printf("PASS: %s\n", m); } while (0)

int main(void)
{
    /* pkt_hdr 往返 */
    q_codec_pkt_hdr_t in;
    memset(&in, 0, sizeof(in));
    in.magic = Q_CODEC_MAGIC;
    in.version = Q_CODEC_VERSION;
    in.cmd = 0x0102;
    in.key_id = 7;
    in.flags = 0x01;
    in.hdr_len = Q_CODEC_HDR_MIN;
    in.seq = 0x0102030405060708ull;
    in.timestamp = 1700000000ull;
    for (int i = 0; i < 12; i++) in.nonce[i] = (uint8_t)(i * 11 + 3);
    in.body_len = 1024;

    uint8_t buf[Q_CODEC_HDR_MIN];
    size_t n = 0;
    CHECK(q_codec_pkt_hdr_pack(&in, buf, sizeof(buf), &n) == 0, "pkt_hdr pack");
    CHECK(n == Q_CODEC_HDR_MIN, "pkt_hdr pack len == 44");

    q_codec_pkt_hdr_t out;
    CHECK(q_codec_pkt_hdr_unpack(buf, sizeof(buf), &out) == 0, "pkt_hdr unpack");
    CHECK(out.magic == in.magic && out.cmd == in.cmd && out.key_id == in.key_id, "pkt_hdr 字段一致");
    CHECK(out.seq == in.seq && out.timestamp == in.timestamp, "pkt_hdr seq/timestamp 一致");
    CHECK(out.body_len == in.body_len && memcmp(out.nonce, in.nonce, 12) == 0, "pkt_hdr body/nonce 一致");

    /* 长度不足应失败 */
    CHECK(q_codec_pkt_hdr_unpack(buf, 10, &out) == -1, "pkt_hdr 短缓冲 -> -1");

    /* TLV 往返 */
    uint8_t tlv[64];
    uint16_t tag = 0x1234;
    uint8_t payload[] = {0xde, 0xad, 0xbe, 0xef};
    size_t tn = 0;
    CHECK(q_codec_tlv_encode(tlv, sizeof(tlv), tag, payload, 4, &tn) == 0, "tlv encode");
    CHECK(tn == 8, "tlv encode len == 8");
    uint16_t dtag; const uint8_t *dval; uint16_t dvlen; size_t cons;
    CHECK(q_codec_tlv_decode(tlv, tn, &dtag, &dval, &dvlen, &cons) == 0, "tlv decode");
    CHECK(dtag == tag && dvlen == 4 && memcmp(dval, payload, 4) == 0, "tlv 内容一致");
    CHECK(cons == 8, "tlv consumed == 8");
    CHECK(q_codec_tlv_decode(tlv, 2, &dtag, &dval, &dvlen, &cons) == -1, "tlv 短缓冲 -> -1");

    /* 字段校验 */
    CHECK(q_codec_field_validate(&in) == 0, "field_validate 合法 -> 0");
    q_codec_pkt_hdr_t bad = in; bad.magic = 0x11111111u;
    CHECK(q_codec_field_validate(&bad) == -1, "field_validate 错误 magic -> -1");
    q_codec_pkt_hdr_t bad2 = in; bad2.version = 9;
    CHECK(q_codec_field_validate(&bad2) == -1, "field_validate 错误版本 -> -1");
    q_codec_pkt_hdr_t bad3 = in; bad3.flags = 0x80;
    CHECK(q_codec_field_validate(&bad3) == -1, "field_validate 保留位 -> -1");
    q_codec_pkt_hdr_t bad4 = in; bad4.body_len = Q_CODEC_BODY_MAX + 1;
    CHECK(q_codec_field_validate(&bad4) == -1, "field_validate body 超限 -> -1");

    if (g_fail == 0) printf("\nALL PASS\n");
    else printf("\n%d FAILED\n", g_fail);
    return g_fail ? 1 : 0;
}
