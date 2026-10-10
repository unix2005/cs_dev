/**
 * @file q_sec_pow.c
 * @brief q_sec_pow —— 基于 SM3 的前导零位工作量证明
 */
#include "headers.h"
#include "q_sec.h"
#include "q_crypto.h"

/* 统计 32 字节哈希的前导零位数（最多统计到 need 位即可提前返回） */
static int leading_zero_bits(const uint8_t *h, int need)
{
    int cnt = 0;
    for (int i = 0; i < 32 && cnt < need; i++)
    {
        uint8_t b = h[i];
        if (b == 0)
        {
            cnt += 8;
            continue;
        }
        for (int j = 7; j >= 0; j--)
        {
            if ((b >> j) & 1)
                return cnt;
            cnt++;
        }
    }
    return cnt;
}

static int pow_inner(const uint8_t *seed, size_t seedlen, int difficulty,
                     uint64_t nonce, uint8_t *hash)
{
    if (seedlen > 32)
        return -1;
    uint8_t buf[40];                   /* seed(<=32) || nonce(8) */
    memcpy(buf, seed, seedlen);
    for (int i = 0; i < 8; i++)
        buf[seedlen + i] = (uint8_t)(nonce >> (8 * (7 - i)));
    return q_crypto_sm3(buf, seedlen + 8, hash);
}

int q_sec_pow_solve(const uint8_t *seed, size_t seedlen, int difficulty, uint64_t *nonce_out)
{
    if (!seed || seedlen == 0 || seedlen > 32 || !nonce_out || difficulty <= 0 || difficulty > 256)
        return -1;
    uint8_t hash[32];
    uint64_t nonce = 0;
    for (;;)
    {
        if (pow_inner(seed, seedlen, difficulty, nonce, hash) != 0)
            return -1;
        if (leading_zero_bits(hash, difficulty) == difficulty)
        {
            *nonce_out = nonce;
            return 0;
        }
        nonce++;
        if (nonce == 0)
            return -1;                 /* 搜遍 2^64 仍未命中（极端） */
    }
}

int q_sec_pow_verify(const uint8_t *seed, size_t seedlen, int difficulty, uint64_t nonce)
{
    if (!seed || seedlen == 0 || seedlen > 32 || difficulty <= 0 || difficulty > 256)
        return -1;
    uint8_t hash[32];
    if (pow_inner(seed, seedlen, difficulty, nonce, hash) != 0)
        return -1;
    return (leading_zero_bits(hash, difficulty) == difficulty) ? 1 : 0;
}
