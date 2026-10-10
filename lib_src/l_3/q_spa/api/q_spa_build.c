/**
 * @file q_spa_build.c
 * @brief SPA 报文构造（客户端侧）
 */
#include "headers.h"
#include "q_spa.h"
#include "q_crypto.h"
#include "q_rand.h"
#include <arpa/inet.h>

int q_spa_build(const uint8_t *spa_key, size_t key_len, uint8_t key_id, const uint8_t user_id[Q_SPA_USERID_LEN],
                uint16_t req_port, uint16_t req_ttl, const uint8_t ip_hint[16], q_spa_pkt_t *out)
{
    if (!spa_key || key_len == 0 || !user_id || !ip_hint || !out)
        return -1;

    memset(out, 0, sizeof(*out));
    out->magic[0] = 0x53;
    out->magic[1] = 0x50;
    out->magic[2] = 0x41;
    out->magic[3] = 0x01;
    out->version = Q_SPA_VERSION;
    out->key_id = key_id;
    out->msg_type = Q_SPA_MSG_OPEN;
    out->reserved = 0;
    out->timestamp = htonl((uint32_t)time(NULL));
    if (q_rand_bytes(out->nonce, Q_SPA_NONCE_LEN) != 0)
        return -1;
    memcpy(out->user_id, user_id, Q_SPA_USERID_LEN);
    out->req_port = htons(req_port);
    out->req_ttl = htons(req_ttl);

    /* 明文 = client_ip_hint(16) || rnd(16) */
    uint8_t pt[Q_SPA_CIPHER_LEN];
    memcpy(pt, ip_hint, 16);
    if (q_rand_bytes(pt + 16, 16) != 0)
        return -1;

    /* AAD = 报文头（magic..req_ttl，共 60 字节）；cipher/tag 不在 AAD 内 */
    size_t aad_len = (size_t)((const uint8_t *)&out->cipher - (const uint8_t *)out);
    size_t ctlen = 0;
    if (q_crypto_sm4_gcm_encrypt(spa_key, key_len, out->nonce, Q_SPA_NONCE_LEN, (const uint8_t *)out, aad_len, pt,
                                 sizeof(pt), out->cipher, out->tag, &ctlen) != 0)
        return -1;
    return 0;
}
