/**
 * @file login_handler.c
 * @brief 登录微服务业务处理：解析 bizd JSON 信封 -> 登录鉴权 -> 生成响应 JSON
 *
 * 凭据校验（方案A：服务端按账号下发 salt，客户端本地派生）：
 *   SALT_QUERY(cmd=4) 下发账号 salt(hex) 与 iter；客户端算
 *   pwd_hash = PBKDF2-SM3(口令, salt, iter) 后随 LOGIN 上送，
 *   本服务与本地保存的期望值做**常量时间**比较（不比较明文口令）。
 *   params 各字段由 bizd 以 hex 编码下发，需先 hex 解码为原始字节。
 * 兼容：未配置 pwd_hash 的遗留部署回退明文比较（启动时告警）。
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

/*
 * 取 params 中的 hex 字段，解码为**原始字节**（可含 0x00，如 32B 口令派生值）。
 * 成功 0 并回写 *outlen；缺字段 / 长度不符返回 -1。
 * 注意：不能用 param_str —— 它按文本处理，遇到内嵌 NUL 会被截断。
 */
static int param_bytes(const cJSON *params, const char *key,
                       uint8_t *out, size_t outcap, size_t *outlen)
{
    cJSON      *it;
    const char *hex;
    size_t      hlen;
    int         n;

    if (!params || !out || !outlen || outcap == 0u)
        return -1;
    it = cJSON_GetObjectItem(params, key);
    if (!it || !cJSON_IsString(it) || !it->valuestring)
        return -1;
    hex  = it->valuestring;
    hlen = strlen(hex);
    if (hlen / 2u > outcap)
        return -1;
    n = login_hex_decode(hex, out, outcap);
    if (n <= 0)
        return -1;
    *outlen = (size_t)n;
    return 0;
}

/* 常量时间等值比较：避免按字节短路泄露前缀匹配长度 */
static int ct_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t d = 0u;
    size_t  i;

    for (i = 0u; i < n; i++)
        d |= (uint8_t)(a[i] ^ b[i]);
    return d == 0u;
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

/*
 * SALT_QUERY（cmd=4，方案A）：登录前下发账号 salt 与派生迭代次数。
 * 请求 params: {"username":"<hex>"}
 * 响应 data  : {"salt":"<hex>","iter":<n>}
 * bizd 的 bizd_hnd_salt 会取 data.salt 转原始字节装入 Q_IPC_T_BODY 回给客户端。
 *
 * 未知账号也返回同一份 salt（不枚举账号是否存在），避免用户名探测。
 */
static char *handle_salt_query(struct login_ctx *g, const cJSON *root, const char *id)
{
    char   hex[LOGIN_SALT_LEN * 2u + 1u];
    cJSON *data;
    (void)root;

    /* 注意：login_hex_encode 成功返回写入字符数（>0），失败返回 -1 —— 判据是 <0，不是 !=0 */
    if (login_hex_encode(g_lcfg.salt, LOGIN_SALT_LEN, hex, sizeof(hex)) < 0)
        return build_rsp(id, LOGIN_R_INTERNAL, "salt encode failed", NULL);

    data = cJSON_CreateObject();
    if (!data)
        return build_rsp(id, LOGIN_R_INTERNAL, "oom", NULL);
    cJSON_AddStringToObject(data, "salt", hex);
    cJSON_AddNumberToObject(data, "iter", (double)g_lcfg.pbkdf2_iter);

    QLOG_INFO(g->log, "salt_query -> salt=%s iter=%u", hex, (unsigned)g_lcfg.pbkdf2_iter);
    return build_rsp(id, LOGIN_R_OK, "ok", data);
}

static char *handle_login(struct login_ctx *g, const cJSON *root, const char *id)
{
    cJSON  *params = cJSON_GetObjectItem(root, "params");
    char    user[128], otp[64];
    uint8_t pwd_hash[LOGIN_PWDHASH_LEN];
    size_t  phlen = 0u;
    char   *tok;
    cJSON  *data;
    int     ok;

    if (param_str(params, "username", user, sizeof(user)) != 0)
        return build_rsp(id, LOGIN_R_BADREQ, "missing username", NULL);
    if (param_bytes(params, "pwd_hash", pwd_hash, sizeof(pwd_hash), &phlen) != 0)
        return build_rsp(id, LOGIN_R_BADREQ, "missing pwd_hash", NULL);
    if (phlen != LOGIN_PWDHASH_LEN)
        return build_rsp(id, LOGIN_R_BADREQ, "bad pwd_hash length", NULL);

    /* 用户名比较（常量时间，避免时序泄露） */
    {
        size_t ul = strlen(user), cl = strlen(g_lcfg.user);
        size_t n  = ul > cl ? ul : cl;
        char   ubuf[128], cbuf[128];
        memset(ubuf, 0, sizeof(ubuf));
        memset(cbuf, 0, sizeof(cbuf));
        memcpy(ubuf, user, ul);
        memcpy(cbuf, g_lcfg.user, cl);
        if (!ct_equal((const uint8_t *)ubuf, (const uint8_t *)cbuf, n) || ul != cl)
        {
            QLOG_WARN(g->log, "login 失败：用户名不匹配");
            return build_rsp(id, LOGIN_R_BADCRED, "bad credentials", NULL);
        }
    }

    /* 动态口令（可选）：配置了期望 OTP 才校验 */
    if (g_lcfg.otp[0])
    {
        if (param_str(params, "otp_code", otp, sizeof(otp)) != 0)
            return build_rsp(id, LOGIN_R_BADREQ, "missing otp_code", NULL);
        if (strcmp(otp, g_lcfg.otp) != 0)
        {
            QLOG_WARN(g->log, "login 失败：动态口令不匹配");
            return build_rsp(id, LOGIN_R_BADCRED, "bad credentials", NULL);
        }
    }

    if (g_lcfg.hash_mode)
    {
        /* 方案A：与本地保存的 PBKDF2-SM3 期望值做常量时间比较 */
        ok = ct_equal(pwd_hash, g_lcfg.expect_hash, LOGIN_PWDHASH_LEN);
    }
    else
    {
        /* 遗留模式：把 pwd_hash 当明文口令文本比较（仅限测试，启动时已告警） */
        ok = (strncmp((const char *)pwd_hash, g_lcfg.pass, LOGIN_PWDHASH_LEN) == 0);
    }
    if (!ok)
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
    case Q_CHAN_CMD_SALT_QUERY:
        rsp = handle_salt_query(g, root, id);
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
