/**
 * @file q_mem_ut.c
 * @brief q_mem 库单元测试 / 冒烟小程序（ut/）
 * @note  编译：见模块 Makefile 的 `make ut` 目标（自动链接 libqmem）
 *        覆盖：安全清零、安全释放、固定块内存池、动态缓冲区。
 */
#include "headers.h"
#include "q_mem.h"
#include <stdlib.h>

static int g_fail = 0;

#define CHECK(cond, msg)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            printf("  [FAIL] %s\n", msg);                                                                              \
            g_fail++;                                                                                                  \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            printf("  [ OK ] %s\n", msg);                                                                              \
        }                                                                                                              \
    } while (0)

static int mem_all_zero(const unsigned char *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (p[i] != 0)
            return 0;
    return 1;
}

int main(void)
{
    printf("=== q_mem 单元测试 ===\n");

    /* 1. 安全清零 */
    unsigned char secret[32];
    size_t i;
    for (i = 0; i < sizeof(secret); i++)
        secret[i] = (unsigned char)(i + 1);
    q_mem_zero(secret, sizeof(secret));
    CHECK(mem_all_zero(secret, sizeof(secret)), "q_mem_zero 全部清零");
    q_mem_zero(NULL, 10);  /* NULL 安全 */
    q_mem_zero(secret, 0); /* 0 长度安全 */

    /* 2. 安全释放（仅验证不崩溃：先填 0xFF 再擦除释放） */
    unsigned char *p = (unsigned char *)malloc(64);
    for (i = 0; i < 64; i++)
        p[i] = 0xFF;
    q_mem_secure_free(p, 64);

    /* 3. 内存池：创建 10 字节块 x 4（10 -> 对齐到 16） */
    q_mempool_t *mp = q_mempool_create(10, 4);
    CHECK(mp != NULL, "q_mempool_create 成功");
    CHECK(q_mempool_avail(mp) == 4, "初始空闲块数=4");
    CHECK(mp->block_size == 16, "块大小对齐到 16");

    void *b0 = q_mempool_alloc(mp);
    void *b1 = q_mempool_alloc(mp);
    void *b2 = q_mempool_alloc(mp);
    void *b3 = q_mempool_alloc(mp);
    CHECK(b0 && b1 && b2 && b3, "分配 4 块成功");
    CHECK(q_mempool_avail(mp) == 0, "分配后空闲=0");
    CHECK(q_mempool_alloc(mp) == NULL, "池满返回 NULL");

    /* 写入敏感数据后 clear 擦除 */
    for (i = 0; i < 16; i++)
        ((unsigned char *)b0)[i] = 0xAA;
    q_mempool_clear(mp);
    CHECK(mem_all_zero((unsigned char *)b0, 16), "q_mempool_clear 擦除已分配块");

    /* 归还一块再分配，复用块内容应已被擦除 */
    q_mempool_free(mp, b3);
    CHECK(q_mempool_avail(mp) == 1, "归还后空闲=1");
    void *b3b = q_mempool_alloc(mp);
    CHECK(b3b == b3, "再次分配到同一块");
    CHECK(mem_all_zero((unsigned char *)b3b, 16), "复用块内容已擦除");

    /* 非法/越界指针归还被安全忽略（空闲数不变） */
    size_t avail_before = q_mempool_avail(mp); /* 此时池满，=0 */
    q_mempool_free(mp, (void *)0x1);           /* 越界指针 */
    q_mempool_free(mp, mp->base + 1);          /* 未对齐指针 */
    CHECK(q_mempool_avail(mp) == avail_before, "非法指针归还被忽略");

    /* 已分配块正常归还：空闲数 +1（b3 经 alloc 取出后此处归还） */
    q_mempool_free(mp, b3);
    CHECK(q_mempool_avail(mp) == avail_before + 1, "已分配块归还成功");

    q_mempool_destroy(mp); /* 销毁整体擦除+释放 */

    /* 4. 动态缓冲区 */
    q_buf_t *buf = q_buf_create(0);
    CHECK(buf != NULL, "q_buf_create 成功");
    const char *msg = "hello-qmem";
    CHECK(q_buf_append(buf, msg, strlen(msg)) == 0, "q_buf_append 成功");
    CHECK(q_buf_len(buf) == strlen(msg), "q_buf_len 正确");
    CHECK(memcmp(q_buf_data(buf), msg, strlen(msg)) == 0, "q_buf_data 内容正确");

    /* 触发自动增长 */
    unsigned char big[200];
    for (i = 0; i < sizeof(big); i++)
        big[i] = (unsigned char)i;
    CHECK(q_buf_append(buf, big, sizeof(big)) == 0, "q_buf_append 增长成功");
    CHECK(q_buf_len(buf) == strlen(msg) + sizeof(big), "增长后长度正确");

    /* 清空擦除 */
    q_buf_clear(buf);
    CHECK(q_buf_len(buf) == 0, "q_buf_clear 长度归零");
    CHECK(mem_all_zero(buf->data, buf->cap), "q_buf_clear 整段容量已擦除");

    q_buf_destroy(buf);

    printf("=== 结果：%s ===\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURE");
    return g_fail == 0 ? 0 : 1;
}
