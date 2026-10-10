/**
 * @file q_xcfg_ut.c
 * @brief q_xcfg 单元测试小程序（构建后由 make ut 运行）
 */
#include "headers.h"
#include "q_xcfg.h"

#include <stdlib.h>
#include <string.h>

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
    q_xcfg_ctx_t *ctx = q_xcfg_init("ut/sample.xml");
    CHECK(ctx != NULL, "q_xcfg_init 解析 sample.xml 成功");

    if (!ctx)
        return 1;

    CHECK(q_xcfg_get_int(ctx, "config.gateway.port", 0) == 8443, "get_int gateway.port == 8443");
    CHECK(q_xcfg_get_bool(ctx, "config.gateway.enabled", false) == true, "get_bool gateway.enabled == true");
    CHECK(q_xcfg_get_float(ctx, "config.gateway.ratio", 0.0) == 0.75, "get_float gateway.ratio == 0.75");

    char *host = q_xcfg_get_string(ctx, "config.gateway.host", NULL);
    CHECK(host != NULL && strcmp(host, "localhost") == 0, "get_string gateway.host == localhost");
    free(host);

    CHECK(q_xcfg_has_key(ctx, "config.gateway.host") == true, "has_key 存在键");
    CHECK(q_xcfg_has_key(ctx, "config.nonexist") == false, "has_key 不存在键返回 false");
    CHECK(q_xcfg_get_int(ctx, "config.nonexist", 42) == 42, "缺失键返回默认值");

    q_xcfg_dump(ctx);
    q_xcfg_destroy(ctx);

    if (g_fail == 0)
        printf("\nALL PASS\n");
    else
        printf("\n%d FAILED\n", g_fail);

    return g_fail == 0 ? 0 : 1;
}
