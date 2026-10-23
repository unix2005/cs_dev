/**
 * @file qc_conf_default.c
 * @brief qc_conf_default —— 填充出厂默认值（不读文件）
 */
#include "qc_client.h"
#include "qc_conf.h"

int qc_conf_default(qc_client_conf_t *c)
{
    if (!c)
        return -1;

    memset(c, 0, sizeof(*c));

    c->server.port   = (uint16_t)QC_DEF_PORT;
    c->server.seq0   = (uint64_t)QC_DEF_SEQ0;
    c->server.key_id = (uint16_t)QC_DEF_KEY_ID;

    c->auth.pbkdf2_iter = QC_DEF_PBKDF2_ITER;

    c->knock.req_ttl = (uint16_t)QC_DEF_KNOCK_TTL;

    c->keeper.heartbeat_ms    = QC_DEF_HB_MS;
    c->keeper.backoff_base_ms = QC_DEF_BO_BASE_MS;
    c->keeper.backoff_max_ms  = QC_DEF_BO_MAX_MS;
    c->keeper.max_retry       = QC_DEF_MAX_RETRY;
    c->keeper.auto_start      = QC_DEF_AUTO_START;

    qc_conf_copy(c->app.client_version, sizeof(c->app.client_version), QC_DEF_CVER);
    qc_conf_copy(c->app.log_level, sizeof(c->app.log_level), QC_DEF_LOG_LEVEL);
    c->app.connect_timeout_ms = QC_DEF_CONN_TIMEOUT_MS;

    /* M5：push 段默认全空/0 —— 语义即"回退用 server.* / knock.*"，
       不填 host 就不会误连到一个不存在的推送栈。 */
    c->push.port = (uint16_t)QC_DEF_PUSH_PORT;

    c->flags = QC_CONF_F_NONE;
    return 0;
}
