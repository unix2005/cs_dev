/**
 * @file q_spa_ut.c
 * @brief q_spa 单元测试：构造 -> 校验 往返 + 篡改失败
 *        依赖 Tongsuo（q_crypto），需在 Linux 目标平台编译运行。
 */
#include "headers.h"
#include "q_spa.h"
#include "q_rand.h"
#include <string.h>

int main(void)
{
    uint8_t key[16];
    if (q_rand_bytes(key, sizeof(key)) != 0)
    {
        printf("FAIL: rand\n");
        return 1;
    }

    uint8_t uid[Q_SPA_USERID_LEN];
    memset(uid, 0xAB, sizeof(uid));
    uint8_t hint[16];
    memset(hint, 0, sizeof(hint));

    q_spa_pkt_t pkt;
    if (q_spa_build(key, sizeof(key), 0, uid, 9000, 300, hint, &pkt) != 0)
    {
        printf("FAIL: build\n");
        return 1;
    }

    uint16_t port = 0, ttl = 0;
    if (q_spa_verify(key, sizeof(key), &pkt, (uint64_t)time(NULL), &port, &ttl) != 0)
    {
        printf("FAIL: verify\n");
        return 1;
    }
    if (port != 9000 || ttl != 300)
    {
        printf("FAIL: fields\n");
        return 1;
    }

    /* 篡改 tag -> 验签失败 */
    q_spa_pkt_t bad = pkt;
    bad.tag[0] ^= 0xFF;
    if (q_spa_verify(key, sizeof(key), &bad, (uint64_t)time(NULL), &port, &ttl) == 0)
    {
        printf("FAIL: tamper not detected\n");
        return 1;
    }

    printf("q_spa_ut: PASS\n");
    return 0;
}
