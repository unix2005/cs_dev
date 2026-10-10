/**
 * @file q_rand_ut.c
 * @brief q_rand 单元测试小程序（构建后由 make ut 运行）
 */
#include "headers.h"
#include "q_rand.h"

static int g_fail = 0;

#define CHECK(cond, msg)                              \
    do                                                \
    {                                                 \
        if (!(cond))                                  \
        {                                             \
            printf("FAIL: %s\n", msg);                \
            g_fail++;                                 \
        }                                             \
        else                                          \
        {                                             \
            printf("PASS: %s\n", msg);               \
        }                                             \
    } while (0)

int main(void)
{
    unsigned char buf[32];
    CHECK(q_rand_bytes(buf, sizeof(buf)) == 0, "rand_bytes 32 bytes");

    int nonzero = 0;
    for (int i = 0; i < 32; i++)
    {
        if (buf[i])
        {
            nonzero = 1;
            break;
        }
    }
    CHECK(nonzero == 1, "rand_bytes produced non-zero data");

    uint32_t a = q_rand_u32();
    uint32_t b = q_rand_u32();
    CHECK(a != b || q_rand_u32() != a, "rand_u32 varies");

    uint64_t u1 = q_rand_u64();
    uint64_t u2 = q_rand_u64();
    CHECK(u1 != u2 || q_rand_u64() != u1, "rand_u64 varies");

    int in_range = 1;
    for (int i = 0; i < 1000; i++)
    {
        int v = q_rand_range(5, 10);
        if (v < 5 || v > 10)
        {
            in_range = 0;
            break;
        }
    }
    CHECK(in_range == 1, "rand_range within [5,10]");
    CHECK(q_rand_range(7, 7) == 7, "rand_range degenerate -> 7");

    if (g_fail == 0)
        printf("\nALL PASS\n");
    else
        printf("\n%d FAILED\n", g_fail);

    return g_fail == 0 ? 0 : 1;
}
