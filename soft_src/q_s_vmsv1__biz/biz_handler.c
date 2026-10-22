/**
 * @file biz_handler.c
 * @brief 业务微服务请求处理：解析 bizd JSON 信封 -> 按 biz_type 取数 -> 生成响应 JSON
 *
 * 支持的 biz_type（M3）：
 *   - stats     : 全量统计（总数/在线/离线/不纳入/未处理报警）
 *   - companies : d_org_info 组织机构列表（分页 + parent 过滤）
 *   - vehicles  : d_vehicleinfo 车辆列表（分页 + 组织子树/关键字/在线/报警/主键过滤）
 * 接口形状以《biz微服务接口约定.md》为准；data 域字段一律是库表列名的 camelCase。
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

/* 取 params 对象（bizd 生成；缺失返回 NULL） */
static const cJSON *params_of(const cJSON *root)
{
    cJSON *p = root ? cJSON_GetObjectItem(root, "params") : NULL;
    return (p && cJSON_IsObject(p)) ? p : NULL;
}

/* ===================== M3：分页 / 业务参数解析 ===================== */

/* 分页参数：来自客户端的 0x0203 OFFSET / 0x0204 LIMIT，bizd 已转成 params.offset/limit。
   limit==0（客户端未指定）-> 默认页大小；再统一 clamp 到 page_limit_max。 */
static void get_pagination(const cJSON *params, uint32_t *offset, uint32_t *limit)
{
    const cJSON *o, *l;
    uint32_t     ov = 0u, lv = BIZ_PAGE_LIMIT_DEF;

    if (params)
    {
        o = cJSON_GetObjectItem(params, "offset");
        if (o && cJSON_IsNumber(o) && o->valuedouble >= 0.0) ov = (uint32_t)o->valuedouble;
        l = cJSON_GetObjectItem(params, "limit");
        if (l && cJSON_IsNumber(l) && l->valuedouble > 0.0)  lv = (uint32_t)l->valuedouble;
    }
    if (lv == 0u)                          lv = BIZ_PAGE_LIMIT_DEF;
    if (g_bcfg.page_limit_max && lv > g_bcfg.page_limit_max)
        lv = g_bcfg.page_limit_max;
    *offset = ov;
    *limit  = lv;
}

/* params.biz_param 是 JSON **文本**（TLV 0x0202 承载的是 UTF-8 文本），需二次解析。
   返回的对象由调用方 cJSON_Delete；不存在/为空返回 NULL（不视为错误：用默认过滤）。 */
static cJSON *biz_param_obj(const cJSON *params, int *bad)
{
    cJSON *bpj = params ? cJSON_GetObjectItem(params, "biz_param") : NULL;

    if (bad) *bad = 0;
    if (!bpj || !cJSON_IsString(bpj) || !bpj->valuestring || !bpj->valuestring[0])
        return NULL;
    {
        cJSON *o = cJSON_Parse(bpj->valuestring);
        if (!o && bad) *bad = 1;
        return o;
    }
}

static const char *bp_str(const cJSON *bp, const char *key)
{
    cJSON *v = bp ? cJSON_GetObjectItem(bp, key) : NULL;
    return (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : NULL;
}

static int bp_int(const cJSON *bp, const char *key, int def)
{
    cJSON *v = bp ? cJSON_GetObjectItem(bp, key) : NULL;
    if (v && cJSON_IsNumber(v)) return (int)v->valuedouble;
    if (v && cJSON_IsBool(v))   return cJSON_IsTrue(v) ? 1 : 0;
    return def;
}

/* 统一的分页列表信封：data = {total, offset, limit, items[]} */
static char *build_list_rsp(const char *id, cJSON *items, uint32_t total,
                            uint32_t offset, uint32_t limit)
{
    cJSON *data = cJSON_CreateObject();

    if (!data) { if (items) cJSON_Delete(items); return NULL; }
    cJSON_AddNumberToObject(data, "total",  (double)total);
    cJSON_AddNumberToObject(data, "offset", (double)offset);
    cJSON_AddNumberToObject(data, "limit",  (double)limit);
    cJSON_AddItemToObject(data, "items", items ? items : cJSON_CreateArray());
    return build_rsp(id, Q_IPC_R_OK, "ok", data);
}

/* companies：d_org_info 组织机构列表
   biz_param: {"parent_org_id":"1","recursive":1}
              parent_org_id 省略/空 = 返回全部（客户端首屏建树用） */
static char *handle_companies(struct biz_ctx *g, const cJSON *params, const char *id)
{
    cJSON      *bp = NULL, *items;
    const char *parent;
    int         recursive, bad = 0;
    uint32_t    offset, limit, total = 0u;

    (void)g;
    bp = biz_param_obj(params, &bad);
    if (bad) return build_rsp(id, Q_IPC_R_BADREQ, "biz_param is not valid JSON", NULL);

    parent    = bp_str(bp, "parent_org_id");
    recursive = bp_int(bp, "recursive", 1);
    get_pagination(params, &offset, &limit);

    items = cJSON_CreateArray();
    if (!items) { cJSON_Delete(bp); return NULL; }
    if (biz_stub_orgs(items, parent, recursive, offset, limit, &total) != 0)
    {
        cJSON_Delete(items);
        cJSON_Delete(bp);
        return build_rsp(id, Q_IPC_R_INTERNAL, "stub orgs failed", NULL);
    }
    cJSON_Delete(bp);
    return build_list_rsp(id, items, total, offset, limit);
}

/* vehicles：d_vehicleinfo 车辆列表
   biz_param: {"org_id":"14","recursive":1,"keyword":"京A","online_only":0,
               "alarm_only":0,"alarm_mask":0,"ids":["000123", ...]} */
static char *handle_vehicles(struct biz_ctx *g, const cJSON *params, const char *id)
{
    cJSON            *bp = NULL, *items, *idsarr;
    biz_veh_filter_t  f;
    const char       *s;
    int               bad = 0;
    uint32_t          offset, limit, total = 0u;

    (void)g;
    bp = biz_param_obj(params, &bad);
    if (bad) return build_rsp(id, Q_IPC_R_BADREQ, "biz_param is not valid JSON", NULL);

    memset(&f, 0, sizeof(f));
    f.recursive = 1;                       /* 默认"该组织及其所有下级" */

    s = bp_str(bp, "org_id");
    if (s) snprintf(f.org_id, sizeof(f.org_id), "%s", s);
    s = bp_str(bp, "keyword");
    if (s) snprintf(f.keyword, sizeof(f.keyword), "%s", s);
    if (bp) f.recursive    = bp_int(bp, "recursive", 1);
    if (bp) f.online_only  = bp_int(bp, "online_only", 0);
    if (bp) f.alarm_only   = bp_int(bp, "alarm_only", 0);
    if (bp) f.alarm_mask   = (uint32_t)bp_int(bp, "alarm_mask", 0);

    idsarr = bp ? cJSON_GetObjectItem(bp, "ids") : NULL;
    if (idsarr && cJSON_IsArray(idsarr))
    {
        cJSON *e;
        cJSON_ArrayForEach(e, idsarr)
        {
            if (f.id_count >= BIZ_IDS_MAX) break;
            if (cJSON_IsString(e) && e->valuestring && e->valuestring[0])
            {
                snprintf(f.ids[f.id_count], BIZ_ID_MAX, "%s", e->valuestring);
                ++f.id_count;
            }
            else if (cJSON_IsNumber(e))     /* 也接受数值主键，方便裸协议调试 */
            {
                snprintf(f.ids[f.id_count], BIZ_ID_MAX, "%06u", (uint32_t)e->valuedouble);
                ++f.id_count;
            }
        }
    }

    get_pagination(params, &offset, &limit);

    items = cJSON_CreateArray();
    if (!items) { cJSON_Delete(bp); return NULL; }
    if (biz_stub_vehicles(items, &f, offset, limit, &total) != 0)
    {
        cJSON_Delete(items);
        cJSON_Delete(bp);
        return build_rsp(id, Q_IPC_R_INTERNAL, "stub vehicles failed", NULL);
    }
    cJSON_Delete(bp);
    return build_list_rsp(id, items, total, offset, limit);
}

/* BIZ_QUERY：按 biz_type 分发 */
static char *handle_biz_query(struct biz_ctx *g, const cJSON *root, const char *id)
{
    const char   *type = param_type(root);
    const cJSON  *params;

    if (!type)
        return build_rsp(id, Q_IPC_R_BADREQ, "missing biz_type", NULL);

    params = params_of(root);

    if (strcmp(type, BIZ_TYPE_STATS) == 0)
    {
        QLOG_INFO(g->log, "biz_query stats (id=%s)", id ? id : "");
        return handle_stats(id);
    }
    if (strcmp(type, BIZ_TYPE_COMPANIES) == 0)
    {
        QLOG_INFO(g->log, "biz_query companies (id=%s)", id ? id : "");
        return handle_companies(g, params, id);
    }
    if (strcmp(type, BIZ_TYPE_VEHICLES) == 0)
    {
        QLOG_INFO(g->log, "biz_query vehicles (id=%s)", id ? id : "");
        return handle_vehicles(g, params, id);
    }

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
