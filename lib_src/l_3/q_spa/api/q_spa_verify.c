/**
 * @file q_spa_verify.c
 * @brief SPA 报文校验（knockd 侧）；校验失败须由调用方静默丢弃
 */
#include "headers.h"
#include "q_spa.h"
#include "q_crypto.h"
#include <arpa/inet.h>

int q_spa_verify(const uint8_t *spa_key, size_t key_len, const q_spa_pkt_t *pkt, uint64_t now_sec,
                 uint16_t *out_req_port, uint16_t *out_req_ttl)
{
    if (!spa_key || key_len == 0 || !pkt || !out_req_port || !out_req_ttl)
        return -1;

    /* 魔数 / 版本 / 类型 粗筛（恒定时间失败，不响应） */
    if (pkt->magic[0] != 0x53 || pkt->magic[1] != 0x50 || pkt->magic[2] != 0x41 || pkt->magic[3] != 0x01)
        return -1;
    if (pkt->version != Q_SPA_VERSION)
        return -1;
    if (pkt->msg_type != Q_SPA_MSG_OPEN)
        return -1;

    /* 时间戳 ±30s 粗筛 */
    uint32_t ts = ntohl(pkt->timestamp);
    if (now_sec + Q_SPA_TS_WINDOW < (uint64_t)ts || (uint64_t)ts + Q_SPA_TS_WINDOW < now_sec)
        return -1;

    /* SM4-GCM 验签（AAD = 报文头 60 字节，含 nonce/timestamp/user_id，整体认证） */
    size_t aad_len = (size_t)((const uint8_t *)&pkt->cipher - (const uint8_t *)pkt);
    uint8_t pt[Q_SPA_CIPHER_LEN];
    size_t ptlen = 0;
    if (q_crypto_sm4_gcm_decrypt(spa_key, key_len, pkt->nonce, Q_SPA_NONCE_LEN, (const uint8_t *)pkt, aad_len,
                                 pkt->cipher, Q_SPA_CIPHER_LEN, pkt->tag, pt, &ptlen) != 0)
        return -1;
    /* pt = ip_hint(16) || rnd(16)，knockd 不采用 ip_hint，此处仅作解密成功判定 */

    *out_req_port = ntohs(pkt->req_port);
    *out_req_ttl = ntohs(pkt->req_ttl);
    return 0;
}
