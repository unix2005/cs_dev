/**
 * @file qc_conf_free.c
 * @brief qc_conf_free —— 清零敏感字段并整体归零
 *
 * 口令与 OTP 是内存里的高价值目标：结构体会被 UI 长期持有一份（重登时要用），
 * 直接 memset(0) 整个结构体虽然也清零了，但先单独清敏感字段是为了
 * 让"哪里清了口令"在代码里可读 —— 合规评审时要能指出这一行。
 */
#include "qc_client.h"
#include "qc_conf.h"

void qc_conf_free(qc_client_conf_t *c)
{
    if (!c)
        return;

    /* 先单独清敏感字段（口令 / OTP），再整体归零 */
    memset(c->auth.password, 0, sizeof(c->auth.password));
    memset(c->auth.otp, 0, sizeof(c->auth.otp));

    memset(c, 0, sizeof(*c));
}
