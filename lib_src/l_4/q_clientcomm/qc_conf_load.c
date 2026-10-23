/**
 * @file qc_conf_load.c
 * @brief qc_conf_load —— 加载 XML 客户端配置（基于 l_1/q_xcfg）
 *
 * 取值策略：**节点全可选，缺失即默认**。
 * 这样配置文件可以只写差异项（运维友好），但代价是"键名写错"和"节点没写"
 * 表现完全一样 —— 故凡是关键项走了默认值，都在 flags 里置 QC_CONF_F_DEFAULTED，
 * 让 UI 有机会提示"你配的 host 好像没生效"，而不是静默连到默认值上。
 */
#include "qc_client.h"
#include "qc_conf.h"

#include "q_xcfg.h"   /* l_1 XML 配置库（点分键 -> XPath） */

#include <stdio.h>
#include <stdlib.h>

int qc_conf_load(const char *path, qc_client_conf_t *out)
{
    char          resolved[QC_PATH_MAX];
    FILE         *probe = NULL;
    q_xcfg_ctx_t *x     = NULL;
    char         *s     = NULL;
    int           v     = 0;

    if (!out)
        return -1;

    /* 未给路径 -> 走三端默认路径（含 VMS_CLIENT_CONF 环境变量） */
    if (!path || path[0] == '\0')
    {
        if (qc_conf_default_path(resolved, sizeof(resolved)) != 0)
        {
            (void)qc_conf_default(out);
            return -2;
        }
        path = resolved;
    }

    (void)qc_conf_default(out);

    /* 先探测文件是否存在：libxml2 对"文件不存在"和"XML 语法错"都只返回 NULL，
       不先区分就会把路径写错报成"XML 解析失败"，排查方向直接偏 90 度。 */
    probe = fopen(path, "rb");
    if (!probe)
        return -2;
    fclose(probe);

    x = q_xcfg_init(path);
    if (!x)
        return -3;

    qc_conf_copy(out->source_path, sizeof(out->source_path), path);

    /* ---------------- server ---------------- */
    s = q_xcfg_get_string(x, QC_K_HOST, NULL);
    if (s && s[0])
        qc_conf_copy(out->server.host, sizeof(out->server.host), s);
    else
        out->flags |= QC_CONF_F_DEFAULTED;
    free(s);

    v = q_xcfg_get_int(x, QC_K_PORT, 0);
    out->server.port = (uint16_t)qc_conf_u32(v, QC_DEF_PORT);
    if (v <= 0)
        out->flags |= QC_CONF_F_DEFAULTED;

    s = q_xcfg_get_string(x, QC_K_PUBKEY, NULL);
    if (s && s[0])
        qc_conf_copy(out->server.pubkey_path, sizeof(out->server.pubkey_path), s);
    else
        out->flags |= QC_CONF_F_DEFAULTED;
    free(s);

    v = q_xcfg_get_int(x, QC_K_SEQ0, 0);
    out->server.seq0 = (uint64_t)qc_conf_u32(v, QC_DEF_SEQ0);

    v = q_xcfg_get_int(x, QC_K_KEY_ID, 0);
    out->server.key_id = (uint16_t)qc_conf_u32(v, QC_DEF_KEY_ID);

    /* ---------------- auth ---------------- */
    s = q_xcfg_get_string(x, QC_K_USER, NULL);
    qc_conf_copy(out->auth.user, sizeof(out->auth.user), s);
    free(s);

    s = q_xcfg_get_string(x, QC_K_PASSWORD, NULL);
    if (s && s[0])
    {
        qc_conf_copy(out->auth.password, sizeof(out->auth.password), s);
        /* 明文口令落盘：等保三级 / 密评下是不合格项。不阻断（保住联调与自动化），
           但必须留下痕迹，避免"没人知道口令其实躺在一个 xml 里"。 */
        out->flags |= QC_CONF_F_PLAIN_PASSWORD;
        fprintf(stderr,
                "[qc_conf] 警告：配置文件 %s 含明文口令，生产环境请留空并改用登录框输入"
                "（口令须由系统凭据管理器保管）\n",
                path);
    }
    free(s);

    s = q_xcfg_get_string(x, QC_K_OTP, NULL);
    qc_conf_copy(out->auth.otp, sizeof(out->auth.otp), s);
    free(s);

    s = q_xcfg_get_string(x, QC_K_DEVID_HEX, NULL);
    qc_conf_copy(out->auth.devid_hex, sizeof(out->auth.devid_hex), s);
    free(s);

    v = q_xcfg_get_int(x, QC_K_PBKDF2_ITER, 0);
    out->auth.pbkdf2_iter = qc_conf_u32(v, QC_DEF_PBKDF2_ITER);

    /* ---------------- knock ---------------- */
    s = q_xcfg_get_string(x, QC_K_KNOCK_EP, NULL);
    qc_conf_copy(out->knock.endpoint, sizeof(out->knock.endpoint), s);
    free(s);

    s = q_xcfg_get_string(x, QC_K_KNOCK_KEY, NULL);
    qc_conf_copy(out->knock.spa_key_path, sizeof(out->knock.spa_key_path), s);
    free(s);

    out->knock.req_port = (uint16_t)qc_conf_u32(q_xcfg_get_int(x, QC_K_KNOCK_PORT, 0), 0);
    out->knock.req_ttl  = (uint16_t)qc_conf_u32(q_xcfg_get_int(x, QC_K_KNOCK_TTL, 0),
                                                QC_DEF_KNOCK_TTL);
    out->knock.key_id   = (uint8_t)qc_conf_u32(q_xcfg_get_int(x, QC_K_KNOCK_KEYID, 0), 0);

    /* ---------------- keeper ---------------- */
    out->keeper.heartbeat_ms    = qc_conf_u32(q_xcfg_get_int(x, QC_K_HB_MS, 0), QC_DEF_HB_MS);
    out->keeper.backoff_base_ms = qc_conf_u32(q_xcfg_get_int(x, QC_K_BO_BASE_MS, 0),
                                              QC_DEF_BO_BASE_MS);
    out->keeper.backoff_max_ms  = qc_conf_u32(q_xcfg_get_int(x, QC_K_BO_MAX_MS, 0),
                                              QC_DEF_BO_MAX_MS);
    /* max_retry = 0 是合法值（无限重试），不能用 qc_conf_u32 兜默认 */
    v = q_xcfg_get_int(x, QC_K_MAX_RETRY, -1);
    out->keeper.max_retry = (v >= 0) ? (uint32_t)v : QC_DEF_MAX_RETRY;
    /* auto_start = 0 同样合法（显式关闭） */
    v = q_xcfg_get_int(x, QC_K_AUTO_START, -1);
    out->keeper.auto_start = (v >= 0) ? (uint32_t)v : QC_DEF_AUTO_START;

    /* ---------------- app ---------------- */
    s = q_xcfg_get_string(x, QC_K_CVER, NULL);
    if (s && s[0])
        qc_conf_copy(out->app.client_version, sizeof(out->app.client_version), s);
    free(s);

    s = q_xcfg_get_string(x, QC_K_LOG_LEVEL, NULL);
    if (s && s[0])
        qc_conf_copy(out->app.log_level, sizeof(out->app.log_level), s);
    free(s);

    out->app.connect_timeout_ms = qc_conf_u32(q_xcfg_get_int(x, QC_K_CONN_TIMEOUT, 0),
                                              QC_DEF_CONN_TIMEOUT_MS);

    /* ---------------- push（M5，全可选；缺失即保持默认的空/0 = 回退 server.*） ----------------
     * 注意：push 段缺失**不**置 QC_CONF_F_DEFAULTED —— 它本来就是可选增强，
     * 单栈部署（推送与命令同实例）下整段不写是正常形态，不应误导 UI 报警。 */
    s = q_xcfg_get_string(x, QC_K_PUSH_HOST, NULL);
    if (s && s[0])
        qc_conf_copy(out->push.host, sizeof(out->push.host), s);
    free(s);

    v = q_xcfg_get_int(x, QC_K_PUSH_PORT, 0);
    out->push.port = (uint16_t)qc_conf_u32(v, QC_DEF_PUSH_PORT);

    s = q_xcfg_get_string(x, QC_K_PUSH_KNOCK, NULL);
    if (s && s[0])
        qc_conf_copy(out->push.knock, sizeof(out->push.knock), s);
    free(s);

    q_xcfg_destroy(x);
    return 0;
}
