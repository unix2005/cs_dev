/**
 * @file biz_handler.c
 * @brief 业务微服务请求处理：解析 bizd JSON 信封 -> 按 biz_type 取数 -> 生成响应 JSON
 *
 * M2 范围：只打通 BIZ_QUERY(0x0020) 的 stats 子类型；
 *          companies / vehicles 在 M3 补齐（当前返回 Q_IPC_R_UNSUPPORTED）。
 *
 * 鉴权：本服务只做"Token 非空且非全零"的桩校验。
 *       真实部署应校验 (token, channel_id, user_id, src_ip) 四元绑定并查会话存储，
 *       见《win_vms_v1与后台交互调用方案.md》§4 与技术方案 §6.3。
 */
#include "srv_biz.h"

/* 构造响应 JSON：{"ver":1,"id":..,"result":..,"message":..,"data":{..}}（调用方 free） */
static char *build_rsp(const char *id, uint32_t result, const char *msg, cJSON *data)
{
    cJSON *root;
    char  *s;

    root = cJSON_CreateObject();
    if (!root)
    {
        if (data) cJSON_Delete(data);
        return NULL;
    }
    cJSON_AddNumberToObject(root, "ver", 1);
    cJSON_AddStringToObject(root, "id", id ? id : "");
    cJSON_AddNumberToObject(root, "result", (double)result);
    cJSON_AddStringToObject(root, "message", msg ? msg : "");
    cJSON_AddItemToObject(root, "data", data ? data : cJSON_CreateObject());

    s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

/* token 是否有效（桩）：存在且非全零。全零意味着 netd 没把会话 Token 透传下来，
   或客户端根本没登录 —— 两者都必须拒绝，否则鉴权形同虚设。 */
static int token_ok(const cJSON *root)
{
    cJSON      *t;
    const char *s;

    if (!root)
        return 0;
    t = cJSON_GetObjectItem(root, "token");
    if (!t || !cJSON_IsString(t) || !t->valuestring || !t->valuestring[0])
        return 0;
    for (s = t->valuestring; *s; s++)
        if (*s != '0')
            return 1;
    return 0;
}

/* 取 params.biz_type（文本，无内嵌 NUL 由 bizd 侧已过滤） */
static const char *param_type(const cJSON *root)
{
    cJSON *params, *bt;

    if (!root)
        return NULL;
    params = cJSON_GetObjectItem(root, "params");
    if (!params || !cJSON_IsObject(params))
        return NULL;
    bt = cJSON_GetObjectItem(params, "biz_type");
    if (!bt || !cJSON_IsString(bt) || !bt->valuestring || !bt->valuestring[0])
        return NULL;
    return bt->valuestring;
}

/* stats：全量统计（总数/在线/离线/不纳入/未处理报警/上线率） */
static char *handle_stats(const char *id)
{
    cJSON *data;
    double rate;

    data = cJSON_CreateObject();
    if (!data)
        return NULL;

    rate = (g_bcfg.total > 0u)
         ? ((double)g_bcfg.online * 100.0 / (double)g_bcfg.total)
         : 0.0;

    cJSON_AddNumberToObject(data, "total",           (double)g_bcfg.total);
    cJSON_AddNumberToObject(data, "online",          (double)g_bcfg.online);
    cJSON_AddNumberToObject(data, "offline",         (double)g_bcfg.offline);
    cJSON_AddNumberToObject(data, "excluded",        (double)g_bcfg.excluded);
    cJSON_AddNumberToObject(data, "alarm_unhandled", (double)g_bcfg.alarm_unhandled);
    cJSON_AddNumberToObject(data, "online_rate",     rate);

    return build_rsp(id, Q_IPC_R_OK, "ok", data);
}

/* BIZ_QUERY：按 biz_type 分发 */
static char *handle_biz_query(struct biz_ctx *g, const cJSON *root, const char *id)
{
    const char *type = param_type(root);

    if (!type)
        return build_rsp(id, Q_IPC_R_BADREQ, "missing biz_type", NULL);

    if (strcmp(type, BIZ_TYPE_STATS) == 0)
    {
        QLOG_INFO(g->log, "biz_query stats (id=%s)", id ? id : "");
        return handle_stats(id);
    }

    /* M3 待补：companies / vehicles */
    QLOG_WARN(g->log, "biz_query 暂不支持的 biz_type=%s (id=%s)", type, id ? id : "");
    return build_rsp(id, Q_IPC_R_UNSUPPORTED, "unsupported biz_type", NULL);
}

char *biz_handle(struct biz_ctx *g, const char *req_json, size_t req_len)
{
    cJSON *root;
    cJSON *cmdj;
    char  *rsp;
    const char *id = "";
    int    cmd = -1;

    (void)req_len;
    if (!g || !req_json)
        return NULL;

    root = cJSON_Parse(req_json);
    if (!root)
        return NULL;

    {
        cJSON *idj = cJSON_GetObjectItem(root, "id");
        if (idj && cJSON_IsString(idj) && idj->valuestring)
            id = idj->valuestring;
    }
    cmdj = cJSON_GetObjectItem(root, "cmd");
    if (cmdj && cJSON_IsNumber(cmdj))
        cmd = (int)cmdj->valuedouble;

    if (g_bcfg.require_token && !token_ok(root))
        rsp = build_rsp(id, Q_IPC_R_UNAUTHORIZED, "missing/invalid token", NULL);
    else
    {
        switch (cmd)
        {
        case Q_CHAN_CMD_BIZ_QUERY:
            rsp = handle_biz_query(g, root, id);
            break;
        case Q_CHAN_CMD_BIZ_UPDATE:
            rsp = build_rsp(id, Q_IPC_R_UNSUPPORTED, "BIZ_UPDATE not implemented yet (M3)", NULL);
            break;
        default:
            QLOG_WARN(g->log, "不支持的指令 cmd=%d (id=%s)", cmd, id);
            rsp = build_rsp(id, Q_IPC_R_UNSUPPORTED, "unsupported cmd", NULL);
            break;
        }
    }

    cJSON_Delete(root);
    return rsp;
}
