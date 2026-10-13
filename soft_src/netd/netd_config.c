/**
 * @file netd_config.c
 * @brief netd 配置加载：XML（q_xcfg）-> g_cfg；并加载服务端 SM2 长期密钥对
 */
#include "srv_netd.h"

netd_cfg_t g_cfg;

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

/* 读取整个文件到 malloc 缓冲；失败返回 -1 且不改动 out / outlen */
static int read_file_all(const char *path, uint8_t **out, size_t *outlen)
{
    FILE   *f;
    long    sz;
    uint8_t *buf;

    if (!path || !*path || !out || !outlen)
        return -1;

    f = fopen(path, "rb");
    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return -1;
    }
    sz = ftell(f);
    if (sz < 0)
    {
        fclose(f);
        return -1;
    }
    rewind(f);

    buf = (uint8_t *)malloc((size_t)sz + 1u);
    if (!buf)
    {
        fclose(f);
        return -1;
    }
    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz)
    {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    *out    = buf;
    *outlen = (size_t)sz;
    return 0;
}

void cfg_defaults(void)
{
    g_cfg.listen_port  = NETD_PORT_DEF;
    g_cfg.backlog      = NETD_BACKLOG_DEF;
    g_cfg.nworkers     = NETD_NWORKERS_DEF;
    g_cfg.max_queue    = NETD_MAXQ_DEF;
    g_cfg.poll_ms      = NETD_POLL_MS_DEF;
    g_cfg.key_id       = (uint16_t)NETD_KEY_ID_DEF;
    g_cfg.log_level    = (int)Q_LOG_INFO;
    g_cfg.log_console  = 1;
    g_cfg.log_max_size = 10u * 1024u * 1024u;
    g_cfg.enable_ipv6  = 0;
    snprintf(g_cfg.ipc_path,  sizeof(g_cfg.ipc_path),  "%s", NETD_IPC_PATH_DEF);
    snprintf(g_cfg.priv_file, sizeof(g_cfg.priv_file), "%s", NETD_PRIV_FILE_DEF);
    snprintf(g_cfg.pub_file,  sizeof(g_cfg.pub_file),  "%s", NETD_PUB_FILE_DEF);
    snprintf(g_cfg.log_file,  sizeof(g_cfg.log_file),  "%s", NETD_LOG_FILE_DEF);
}

int load_config(const char *path)
{
    q_xcfg_ctx_t *xc = q_xcfg_init(path);
    char *v;

    if (!xc)
    {
        fprintf(stderr, "[netd] 配置文件打开失败: %s（回退默认值）\n", path ? path : "(null)");
        return -1;
    }

    g_cfg.listen_port = q_xcfg_get_int(xc, "config.netd.listen_port", g_cfg.listen_port);
    g_cfg.backlog     = q_xcfg_get_int(xc, "config.netd.backlog",    g_cfg.backlog);
    g_cfg.nworkers    = q_xcfg_get_int(xc, "config.netd.nworkers",   g_cfg.nworkers);
    g_cfg.max_queue   = q_xcfg_get_int(xc, "config.netd.max_queue",  g_cfg.max_queue);
    g_cfg.poll_ms     = q_xcfg_get_int(xc, "config.netd.poll_timeout_ms", g_cfg.poll_ms);
    g_cfg.key_id      = (uint16_t)q_xcfg_get_int(xc, "config.netd.key_id", (int)g_cfg.key_id);
    g_cfg.enable_ipv6 = q_xcfg_get_bool(xc, "config.netd.enable_ipv6", false) ? 1 : 0;
    g_cfg.log_console = q_xcfg_get_bool(xc, "config.netd.log_console", true) ? 1 : 0;

    v = q_xcfg_get_string(xc, "config.netd.ipc_path", g_cfg.ipc_path);
    if (v) { snprintf(g_cfg.ipc_path, sizeof(g_cfg.ipc_path), "%s", v); free(v); }

    v = q_xcfg_get_string(xc, "config.netd.sm2_priv_file", g_cfg.priv_file);
    if (v) { snprintf(g_cfg.priv_file, sizeof(g_cfg.priv_file), "%s", v); free(v); }

    v = q_xcfg_get_string(xc, "config.netd.sm2_pub_file", g_cfg.pub_file);
    if (v) { snprintf(g_cfg.pub_file, sizeof(g_cfg.pub_file), "%s", v); free(v); }

    v = q_xcfg_get_string(xc, "config.netd.log_file", g_cfg.log_file);
    if (v) { snprintf(g_cfg.log_file, sizeof(g_cfg.log_file), "%s", v); free(v); }

    v = q_xcfg_get_string(xc, "config.netd.log_level", "info");
    if (v) { g_cfg.log_level = (int)parse_log_level(v); free(v); }

    q_xcfg_destroy(xc);
    return 0;
}

int load_sm2_keypair(netd_ctx_t *g)
{
    if (read_file_all(g_cfg.priv_file, &g->sm2_priv, &g->sm2_priv_len) != 0)
    {
        fprintf(stderr, "[netd] 读取 SM2 私钥失败: %s\n", g_cfg.priv_file);
        return -1;
    }
    if (read_file_all(g_cfg.pub_file, &g->sm2_pub, &g->sm2_pub_len) != 0)
    {
        fprintf(stderr, "[netd] 读取 SM2 公钥失败: %s\n", g_cfg.pub_file);
        return -1;
    }
    if (g->sm2_priv_len == 0 || g->sm2_pub_len == 0)
    {
        fprintf(stderr, "[netd] SM2 密钥文件为空\n");
        return -1;
    }
    return 0;
}
