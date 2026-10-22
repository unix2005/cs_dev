/**
 * @file biz_stub.c
 * @brief 桩数据生成：d_org_info（组织机构）/ d_vehicleinfo（车辆）的不连库替身
 *
 * 设计要点：
 *   1. **确定性**：行内容只由行号 idx 决定（无 rand、无时间），
 *      因此同一 (offset,limit) 多次查询结果完全一致 —— 分页翻页不会重复/丢行，
 *      联调时也能用"第 3 行是不是京Axxxxx"来断言链路正确性。
 *   2. **只生成一页**：total 恒返回"过滤后的总行数"，items 只填 [offset, offset+limit)，
 *      这样 3 万行的表也不会把单包撑爆（上限 256KB，见 include_ipc_proto.h）。
 *   3. **字段命名 = 库表列名的 camelCase**：snake_case 去下划线、下划线后首字母大写
 *      （vehicle_id -> vehicleId，wgs84_lon -> wgs84Lon，note -> note）。
 *      可空列无值一律输出 JSON null（不是空串），与 Oracle NULL 语义一致。
 *   4. **扩展字段**（非库表列，真实部署由定位表 JOIN / 统计缓存提供）已单独标注，
 *      见下方 k_ext 注释；客户端只按名字取值，换真实实现时形状不变。
 *
 * 真实实现替换点：把 biz_stub_orgs / biz_stub_vehicles 换成带连接池的 SQL 查询即可，
 * 上层 biz_handler.c 与客户端解析代码一律不动。
 */
#include "srv_biz.h"

#include <stdio.h>
#include <string.h>

/* ===================== 组织机构种子（对齐客户端 UI 的既有组织树） ===================== */
typedef struct
{
    const char *id;
    const char *parent;      /* 根节点的父级为 "-1"（见 d_org_info 列注释） */
    const char *short_name;  /* org_short_name */
    const char *long_name;   /* org_long_name */
    uint32_t    veh;         /* 车辆数（扩展字段 vehicleCount，真实由统计表算） */
    uint32_t    online;      /* 在线数（扩展字段 onlineCount） */
} org_seed_t;

static const org_seed_t k_org[] = {
    { "1",    "-1", "运输公司",         "中国石油集团运输公司", 18714, 8960 },
    { "2",    "1",  "塔运司",           "塔里木运输公司",        886,  293 },
    { "3",    "1",  "沙运司",           "沙漠运输公司",          464,  110 },
    { "4",    "1",  "一公司",           "第一运输公司",          862,   89 },
    { "5",    "1",  "长庆运输公司",     "长庆运输公司",         2038, 1844 },
    { "6",    "1",  "华北运输公司",     "华北运输公司",          876,    0 },
    { "7",    "1",  "准噶尔分公司",     "准噶尔分公司",          450,   70 },
    { "8",    "1",  "冀东运输公司",     "冀东运输公司",          130,   87 },
    { "9",    "1",  "黑龙江分公司",     "黑龙江分公司",          500,  316 },
    { "10",   "1",  "吉林分公司",       "吉林分公司",            615,  332 },
    { "11",   "1",  "辽宁分公司",       "辽宁分公司",            470,  269 },
    { "12",   "1",  "大连分公司",       "大连分公司",            158,   93 },
    { "13",   "1",  "内蒙古分公司",     "内蒙古分公司",          520,  210 },
    { "14",   "1",  "北京分公司",       "北京分公司",            398,  149 },
    { "15",   "1",  "河北分公司",       "河北分公司",            272,  215 },
    { "16",   "1",  "天津分公司",       "天津分公司",            107,   57 },
    { "17",   "1",  "山西分公司",       "山西分公司",            167,  116 },
    { "18",   "1",  "山东分公司",       "山东分公司",            344,  272 },
    { "19",   "1",  "江苏分公司",       "江苏分公司",            135,   82 },
    /* —— 三级组织：省级分公司 -> 地市级配送中心；车辆实体挂在叶子单位上 —— */
    { "1101", "11", "沈阳配送中心",     "沈阳配送中心",          250,  150 },
    { "1102", "11", "大连配送中心",     "大连配送中心",          220,  119 },
    { "1401", "14", "北京朝阳配送中心", "北京朝阳配送中心",      210,   80 },
    { "1402", "14", "北京海淀配送中心", "北京海淀配送中心",      188,   69 },
    { "1501", "15", "石家庄配送中心",   "石家庄配送中心",        160,  120 },
    { "1502", "15", "保定配送中心",     "保定配送中心",          112,   95 },
};
#define ORG_N  ((uint32_t)(sizeof(k_org) / sizeof(k_org[0])))

/* ===================== 车辆归属区间（叶子组织） =====================
 * 约定：车辆只挂在叶子单位上；父组织的 vehicleCount = 各叶子之和（已在种子里对齐）。
 * 行号 >= LEAF_TOTAL 的车辆视为"未分配"（org_id = "0"），真实系统里同样存在这类数据。 */
typedef struct
{
    const char *org_id;
    uint32_t    start;
    uint32_t    count;
} leaf_seg_t;

static const leaf_seg_t k_leaf[] = {
    { "2",       0,  886 }, { "3",     886,  464 }, { "4",    1350,  862 },
    { "5",    2212, 2038 }, { "6",    4250,  876 }, { "7",    5126,  450 },
    { "8",    5576,  130 }, { "9",    5706,  500 }, { "10",   6206,  615 },
    { "12",   6821,  158 }, { "13",   6979,  520 }, { "16",   7499,  107 },
    { "17",   7606,  167 }, { "18",   7773,  344 }, { "19",   8117,  135 },
    { "1101", 8252,  250 }, { "1102", 8502,  220 }, { "1401", 8722,  210 },
    { "1402", 8932,  188 }, { "1501", 9120,  160 }, { "1502", 9280,  112 },
};
#define LEAF_N       ((uint32_t)(sizeof(k_leaf) / sizeof(k_leaf[0])))
#define LEAF_TOTAL   9392u      /* 各叶子 count 之和；行号超出即"未分配" */

static const char *k_plate_prefix[] = {
    "京", "津", "冀", "晋", "蒙", "辽", "吉", "黑", "鲁", "苏", "陕", "甘"
};
static const char *k_city[] = {
    "北京市朝阳区", "北京市海淀区", "天津市滨海新区", "石家庄市裕华区", "太原市小店区",
    "呼和浩特市赛罕区", "沈阳市铁西区", "长春市朝阳区", "哈尔滨市南岗区", "济南市历下区",
    "西安市雁塔区", "兰州市城关区", "保定市莲池区", "唐山市路北区", "青岛市市南区",
    "银川市金凤区", "乌海市海勃湾区", "鄂尔多斯市东胜区", "榆林市榆阳区", "克拉玛依市克拉玛依区"
};
/* 报警位与 vms_api.h 的 VMS_ALARM_* 对齐（1 超速 / 2 疲劳 / 4 掉线 / 8 越界 / 16 GNSS 失锁） */
static const char *k_alarm_text[] = {
    "超速报警", "疲劳驾驶", "掉线告警", "越界报警", "GNSS 失锁"
};

/* --------------------------------- 小工具 --------------------------------- */
/* 确定性伪随机：只依赖 (idx, salt)，无全局状态（worker 多线程安全） */
static uint32_t stub_rand(uint32_t idx, uint32_t salt)
{
    uint32_t h = idx * 2654435761u + salt * 40503u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return h;
}

static uint32_t stub_range(uint32_t idx, uint32_t salt, uint32_t lo, uint32_t hi)
{
    if (hi <= lo) return lo;
    return lo + stub_rand(idx, salt) % (hi - lo + 1u);
}

/* org_id -> 种子下标；找不到返回 -1 */
static int org_index_of(const char *id)
{
    uint32_t i;
    if (!id || !id[0]) return -1;
    for (i = 0u; i < ORG_N; i++)
        if (strcmp(k_org[i].id, id) == 0)
            return (int)i;
    return -1;
}

/* 组织 i 是否落在 root_id 子树内（含 root 自身）；root_id 为空视为"全部" */
static int org_in_subtree(uint32_t i, const char *root_id)
{
    uint32_t hop = 0u;
    if (!root_id || !root_id[0]) return 1;
    while (hop++ < ORG_N)
    {
        if (strcmp(k_org[i].id, root_id) == 0) return 1;
        if (strcmp(k_org[i].parent, "-1") == 0) return 0;   /* 已到根 */
        {
            int p = org_index_of(k_org[i].parent);
            if (p < 0) return 0;
            i = (uint32_t)p;
        }
    }
    return 0;
}

/* 该组织是否还有下级组织 */
static uint32_t org_child_count(const char *id)
{
    uint32_t i, n = 0u;
    for (i = 0u; i < ORG_N; i++)
        if (strcmp(k_org[i].parent, id) == 0) ++n;
    return n;
}

/* 可空列：有值写字符串，无值写 JSON null */
static void add_nullable_str(cJSON *o, const char *key, const char *val)
{
    if (val && val[0])
        cJSON_AddStringToObject(o, key, val);
    else
        cJSON_AddNullToObject(o, key);
}

/* ===================== d_org_info -> 一条 JSON ===================== */
static cJSON *make_org_item(uint32_t k)
{
    const org_seed_t *s = &k_org[k];
    cJSON  *o;
    char    buf[64];
    double  lon, lat;

    o = cJSON_CreateObject();
    if (!o) return NULL;

    lon = 113.572906 + (double)stub_range(k, 11u, 0u, 4000u) / 10000.0 - 0.2;
    lat =  42.443101 + (double)stub_range(k, 12u, 0u, 3000u) / 10000.0 - 0.15;

    /* —— d_org_info 库表列（camelCase）—— */
    cJSON_AddStringToObject(o, "orgId",        s->id);
    cJSON_AddStringToObject(o, "parentOrgId",  s->parent);
    cJSON_AddStringToObject(o, "orgLongName",  s->long_name);
    cJSON_AddStringToObject(o, "orgShortName", s->short_name);

    snprintf(buf, sizeof(buf), "%s%s", k_city[k % (sizeof(k_city) / sizeof(char *))], "1号院");
    cJSON_AddStringToObject(o, "orgAddress", buf);

    cJSON_AddNumberToObject(o, "wgs84Lon", lon);
    cJSON_AddNumberToObject(o, "wgs84Lat", lat);
    cJSON_AddNumberToObject(o, "hyLon",    lon + 0.006100);   /* 华油偏转（桩：固定偏移） */
    cJSON_AddNumberToObject(o, "hyLat",    lat + 0.002300);

    snprintf(buf, sizeof(buf), "%04u", 1u + (k % 9u));
    cJSON_AddStringToObject(o, "orgRanking", buf);
    cJSON_AddStringToObject(o, "orgShowId",  "1");

    snprintf(buf, sizeof(buf), "010-%04u", 1000u + (k % 9000u));
    cJSON_AddStringToObject(o, "orgTelephone", buf);
    snprintf(buf, sizeof(buf), "138%08u", 10000000u + (k % 90000000u));
    cJSON_AddStringToObject(o, "orgPhone", buf);
    snprintf(buf, sizeof(buf), "联系人%u", k + 1u);
    cJSON_AddStringToObject(o, "contactPerson", buf);

    cJSON_AddStringToObject(o, "deleteFlag", "0");
    cJSON_AddStringToObject(o, "beginTime",  "2020-01-01 00:00:00");
    cJSON_AddStringToObject(o, "endTime",    "2099-12-31 23:59:59");
    cJSON_AddStringToObject(o, "createTime", "2020-01-01 08:00:00");
    cJSON_AddStringToObject(o, "createBy",   "system");
    add_nullable_str(o, "modifyTime", NULL);
    add_nullable_str(o, "modifyBy",   NULL);
    add_nullable_str(o, "note",       NULL);

    /* —— 扩展字段（非库表列）：车辆数/在线数由统计表算，下级数由 parent 关系算 —— */
    cJSON_AddNumberToObject(o, "vehicleCount", (double)s->veh);
    cJSON_AddNumberToObject(o, "onlineCount",  (double)s->online);
    cJSON_AddNumberToObject(o, "childCount",   (double)org_child_count(s->id));
    return o;
}

/* ===================== d_vehicleinfo -> 一条 JSON ===================== */
static cJSON *make_vehicle_item(uint32_t idx, const char *org_id, uint32_t online_total)
{
    cJSON      *o;
    char        buf[64];
    uint32_t    online, alarm_flags;
    const char *alarm_text = NULL;

    o = cJSON_CreateObject();
    if (!o) return NULL;

    online      = (idx < online_total) ? 1u : 0u;
    alarm_flags = (stub_range(idx, 21u, 0u, 100u) < 12u)
                ? (1u << stub_range(idx, 22u, 0u, 4u))
                : 0u;
    if (alarm_flags)
    {
        uint32_t b = 0u, f = alarm_flags;
        while (f > 1u) { f >>= 1; ++b; }
        if (b < (uint32_t)(sizeof(k_alarm_text) / sizeof(char *)))
            alarm_text = k_alarm_text[b];
    }

    /* —— d_vehicleinfo 库表列（camelCase）—— */
    snprintf(buf, sizeof(buf), "%06u", idx + 1u);
    cJSON_AddStringToObject(o, "vehicleId", buf);

    /* lis_plate = 省份简称 + 字母 + 5 位数字（CHAR(20)：Oracle 定长会补空格，
       真实实现取值后必须 RTRIM，否则 JSON 里会出现尾部空格） */
    snprintf(buf, sizeof(buf), "%s%c%05u",
             k_plate_prefix[idx % (sizeof(k_plate_prefix) / sizeof(char *))],
             (char)('A' + stub_range(idx, 1u, 0u, 25u)),
             stub_range(idx, 2u, 10000u, 99999u));
    cJSON_AddStringToObject(o, "lisPlate", buf);

    snprintf(buf, sizeof(buf), "%u", 1u + (idx % 5u));   /* 1蓝 2黄 3白 4黑 5绿 */
    cJSON_AddStringToObject(o, "lisPlateColor", buf);
    cJSON_AddStringToObject(o, "orgId", org_id);

    snprintf(buf, sizeof(buf), "EMP%06u", stub_range(idx, 3u, 1u, 8000u));
    cJSON_AddStringToObject(o, "mDriverEmpid", buf);
    add_nullable_str(o, "sDriverEmpid", (idx % 3u == 0u) ? buf : NULL);
    add_nullable_str(o, "supEmpid",     (idx % 5u == 0u) ? buf : NULL);

    snprintf(buf, sizeof(buf), "VT%04u", 1u + (idx % 12u));
    cJSON_AddStringToObject(o, "vehicleTypeId", buf);
    snprintf(buf, sizeof(buf), "%u", 1u + (idx % 5u));   /* 1货运 2客运 3危化品 4工程 5公务 */
    cJSON_AddStringToObject(o, "vehicleUse", buf);
    cJSON_AddNumberToObject(o, "scrappedFlag", (idx % 500u == 0u) ? 1.0 : 0.0);

    snprintf(buf, sizeof(buf), "ERP%08u", idx + 1u);
    cJSON_AddStringToObject(o, "erpCode", buf);
    snprintf(buf, sizeof(buf), "QC%05u", idx % 100000u);   /* UI 的"自编号" */
    cJSON_AddStringToObject(o, "userDefinedCode", buf);
    add_nullable_str(o, "dbhKeyId", NULL);
    cJSON_AddStringToObject(o, "dataFrom", "V");

    snprintf(buf, sizeof(buf), "2021-%02u-%02u %02u:%02u:%02u",
             1u + (idx % 12u), 1u + (idx % 28u), idx % 24u, idx % 60u, (idx * 7u) % 60u);
    cJSON_AddStringToObject(o, "createTime", buf);
    cJSON_AddStringToObject(o, "createBy", "system");
    add_nullable_str(o, "modifyTime", NULL);
    add_nullable_str(o, "modifyBy",   NULL);
    add_nullable_str(o, "note",       NULL);

    /* —— 扩展字段（非 d_vehicleinfo 列）：来自定位/报警表，真实部署 JOIN 后给出 —— */
    cJSON_AddNumberToObject(o, "online",         (double)online);
    cJSON_AddNumberToObject(o, "offlineSeconds", online ? 0.0
                                                        : (double)stub_range(idx, 31u, 60u, 604800u));
    cJSON_AddNumberToObject(o, "alarmFlags",     (double)alarm_flags);
    add_nullable_str(o, "alarmText", alarm_text);
    cJSON_AddNumberToObject(o, "speedKph", online ? (double)stub_range(idx, 32u, 0u, 95u) : 0.0);
    cJSON_AddNumberToObject(o, "lon", 113.572906 + (double)stub_range(idx, 33u, 0u, 4000u) / 10000.0 - 0.2);
    cJSON_AddNumberToObject(o, "lat",  42.443101 + (double)stub_range(idx, 34u, 0u, 3000u) / 10000.0 - 0.15);
    snprintf(buf, sizeof(buf), "2026-10-21 %02u:%02u:%02u",
             8u + (idx % 12u), idx % 60u, (idx * 7u) % 60u);
    cJSON_AddStringToObject(o, "lastTime", buf);
    cJSON_AddStringToObject(o, "location",
                            k_city[idx % (sizeof(k_city) / sizeof(char *))]);
    return o;
}

/* ===================== 对外：组织机构列表 =====================
 * parent_id 为空 = 全部；否则按 parent_org_id 过滤（recursive=1 时返回整棵子树）。 */
int biz_stub_orgs(cJSON *items, const char *parent_id, int recursive,
                  uint32_t offset, uint32_t limit, uint32_t *total_out)
{
    uint32_t i, total = 0u, written = 0u;
    int      all = (!parent_id || !parent_id[0]);

    if (!items || !total_out) return -1;
    *total_out = 0u;

    for (i = 0u; i < ORG_N; i++)
    {
        int hit;

        if (all)
            hit = 1;
        else if (recursive)
            hit = org_in_subtree(i, parent_id);
        else
            hit = (strcmp(k_org[i].parent, parent_id) == 0);

        if (!hit) continue;

        if (total >= offset && written < limit)
        {
            cJSON *it = make_org_item(i);
            if (!it) return -1;
            cJSON_AddItemToArray(items, it);
            ++written;
        }
        ++total;
    }
    *total_out = total;
    return 0;
}

/* ===================== 对外：车辆列表 =====================
 * 先按组织过滤（叶子归属），再按关键字 / 在线 / 报警过滤；
 * 只生成命中区间内的那一页，total 返回过滤后的总行数。 */
int biz_stub_vehicles(cJSON *items, const biz_veh_filter_t *f,
                      uint32_t offset, uint32_t limit, uint32_t *total_out)
{
    uint32_t idx, total = 0u, written = 0u;
    uint32_t allow[LEAF_N];
    uint32_t i;
    int      allow_zero, filter_org;
    uint32_t mask;

    if (!items || !f || !total_out) return -1;
    *total_out = 0u;

    filter_org = (f->org_id[0] != '\0');
    allow_zero = filter_org ? 0 : 1;
    for (i = 0u; i < LEAF_N; i++)
    {
        if (!filter_org)
        {
            allow[i] = 1u;
        }
        else if (f->recursive)
        {
            /* 注意：i 是 k_leaf 下标，必须先换成 k_org 下标再判子树 */
            int oi = org_index_of(k_leaf[i].org_id);
            allow[i] = (oi >= 0 && org_in_subtree((uint32_t)oi, f->org_id)) ? 1u : 0u;
        }
        else
        {
            allow[i] = (strcmp(k_leaf[i].org_id, f->org_id) == 0) ? 1u : 0u;
        }
    }
    mask = f->alarm_mask ? f->alarm_mask : 0xFFFFFFFFu;

    for (idx = 0u; idx < g_bcfg.vehicle_total; idx++)
    {
        const char *org_id;
        uint32_t    seg;

        /* —— 组织过滤 —— */
        if (idx >= LEAF_TOTAL)
        {
            if (!allow_zero) continue;
            org_id = "0";
        }
        else
        {
            for (seg = 0u; seg < LEAF_N; seg++)
                if (idx >= k_leaf[seg].start && idx < k_leaf[seg].start + k_leaf[seg].count)
                    break;
            if (seg >= LEAF_N || !allow[seg]) continue;
            org_id = k_leaf[seg].org_id;
        }

        /* —— 在线 / 报警过滤（依赖行号，先于关键字，避免多余的字符串运算）—— */
        if (f->online_only && idx >= g_bcfg.vehicle_online) continue;
        if (f->alarm_only)
        {
            uint32_t af = (stub_range(idx, 21u, 0u, 100u) < 12u)
                        ? (1u << stub_range(idx, 22u, 0u, 4u)) : 0u;
            if ((af & mask) == 0u) continue;
        }

        /* —— 按主键精确过滤（组织树上勾选了若干车辆）—— */
        if (f->id_count > 0)
        {
            char vid[24];
            int   hit = 0, k;

            snprintf(vid, sizeof(vid), "%06u", idx + 1u);   /* 与 make_vehicle_item 的 vehicleId 同规则 */
            for (k = 0; k < f->id_count; k++)
                if (strcmp(f->ids[k], vid) == 0) { hit = 1; break; }
            if (!hit) continue;
        }

        /* —— 关键字过滤（车牌 / 自编号）—— */
        if (f->keyword[0])
        {
            char plate[32], self_code[32];
            snprintf(plate, sizeof(plate), "%s%c%05u",
                     k_plate_prefix[idx % (sizeof(k_plate_prefix) / sizeof(char *))],
                     (char)('A' + stub_range(idx, 1u, 0u, 25u)),
                     stub_range(idx, 2u, 10000u, 99999u));
            snprintf(self_code, sizeof(self_code), "QC%05u", idx % 100000u);
            if (!strstr(plate, f->keyword) && !strstr(self_code, f->keyword))
                continue;
        }

        if (total >= offset && written < limit)
        {
            cJSON *it = make_vehicle_item(idx, org_id, g_bcfg.vehicle_online);
            if (!it) return -1;
            cJSON_AddItemToArray(items, it);
            ++written;
        }
        ++total;
    }
    *total_out = total;
    return 0;
}
