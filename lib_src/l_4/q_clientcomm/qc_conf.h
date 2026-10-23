/**
 * @file qc_conf.h
 * @brief l_4/q_clientcomm 【内部头】：客户端 XML 配置（不随 install_hdr 归集到 include/）
 * @platform Windows / Linux / macOS
 *
 * 职责：只放「键名常量」与「取值小工具」，供 qc_conf_*.c 共用。
 * 键名集中在一处，是因为它**就是跨端契约**：任何一端改了 XML 里一个标签名，
 * 另外两端会静默走默认值（不报错、不告警，只是"配了没生效"）——
 * 这类问题只能靠"键名只有一份定义"来防。
 */
#ifndef QC_CONF_H
#define QC_CONF_H

#include "qc_client.h"

/* ------------------------------- XML 键（点分路径） -------------------------------
 * q_xcfg 会把 "a.b.c" 转成 XPath "/a/b/c/text()" 求值，故此处写的是完整路径。
 */
#define QC_K_HOST          "vms_client.server.host"
#define QC_K_PORT          "vms_client.server.port"
#define QC_K_PUBKEY        "vms_client.server.pubkey_path"
#define QC_K_SEQ0          "vms_client.server.seq0"
#define QC_K_KEY_ID        "vms_client.server.key_id"

#define QC_K_USER          "vms_client.auth.user"
#define QC_K_PASSWORD      "vms_client.auth.password"
#define QC_K_OTP           "vms_client.auth.otp"
#define QC_K_DEVID_HEX     "vms_client.auth.devid_hex"
#define QC_K_PBKDF2_ITER   "vms_client.auth.pbkdf2_iter"

#define QC_K_KNOCK_EP      "vms_client.knock.endpoint"
#define QC_K_KNOCK_KEY     "vms_client.knock.spa_key_path"
#define QC_K_KNOCK_PORT    "vms_client.knock.req_port"
#define QC_K_KNOCK_TTL     "vms_client.knock.req_ttl"
#define QC_K_KNOCK_KEYID   "vms_client.knock.key_id"

#define QC_K_HB_MS         "vms_client.keeper.heartbeat_ms"
#define QC_K_BO_BASE_MS    "vms_client.keeper.backoff_base_ms"
#define QC_K_BO_MAX_MS     "vms_client.keeper.backoff_max_ms"
#define QC_K_MAX_RETRY     "vms_client.keeper.max_retry"
#define QC_K_AUTO_START    "vms_client.keeper.auto_start"

#define QC_K_CVER          "vms_client.app.client_version"
#define QC_K_LOG_LEVEL     "vms_client.app.log_level"
#define QC_K_CONN_TIMEOUT  "vms_client.app.connect_timeout_ms"

/* M5 推送段（三条全部可选；缺省回退 server.* / knock.*，兼容单栈部署） */
#define QC_K_PUSH_HOST     "vms_client.push.host"
#define QC_K_PUSH_PORT     "vms_client.push.port"
#define QC_K_PUSH_KNOCK    "vms_client.push.knock"

/* 环境变量：显式指定配置文件路径（三端同名，联调/多实例用） */
#define QC_ENV_CONF_PATH   "VMS_CLIENT_CONF"

/* ------------------------------- 默认值 ------------------------------- */
#define QC_DEF_PORT            18099u
#define QC_DEF_SEQ0            1u
#define QC_DEF_KEY_ID          1u
#define QC_DEF_PBKDF2_ITER     0u        /* 0 = 取协议默认 Q_PBKDF2_ITER_DEFAULT */
#define QC_DEF_KNOCK_TTL       300u
#define QC_DEF_HB_MS           60000u
#define QC_DEF_BO_BASE_MS      1000u
#define QC_DEF_BO_MAX_MS       30000u
#define QC_DEF_MAX_RETRY       0u        /* 0 = 无限重试，不退到 OFFLINE */
#define QC_DEF_AUTO_START      1u
#define QC_DEF_CONN_TIMEOUT_MS 5000u
#define QC_DEF_CVER            "1.1.0"
#define QC_DEF_LOG_LEVEL       "info"
#define QC_DEF_PUSH_PORT       0u        /* 0 = 回退用 server.port */

/* 口令派生迭代下限：低于此值视为配置错误（彩虹表可承受） */
#define QC_MIN_PBKDF2_ITER     100000u

/* ------------------------------- 小工具 ------------------------------- */
#include <string.h>

/* 截断安全拷贝：源为空则清空；过长则截断且保证 NUL 结尾（绝不越界）。 */
static inline void qc_conf_copy(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0u)
        return;
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= cap)
        n = cap - 1u;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* 取非零正整数（配置里写 0/负数/垃圾时保留默认）。 */
static inline uint32_t qc_conf_u32(int v, uint32_t def)
{
    return (v > 0) ? (uint32_t)v : def;
}

#endif /* QC_CONF_H */
