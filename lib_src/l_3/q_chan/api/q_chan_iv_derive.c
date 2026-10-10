/**
 * @file q_chan_iv_derive.c
 * @brief q_chan_iv_derive —— 由 nonce_base 与 seq 派生 SM4-GCM IV
 *
 * IV = nonce_base XOR (seq 零扩展至 12 字节，低 8 字节承载 seq 大端)。
 * 严禁随机生成 IV（GCM nonce 重用属灾难性失效）；seq 单调递增保证 IV 唯一。
 */
#include "headers.h"
#include "q_chan.h"
#include "q_chan_int.h"

void q_chan_iv_derive(const uint8_t *nonce_base, uint64_t seq, uint8_t iv[Q_CHAN_IV_LEN])
{
    for (int i = 0; i < Q_CHAN_IV_LEN; i++)
        iv[i] = nonce_base[i];
    for (int i = 0; i < 8; i++)
        iv[4 + i] ^= (uint8_t)((seq >> (8 * (7 - i))) & 0xff);
}
