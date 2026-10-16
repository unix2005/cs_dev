/**
 * @file qc_ut.c
 * @brief libqclientcomm 冒烟单元测试（不依赖运行中的服务端）
 * @note  验证：编译/链接闭包、会话创建空路径、PBKDF2-SM3 派生、内层 TLV 编解码回环、
 *        qc_fetch_salt 的参数校验与未连接路径。
 *        连接/握手/SALT_QUERY/登录等需真实 netd 端到端联调，不在此覆盖。
 */
#include "qc_client.h"
#include "q_codec.h"          /* 直接验证内层 TLV 编解码与文档一致 */
#include "include_ipc_proto.h" /* Q_IPC_T_USERNAME / Q_IPC_T_PWDHASH 内层 TLV tag */

#include <stdio.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg)                                                    \
    do                                                                      \
    {                                                                       \
        if (!(cond))                                                        \
        {                                                                   \
            printf("  FAIL: %s\n", (msg));                                  \
            g_fail++;                                                       \
        }                                                                   \
        else                                                                \
        {                                                                   \
            printf("  PASS: %s\n", (msg));                                  \
        }                                                                   \
    } while (0)

int main(void)
{
    uint8_t  salt[32];
    size_t   slen = 0;
    uint8_t  bogus[64] = {0};
    uint8_t  ph[32];
    uint8_t  dk[32];
    uint8_t  buf[256];
    size_t   off = 0, n = 0, o2 = 0;
    int      rc, result, nonzero, got_ph = 0, got_un = 0;
    qc_session_t *s;

    for (int i = 0; i < 32; i++)
        ph[i] = (uint8_t)i;

    /* 1. 空服务端公钥 -> 必须返回 NULL（致命参数校验） */
    s = qc_session_create(NULL, 0, 1, 1);
    CHECK(s == NULL, "qc_session_create(NULL) -> NULL");

    /* 2. 会话创建后未连接，qc_connect 应因不可达快速失败而非崩溃 */
    s = qc_session_create(bogus, sizeof(bogus), 1, 1);
    CHECK(s != NULL, "qc_session_create(bogus der) -> non-NULL handle");
    if (s)
    {
        rc = qc_connect(s, "240.0.0.1", 1);
        CHECK(rc < 0, "qc_connect(unreachable) -> <0 (no crash)");
    }

    /* 3. qc_fetch_salt 参数校验（方案A 新增接口的空路径） */
    rc = qc_fetch_salt(NULL, "alice", salt, sizeof(salt), &slen);
    CHECK(rc == -1, "qc_fetch_salt(NULL session) -> -1");
    rc = qc_fetch_salt(s, NULL, salt, sizeof(salt), &slen);
    CHECK(rc == -1, "qc_fetch_salt(NULL username) -> -1");
    rc = qc_fetch_salt(s, "alice", NULL, sizeof(salt), &slen);
    CHECK(rc == -1, "qc_fetch_salt(NULL salt_out) -> -1");
    rc = qc_fetch_salt(s, "alice", salt, 0, &slen);
    CHECK(rc == -1, "qc_fetch_salt(salt_cap=0) -> -1");
    rc = qc_fetch_salt(s, "alice", salt, sizeof(salt), NULL);
    CHECK(rc == -1, "qc_fetch_salt(NULL salt_len_out) -> -1");
    /* 已创建但未连接（fd 非法）-> 应返回 -1 而非崩溃 */
    rc = qc_fetch_salt(s, "alice", salt, sizeof(salt), &slen);
    CHECK(rc == -1, "qc_fetch_salt(未连接会话) -> -1 (no crash)");

    /* 4. PBKDF2-SM3 口令派生（纯本地，不触网） */
    rc = qc_pbkdf2_sm3((const uint8_t *)"password", 8,
                       (const uint8_t *)"saltsalt", 8, 100000u, dk);
    CHECK(rc == 0, "qc_pbkdf2_sm3 -> 0");
    nonzero = 0;
    for (int i = 0; i < 32; i++)
        if (dk[i]) { nonzero = 1; break; }
    CHECK(nonzero, "qc_pbkdf2_sm3 -> non-zero 32B");
    /* 参数校验 */
    rc = qc_pbkdf2_sm3(NULL, 8, (const uint8_t *)"s", 1, 100000u, dk);
    CHECK(rc == -1, "qc_pbkdf2_sm3(NULL pass) -> -1");

    /* 5. 内层 TLV 编解码回环（USERNAME + PWDHASH），验证与文档 tag 一致 */
    CHECK(q_codec_tlv_encode(buf + off, sizeof(buf) - off, Q_IPC_T_PWDHASH, ph, 32, &n) == 0,
          "tlv_encode PWDHASH(32)");
    off += n;
    CHECK(q_codec_tlv_encode(buf + off, sizeof(buf) - off, Q_IPC_T_USERNAME,
                             (const uint8_t *)"alice", 5, &n) == 0,
          "tlv_encode USERNAME(alice)");
    off += n;

    while (o2 < off)
    {
        uint16_t tag;
        const uint8_t *v;
        uint16_t vlen;
        size_t used;
        if (q_codec_tlv_decode(buf + o2, off - o2, &tag, &v, &vlen, &used) != 0)
            break;
        if (tag == Q_IPC_T_PWDHASH && vlen == 32 && memcmp(v, ph, 32) == 0)
            got_ph = 1;
        if (tag == Q_IPC_T_USERNAME && vlen == 5 && memcmp(v, "alice", 5) == 0)
            got_un = 1;
        o2 += used;
    }
    CHECK(got_ph && got_un, "TLV decode round trip (USERNAME+PWDHASH)");

    /* 6. RESULT 解析：构造 RESULT=0，确认 TLV 结构可回环（模拟 SALT_QUERY 响应） */
    {
        uint8_t rb[4] = {0, 0, 0, 0};
        uint8_t sbuf[64];
        size_t  soff = 0, sn = 0, roff = 0;
        CHECK(q_codec_tlv_encode(sbuf + soff, sizeof(sbuf) - soff, Q_IPC_T_RESULT, rb, 4, &sn) == 0,
              "tlv_encode RESULT(0)");
        soff += sn;
        CHECK(q_codec_tlv_encode(sbuf + soff, sizeof(sbuf) - soff, Q_IPC_T_BODY, ph, 16, &sn) == 0,
              "tlv_encode BODY(salt 16B)");
        soff += sn;
        result = -1;
        slen = 0;
        while (roff < soff)
        {
            uint16_t tag;
            const uint8_t *v;
            uint16_t vlen;
            size_t used;
            if (q_codec_tlv_decode(sbuf + roff, soff - roff, &tag, &v, &vlen, &used) != 0)
                break;
            roff += used;
            if (tag == Q_IPC_T_RESULT && vlen >= 4)
                result = ((uint32_t)v[0] << 24) | ((uint32_t)v[1] << 16) |
                         ((uint32_t)v[2] << 8) | (uint32_t)v[3];
            else if (tag == Q_IPC_T_BODY && vlen > 0 && vlen <= sizeof(salt))
            {
                memcpy(salt, v, vlen);
                slen = vlen;
            }
        }
        CHECK(result == 0 && slen == 16, "SALT_QUERY 响应解析：RESULT=0 + BODY(salt 16B)");
    }

    if (s)
        qc_session_destroy(s);

    printf("\n%s\n", g_fail == 0 ? "[ALL PASS]" : "[SOME FAILED]");
    return g_fail == 0 ? 0 : 1;
}
