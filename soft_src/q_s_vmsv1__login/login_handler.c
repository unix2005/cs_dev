/**
 * @file login_handler.c
 * @brief 登录微服务业务处理：解析 bizd JSON 信封 -> 登录鉴权 -> 生成响应 JSON
 *
 * 凭据校验：params.username / params.pwd_hash 由 bizd 以 hex 编码下发，
 * 本服务 hex 解码后与配置的用户名/密码比对（测试默认 qiaoshui / 123456）。
 * LOGIN 成功签发 32 字节随机 Token（hex）；HEARTBEAT/LOGOUT 直接回 ok。
 */
#include "srv_login.h"

/* 取 params 中的 hex 字段，解码为 NUL 结尾明文；成功 0，缺字段/解析失败 -1 */
static int param_str(const cJSON *params, const char *key, char *out, size_t outcap)
{
    cJSON *it;
    const char *hex;
    int n;

    if (!params || outcap == 0u)
        return -1;
    it = cJSON_GetObjectItem(params, key);
    if (!it || !cJSON_IsString(it) || !it->valuestring)
        return -1;
    hex = it->valuestring;
    n = login_hex_decode(hex, (uint8_t *)out, outcap - 1u);
    if (n < 0)
        return -1;
    out[n] = '\0';
    return 0;
}

static char *gen_token(void)
{
    uint8_t tk[LOGIN_TOKEN_LEN];
    char   *hex = (char *)malloc(LOGIN_TOKEN_LEN * 2u + 1u);

    if (!hex)
        return NULL;
    if (q_rand_bytes(tk, sizeof(tk)) != 0)
    {
        free(hex);
        return NULL;
    }
    (void)login_hex_encode(tk, sizeof(tk), hex, LOGIN_TOKEN_LEN * 2u + 1u);
    return hex;
}

static char *build_rsp(const char *id, int result, const char *msg, cJSON *data)
{
    cJSON *root = cJSON_CreateObject();
    char  *s;

    if (!root)
        return NULL;
    cJSON_AddNumberToObject(root, "ver", 1);
    cJSON_AddStringToObject(root, "id", id ? id : "");
    cJSON_AddNumberToObject(root, "result", result);
    cJSON_AddStringToObject(root, "message", msg ? msg : "");
    if (data)
        cJSON_AddItemToObject(root, "data", data);
    else
        cJSON_AddItemToObject(root, "data", cJSON_CreateObject());
    s = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return s;
}

static char *handle_login(struct login_ctx *g, const cJSON *root, const char *id)
{
    cJSON *params = cJSON_GetObjectItem(root, "params");
    char   user[128], pass[128];
    char  *tok;
    cJSON *data;

    if (param_str(params, "username", user, sizeof(user)) != 0)
        return build_rsp(id, LOGIN_R_BADREQ, "missing username", NULL);
    if (param_str(params, "pwd_hash", pass, sizeof(pass)) != 0)
        return build_rsp(id, LOGIN_R_BADREQ, "missing pwd_hash", NULL);

    if (strcmp(user, g_lcfg.user) != 0 || strcmp(pass, g_lcfg.pass) != 0)
    {
        QLOG_WARN(g->log, "login 失败：凭据不匹配");
        return build_rsp(id, LOGIN_R_BADCRED, "bad credentials", NULL);
    }

    tok = gen_token();
    if (!tok)
        return build_rsp(id, LOGIN_R_INTERNAL, "token gen failed", NULL);

    data = cJSON_CreateObject();
    if (data)
    {
        cJSON_AddStringToObject(data, "token", tok);
        cJSON_AddStringToObject(data, "user", g_lcfg.user);
        cJSON_AddNumberToObject(data, "expires_in", 3600);
    }
    free(tok);
    QLOG_INFO(g->log, "login 成功：user=%s", g_lcfg.user);
    return build_rsp(id, LOGIN_R_OK, "ok", data);
}

char *login_handle(struct login_ctx *g, const char *req_json, size_t req_len)
{
    cJSON  *root;
    cJSON  *id_item, *cmd_item;
    const char *id;
    char   *rsp;
    double  cmd;

    if (!req_json || req_len == 0u)
        return NULL;

    root = cJSON_ParseWithLength(req_json, req_len);
    if (!root)
    {
        QLOG_WARN(g->log, "请求 JSON 解析失败");
        return NULL;
    }

    id_item  = cJSON_GetObjectItem(root, "id");
    cmd_item = cJSON_GetObjectItem(root, "cmd");
    id = (id_item && cJSON_IsString(id_item) && id_item->valuestring) ? id_item->valuestring : "";

    if (!cmd_item || !cJSON_IsNumber(cmd_item))
    {
        rsp = build_rsp(id, LOGIN_R_BADREQ, "missing cmd", NULL);
        cJSON_Delete(root);
        return rsp;
    }
    cmd = cmd_item->valuedouble;

    switch ((int)cmd)
    {
    case Q_CHAN_CMD_LOGIN:
        rsp = handle_login(g, root, id);
        break;
    case Q_CHAN_CMD_HEARTBEAT:
    {
        cJSON *data = cJSON_CreateObject();
        if (data)
            cJSON_AddStringToObject(data, "alive", "1");
        rsp = build_rsp(id, LOGIN_R_OK, "ok", data);
        break;
    }
    case Q_CHAN_CMD_LOGOUT:
        rsp = build_rsp(id, LOGIN_R_OK, "ok", NULL);
        break;
    default:
        rsp = build_rsp(id, LOGIN_R_UNSUPPORTED, "unsupported cmd", NULL);
        break;
    }

    cJSON_Delete(root);
    return rsp;
}
