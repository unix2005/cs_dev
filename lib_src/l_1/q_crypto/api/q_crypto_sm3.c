/**
 * @file q_crypto_sm3.c
 * @brief q_crypto_sm3 —— SM3 哈希（基于 Tongsuo EVP_sm3）
 */
#include "headers.h"
#include "q_crypto.h"

int q_crypto_sm3(const uint8_t *data, size_t dlen, uint8_t out[32])
{
    if (!data || !out)
        return -1;

    unsigned int n = 0;
    if (EVP_Digest(data, dlen, out, &n, EVP_sm3(), NULL) != 1)
        return -1;
    return 0;
}
