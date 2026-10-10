/**
 * @file q_sec_session_key.c
 * @brief q_sec_session_key —— SM2 ECDH 共享密钥 + SM3-KDF 派生会话密钥
 */
#include "headers.h"
#include "q_sec.h"
#include "q_crypto.h"
#include "q_mem.h"

int q_sec_session_key(const uint8_t *my_priv_der, size_t my_priv_len,
                      const uint8_t *peer_pub_der, size_t peer_pub_len,
                      uint64_t seq, uint8_t out[32])
{
    if (!my_priv_der || my_priv_len == 0 || !peer_pub_der || peer_pub_len == 0 || !out)
        return -1;

    uint8_t shared[32];
    if (q_crypto_sm2_derive(my_priv_der, my_priv_len, peer_pub_der, peer_pub_len, shared) != 0)
        return -1;

    /* KDF 材料 = 共享密钥(32) || seq(8, 大端) */
    uint8_t material[40];
    memcpy(material, shared, 32);
    for (int i = 0; i < 8; i++)
        material[32 + i] = (uint8_t)(seq >> (8 * (7 - i)));

    int rc = q_crypto_sm3_kdf(material, sizeof(material), out, 32);

    q_mem_zero(shared, sizeof(shared));     /* 临时共享密钥立即清零 */
    q_mem_zero(material, sizeof(material));
    return rc;
}
