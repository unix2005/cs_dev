/**
 * @file q_crypto_hmac_sm3.c
 * @brief q_crypto_hmac_sm3 —— HMAC-SM3（基于 Tongsuo HMAC + EVP_sm3）
 */
#include "headers.h"
#include "q_crypto.h"

int q_crypto_hmac_sm3(const uint8_t *key, size_t klen,
                      const uint8_t *data, size_t dlen, uint8_t out[32])
{
    if (!key || !data || !out)
        return -1;

    unsigned int n = 0;
    if (HMAC(EVP_sm3(), key, (int)klen, data, dlen, out, &n) == NULL)
        return -1;
    return 0;
}
