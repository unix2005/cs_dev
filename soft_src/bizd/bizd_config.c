/**
 * @file bizd_config.c
 * @brief bizd 配置加载：XML（q_xcfg）-> g_bcfg
 */
#include "srv_bizd.h"

bizd_cfg_t g_bcfg;

static q_log_level_t parse_log_level(const char *s)
{
    if (!s || !*s)
        return Q_LOG_INFO;
    if (strcmp(s, "trace") == 0)
        return Q_LOG_TRACE;
    if (strcmp(s, "debug") == 0)
        return Q_LOG_DEBUG;
    if (strcmp(s, "info") == 0)
        return Q_LOG_INFO;
    if (strcmp(s, "warn") == 0)
        return Q_LOG_WARN;
    if (strcmp(s, "error") == 0)
        return Q_LOG_ERROR;
    return Q_LOG_INFO;
}

void bizd_cfg_defaults(void)
{
    g_bcfg.backlog     = BIZD_BACKLOG_DEF;
    g_bcfg.nworkers    = BIZD_NWORKERS_DEF;
    g_bcfg.max_queue   = BIZD_MAXQ_DEF;
    g_bcfg.poll_ms    = BIZD_POLL_MS_DEF;
    g_bcfg.log_level   = (int)Q_LOG_INFO;
    g_bcfg.log_console = 1;
    g_bcfg.log_max_size = 10u * 1024u * 1024u;

    snprintf(g_bcfg.ipc_path, sizeof(g_bcfg.ipc_path), "%s", BIZD_IPC_PATH_DEF);
    snprintf(g_bcfg.log_file, sizeof(g_bcfg.log_file), "%s", BIZD_LOG_FILE_DEF);

    snprintf(g_bcfg.ep_hello.host, sizeof(g_bcfg.ep_hello.host), "%s", BIZD_EP_HELLO_HOST_DEF);
    g_bcfg.ep_hello.port = BIZD_EP_HELLO_PORT_DEF;
    snprintf(g_bcfg.ep_auth.host,  sizeof(g_bcfg.ep_auth.host),  "%s", BIZD_EP_AUTH_HOST_DEF);
    g_bcfg.ep_auth.port  = BIZD_EP_AUTH_PORT_DEF;
    snprintf(g_bcfg.ep_biz.host,   sizeof(g_bcfg.ep_biz.host),   "%s", BIZD_EP_BIZ_HOST_DEF);
    g_bcfg.ep_biz.port   = BIZD_EP_BIZ_PORT_DEF;
    snprintf(g_bcfg.ep_video.host, sizeof(g_bcfg.ep_video.host), "%s", BIZD_EP_VIDEO_HOST_DEF);
    g_bcfg.ep_video.port = BIZD_EP_VIDEO_PORT_DEF;
}

int bizd_load_config(const char *path)
{
    q_xcfg_ctx_t *xc = q_xcfg_init(path);
    char *v;

    if (!xc)
    {
        fprintf(stderr, "[bizd] 配置文件打开失败: %s（回退默认值）\n", path ? path : "(null)");
        return -1;
    }

    g_bcfg.backlog   = q_xcfg_get_int(xc, "config.bizd.backlog",   g_bcfg.backlog);
    g_bcfg.nworkers  = q_xcfg_get_int(xc, "config.bizd.nworkers",  g_bcfg.nworkers);
    g_bcfg.max_queue = q_xcfg_get_int(xc, "config.bizd.max_queue", g_bcfg.max_queue);
    g_bcfg.poll_ms  = q_xcfg_get_int(xc, "config.bizd.poll_timeout_ms", g_bcfg.poll_ms);
    g_bcfg.log_console = q_xcfg_get_bool(xc, "config.bizd.log_console", true) ? 1 : 0;

    v = q_xcfg_get_string(xc, "config.bizd.ipc_path", g_bcfg.ipc_path);
    if (v) { snprintf(g_bcfg.ipc_path, sizeof(g_bcfg.ipc_path), "%s", v); free(v); }

    v = q_xcfg_get_string(xc, "config.bizd.log_file", g_bcfg.log_file);
    if (v) { snprintf(g_bcfg.log_file, sizeof(g_bcfg.log_file), "%s", v); free(v); }

    v = q_xcfg_get_string(xc, "config.bizd.log_level", "info");
    if (v) { g_bcfg.log_level = (int)parse_log_level(v); free(v); }

    q_xcfg_destroy(xc);
    return 0;
}
