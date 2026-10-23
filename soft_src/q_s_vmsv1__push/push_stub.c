/**
 * @file push_stub.c
 * @brief 推送微服务桩数据：组织树 / 测试账号 / 6 台测试车 / TRAJ-ALARM 组帧 / 自检
 *
 * 桩数据与客户端 Mock **刻意同形**：车辆主键一律「车牌+颜色」（d_vehicleinfo.vehicle_id
 * 的库表注释语义），否则"Mock 能过、真桩全废"。坐标以 113.57E/42.44N（包头一带）为
 * 中心撒点，与客户端 mock 的可见区域一致，UI 上"实时在动"才看得见。
 *
 * 报警按【用户组织子树】过滤：pushd 只把 org 在用户所属组织子树内的报警下发给该会话
 * （轨迹不按组织过滤 —— 轨迹是"我订阅了这辆车"的精确语义）。
 */
#include "srv_push.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ===================== 测试组织树（极简，覆盖多层） =====================
 *   1 运输公司（根）
 *   ├─ 2 塔运司
 *   │   └─ 21 塔运一队（叶子）
 *   └─ 3 沙运司
 *       └─ 31 沙运一队（叶子）
 * 另有一台车直接挂 2（中间节点）与一台挂 1（根），用于验证子树判定不假设"车必在叶子"。 */
typedef struct push_org { const char *id; const char *parent; const char *name; } push_org_t;

static const push_org_t k_orgs[] = {
    { "1",  "",  "运输公司" },
    { "2",  "1", "塔运司" },
    { "3",  "1", "沙运司" },
    { "21", "2", "塔运一队" },
    { "31", "3", "沙运一队" },
};

/* 用户名 -> 所属组织（测试表）。真实部署应由登录服务/会话存储提供。 */
typedef struct push_user { const char *name; const char *org_id; } push_user_t;

static const push_user_t k_users[] = {
    { "qiaoshui", "1" },   /* e2e 测试账号：根组织，看全部报警 */
    { "tayun",    "2" },   /* 塔运司账号：只看塔运司子树（2 及其下级 21）的报警 */
};

int push_org_in_subtree(const char *root, const char *org)
{
    const char *cur;
    size_t      i;
    int         guard = 0;

    if (!root || !org || !root[0] || !org[0])
        return 0;
    if (strcmp(root, org) == 0)
        return 1;

    /* 从 org 沿父链上爬，碰到 root 即在子树内；guard 防数据错误成环 */
    cur = org;
    while (cur && cur[0] && guard++ < 32)
    {
        for (i = 0u; i < sizeof(k_orgs) / sizeof(k_orgs[0]); ++i)
        {
            if (strcmp(k_orgs[i].id, cur) != 0)
                continue;
            if (strcmp(k_orgs[i].parent, root) == 0)
                return 1;
            cur = k_orgs[i].parent;
            break;
        }
        if (i >= sizeof(k_orgs) / sizeof(k_orgs[0]))
            break;                          /* 未知 org：爬到顶也没匹配 */
    }
    return 0;
}

const char *push_user_org(const char *user_name)
{
    size_t i;

    if (user_name && user_name[0])
    {
        for (i = 0u; i < sizeof(k_users) / sizeof(k_users[0]); ++i)
            if (strcmp(k_users[i].name, user_name) == 0)
                return k_users[i].org_id;
    }
    /* 未知用户：桩行为 fail-open 到根组织（看全部）并告警。
       生产实现应拒绝或按最小权限组织放行 —— 这里只是测试替身。 */
    return "1";
}

/* ===================== 测试车辆 ===================== */
typedef struct sim_veh {
    char     vehicle_id[PUSH_ID_MAX];   /* 主键：车牌+颜色 */
    char     plate[16];
    char     org_id[PUSH_ID_MAX];
    double   lon, lat;
    uint32_t speed_kph;
    double   heading;                   /* 弧度 */
    int      online;
    uint32_t active_alarm;              /* 活跃报警位（Q_PUSH_ALARM_*），0=无 */
    uint64_t alarm_until_ms;
    uint64_t next_emit_ms;              /* 下次轨迹上报时刻 */
    uint64_t phase_ms;                  /* 错峰相位 */
} sim_veh_t;

static sim_veh_t g_veh[PUSH_VEH_MAX];
static uint32_t g_veh_n = PUSH_VEH_MAX;

/* 位置边界（超出则反射 heading，把车拢在可见区域内） */
#define PUSH_LON_MIN  113.40
#define PUSH_LON_MAX  113.80
#define PUSH_LAT_MIN  42.25
#define PUSH_LAT_MAX  42.65

static const struct {
    const char *vid; const char *plate; const char *org;
    double lon, lat; uint32_t kph;
} k_veh_seed[] = {
    { "京A10001-1", "京A10001", "21", 113.5729, 42.4431, 62 },
    { "京A10002-1", "京A10002", "21", 113.5810, 42.4502, 48 },
    { "京A10003-2", "京A10003", "31", 113.5601, 42.4390, 75 },
    { "京A10004-2", "京A10004", "31", 113.5902, 42.4310, 55 },
    { "京A10005-3", "京A10005", "2",  113.5480, 42.4600, 40 },
    { "京A10006-3", "京A10006", "1",  113.6100, 42.4200, 88 },
};

/* 报警定义：错峰触发，周期各异，覆盖三个不同组织的车 */
typedef struct sim_alarm {
    const char *vid;
    uint32_t    type;      /* Q_PUSH_ALARM_* */
    const char *text;
    uint32_t    period_ms;
    uint64_t    next_ms;
    int         fired;     /* 本 tick 是否触发（交付前收集，交付后清除） */
} sim_alarm_t;

static sim_alarm_t k_alarms[] = {
    { "京A10001-1", Q_PUSH_ALARM_OVERSPEED, "超速报警：时速 92km/h，超过阈值 80km/h", 12000, 0, 0 },
    { "京A10003-2", Q_PUSH_ALARM_FATIGUE,   "疲劳驾驶：连续驾驶 4 小时未休息",       17000, 0, 0 },
    { "京A10005-3", Q_PUSH_ALARM_GEOFENCE,  "越界报警：车辆驶离电子围栏区域",         23000, 0, 0 },
};

static const char *k_addrs[] = {
    "包头市青山区", "包头市昆都仑区", "鄂尔多斯市东胜区", "呼和浩特市赛罕区",
    "巴彦淖尔市临河区", "乌兰察布市集宁区", "包头市东河区", "土默特右旗"
};

/* 位置 -> 地址描述：按坐标分桶取一个固定地名（随位置移动而变化，UI 上看得出在动） */
static const char *addr_of(double lon, double lat)
{
    unsigned h = (unsigned)((int)(lon * 1000.0) * 31u + (int)(lat * 1000.0));
    return k_addrs[h % (sizeof(k_addrs) / sizeof(k_addrs[0]))];
}

/* 确定性 LCG（桩数据要可复现：同一时刻各客户端看到同一台车的同一状态） */
static uint64_t g_lcg = 0x2545F4914F6CDD1DULL;
static uint32_t lcg_next(void)
{
    g_lcg = g_lcg * 6364136223846793005ULL + 1442695040888963407ULL;
    return (uint32_t)(g_lcg >> 33);
}

void push_stub_reset(uint64_t now_ms)
{
    uint32_t i;

    g_lcg = 0x2545F4914F6CDD1DULL;
    for (i = 0u; i < PUSH_VEH_MAX; ++i)
    {
        sim_veh_t *v = &g_veh[i];

        memset(v, 0, sizeof(*v));
        snprintf(v->vehicle_id, sizeof(v->vehicle_id), "%s", k_veh_seed[i].vid);
        snprintf(v->plate, sizeof(v->plate), "%s", k_veh_seed[i].plate);
        snprintf(v->org_id, sizeof(v->org_id), "%s", k_veh_seed[i].org);
        v->lon         = k_veh_seed[i].lon;
        v->lat         = k_veh_seed[i].lat;
        v->speed_kph   = k_veh_seed[i].kph;
        v->heading     = (double)(lcg_next() % 628u) / 100.0;   /* 0..6.28 rad */
        v->online      = 1;
        v->active_alarm = 0u;
        v->alarm_until_ms = 0u;
        v->phase_ms    = (uint64_t)i * (PUSH_EMIT_MS_DEF / PUSH_VEH_MAX + 1u);
        v->next_emit_ms = now_ms + v->phase_ms;
    }
    for (i = 0u; i < sizeof(k_alarms) / sizeof(k_alarms[0]); ++i)
    {
        k_alarms[i].next_ms = now_ms + 3000u + (uint64_t)i * 5000u;
        k_alarms[i].fired   = 0;
    }
    g_veh_n = PUSH_VEH_MAX;
}

uint32_t push_veh_count(void) { return g_pcfg.vehicles ? g_pcfg.vehicles : PUSH_VEH_MAX; }

static sim_veh_t *veh_at(uint32_t i)
{
    if (i >= push_veh_count())
        return NULL;
    return &g_veh[i];
}

const char *push_veh_id(uint32_t i)
{
    sim_veh_t *v = veh_at(i);
    return v ? v->vehicle_id : "";
}

const char *push_veh_org(uint32_t i)
{
    sim_veh_t *v = veh_at(i);
    return v ? v->org_id : "";
}

const char *push_veh_plate(uint32_t i)
{
    sim_veh_t *v = veh_at(i);
    return v ? v->plate : "";
}

/* ===================== TLV 组帧（与 qc_push_parse_traj/parse_alarm 严格对齐） ===================== */

static int tlv_put(uint8_t *out, size_t cap, size_t *off, uint16_t tag,
                   const void *val, uint16_t vlen)
{
    size_t n = 0u;

    if (q_codec_tlv_encode(out + *off, cap - *off, tag,
                           (const uint8_t *)val, vlen, &n) != 0)
        return -1;
    *off += n;
    return 0;
}

static int tlv_put_text(uint8_t *out, size_t cap, size_t *off, uint16_t tag, const char *s)
{
    size_t len = s ? strlen(s) : 0u;

    if (len == 0u)
        return 0;                       /* 空文本不发：解析侧有默认值 */
    if (len > 0xFFFFu)
        return -1;
    return tlv_put(out, cap, off, tag, s, (uint16_t)len);
}

static int tlv_put_u32(uint8_t *out, size_t cap, size_t *off, uint16_t tag, uint32_t v)
{
    uint8_t w[4];

    w[0] = (uint8_t)(v >> 24);
    w[1] = (uint8_t)(v >> 16);
    w[2] = (uint8_t)(v >> 8);
    w[3] = (uint8_t)(v);
    return tlv_put(out, cap, off, tag, w, 4);
}

/* double -> IEEE754 位型大端 8B（memcpy 取位型，逐字节 BE 序列化；
   与 qc_push_put_f64 同一约定，一端改了另一端静默错位） */
static int tlv_put_f64(uint8_t *out, size_t cap, size_t *off, uint16_t tag, double d)
{
    uint64_t be;
    uint8_t  w[8];
    int      i;

    memcpy(&be, &d, sizeof(be));
    for (i = 0; i < 8; ++i)
        w[i] = (uint8_t)(be >> (56 - 8 * i));
    return tlv_put(out, cap, off, tag, w, 8);
}

size_t push_build_traj(uint8_t *out, size_t cap, const char *vid,
                       double lon, double lat, uint32_t speed, uint32_t online,
                       uint32_t alarm_flags)
{
    size_t off = 0u;
    char   tv[24];

    if (!out || cap == 0u || !vid || !vid[0])
        return 0;
    push_now_str(tv, sizeof(tv));

    if (tlv_put_text(out, cap, &off, Q_IPC_T_PUSH_VID, vid) != 0)   return 0;
    if (tlv_put_f64(out, cap, &off, Q_IPC_T_PUSH_LON, lon) != 0)    return 0;
    if (tlv_put_f64(out, cap, &off, Q_IPC_T_PUSH_LAT, lat) != 0)    return 0;
    if (tlv_put_u32(out, cap, &off, Q_IPC_T_PUSH_SPEED, speed) != 0) return 0;
    if (tlv_put_u32(out, cap, &off, Q_IPC_T_PUSH_ONLINE, online) != 0) return 0;
    if (alarm_flags &&
        tlv_put_u32(out, cap, &off, Q_IPC_T_PUSH_AFLAGS, alarm_flags) != 0) return 0;
    if (tlv_put_text(out, cap, &off, Q_IPC_T_PUSH_TV, tv) != 0)     return 0;
    if (tlv_put_text(out, cap, &off, Q_IPC_T_PUSH_ADDR, addr_of(lon, lat)) != 0) return 0;
    return off;
}

size_t push_build_alarm(uint8_t *out, size_t cap, const char *vid, const char *org,
                        const char *plate, uint32_t alarm_type,
                        double lon, double lat, const char *text, const char *addr)
{
    size_t off = 0u;
    char   tv[24];

    if (!out || cap == 0u || !vid || !vid[0])
        return 0;
    push_now_str(tv, sizeof(tv));

    if (tlv_put_text(out, cap, &off, Q_IPC_T_PUSH_VID, vid) != 0)    return 0;
    if (tlv_put_text(out, cap, &off, Q_IPC_T_PUSH_ORG, org) != 0)    return 0;
    if (tlv_put_text(out, cap, &off, Q_IPC_T_PUSH_PLATE, plate) != 0) return 0;
    if (tlv_put_u32(out, cap, &off, Q_IPC_T_PUSH_ATYPE, alarm_type) != 0) return 0;
    if (tlv_put_f64(out, cap, &off, Q_IPC_T_PUSH_LON, lon) != 0)     return 0;
    if (tlv_put_f64(out, cap, &off, Q_IPC_T_PUSH_LAT, lat) != 0)     return 0;
    if (tlv_put_text(out, cap, &off, Q_IPC_T_PUSH_TV, tv) != 0)      return 0;
    if (tlv_put_text(out, cap, &off, Q_IPC_T_PUSH_ATEXT, text) != 0) return 0;
    if (tlv_put_text(out, cap, &off, Q_IPC_T_PUSH_ADDR, addr) != 0)  return 0;
    return off;
}

/* ===================== 模拟推进（push_sim.c 持 g_lock 调用） ===================== */

/* 本 tick 触发的报警（交付前收集，保证每台车每周期只触发一次，与 session 数无关） */
#define PUSH_FIRED_MAX 8u
typedef struct fired_alarm {
    const sim_veh_t *veh;
    const sim_alarm_t *def;
} fired_alarm_t;

static uint32_t sim_tick_locked(uint64_t now_ms, fired_alarm_t *fired, uint32_t fired_cap)
{
    uint32_t i, nfired = 0u;
    double   dt = (double)g_pcfg.tick_ms / 1000.0;
    uint32_t n = push_veh_count();

    for (i = 0u; i < n; ++i)
    {
        sim_veh_t *v = &g_veh[i];
        double     dist, dlon, dlat, jit;

        /* 运动：速度 * 步长，heading 带确定性抖动；越界反射 */
        dist = (double)v->speed_kph / 3.6 * dt;
        jit  = ((double)(lcg_next() % 200u) - 100.0) / 1000.0;   /* ±0.1 rad */
        v->heading += jit;
        dlat = dist * cos(v->heading) / 111320.0;
        dlon = dist * sin(v->heading) / (111320.0 * cos(v->lat * M_PI / 180.0));
        v->lat += dlat;
        v->lon += dlon;
        if (v->lat < PUSH_LAT_MIN || v->lat > PUSH_LAT_MAX) { v->lat -= 2.0 * dlat; v->heading = -v->heading; }
        if (v->lon < PUSH_LON_MIN || v->lon > PUSH_LON_MAX) { v->lon -= 2.0 * dlon; v->heading = M_PI - v->heading; }

        /* 报警活跃窗口到期即清除（窗口内 TRAJ 的 AFLAGS 会带该位） */
        if (v->active_alarm && now_ms >= v->alarm_until_ms)
            v->active_alarm = 0u;

        /* 到点触发：改车状态只在这里做一次，收集后由交付循环按组织子树分发 */
        if (!g_pcfg.no_alarms)
        {
            uint32_t k;
            for (k = 0u; k < sizeof(k_alarms) / sizeof(k_alarms[0]); ++k)
            {
                sim_alarm_t *a = &k_alarms[k];
                if (a->fired || now_ms < a->next_ms)
                    continue;
                if (strcmp(a->vid, v->vehicle_id) != 0)
                    continue;
                a->fired = 1;
                v->active_alarm  |= a->type;
                v->alarm_until_ms = now_ms + PUSH_ALARM_WINOW_MS;
                if (fired && nfired < fired_cap)
                {
                    fired[nfired].veh = v;
                    fired[nfired].def = a;
                    ++nfired;
                }
            }
        }
    }
    return nfired;
}

/* 供 push_sim.c 使用：推进 + 交付一 tick 的全部数据（持 g_lock） */
void push_sim_deliver_locked(uint64_t now_ms)
{
    fired_alarm_t  fired[PUSH_FIRED_MAX];
    uint32_t       nfired, i, k;
    uint32_t       n = push_veh_count();
    push_session_t *s;
    uint8_t        inner[PUSH_MAXMSG];        /* 推送域 TLV 序列（push_build_* 产物） */
    uint8_t        wire[PUSH_MAXMSG + 16u];   /* 外层帧：TLV(CMD)+TLV(BODY) */
    size_t         ilen, wlen;

    nfired = sim_tick_locked(now_ms, fired, PUSH_FIRED_MAX);

    for (s = push_session_first(); s; s = s->next)
    {
        if (!s->active || s->dead)
            continue;

        /* 轨迹：订阅了这辆车且到点就发（未订阅不发；订阅即刻到点的首帧立即下发） */
        for (i = 0u; i < n; ++i)
        {
            sim_veh_t *v = &g_veh[i];
            int        subd = 0;

            for (k = 0u; k < s->nvid; ++k)
                if (strcmp(s->vids[k], v->vehicle_id) == 0) { subd = 1; break; }
            if (!subd || now_ms < v->next_emit_ms)
                continue;

            ilen = push_build_traj(inner, sizeof(inner), v->vehicle_id, v->lon, v->lat,
                                   v->speed_kph, (uint32_t)v->online, v->active_alarm);
            if (ilen == 0u)
                continue;
            wlen = push_wrap_cmd_body(Q_CHAN_CMD_PUSH_TRAJ, inner, ilen, wire, sizeof(wire));
            if (wlen == 0u)
                continue;
            if (push_send_frame(s->fd, wire, wlen) != 0)
            {
                s->dead = 1;               /* 发送失败：worker 随后拆链 */
                break;
            }
            v->next_emit_ms = now_ms + (g_pcfg.emit_ms ? g_pcfg.emit_ms : PUSH_EMIT_MS_DEF);
        }
        if (s->dead)
            continue;

        /* 报警：按用户组织子树过滤（轨迹不过滤 —— 那是"我订了这辆车"的精确语义） */
        if (g_pcfg.no_alarms)
            continue;
        for (i = 0u; i < nfired; ++i)
        {
            const sim_veh_t   *v = fired[i].veh;
            const sim_alarm_t *a = fired[i].def;

            if (!push_org_in_subtree(s->org_id, v->org_id))
                continue;
            ilen = push_build_alarm(inner, sizeof(inner), v->vehicle_id, v->org_id,
                                    v->plate, a->type, v->lon, v->lat, a->text,
                                    addr_of(v->lon, v->lat));
            if (ilen == 0u)
                continue;
            wlen = push_wrap_cmd_body(Q_CHAN_CMD_PUSH_ALARM, inner, ilen, wire, sizeof(wire));
            if (wlen == 0u)
                continue;
            if (push_send_frame(s->fd, wire, wlen) != 0)
            {
                s->dead = 1;
                break;
            }
        }
    }

    for (i = 0u; i < sizeof(k_alarms) / sizeof(k_alarms[0]); ++i)
    {
        if (!k_alarms[i].fired)
            continue;
        k_alarms[i].fired = 0;
        k_alarms[i].next_ms = now_ms + k_alarms[i].period_ms;
    }
}

/* ===================== 自检（--selftest） =====================
 * 不连 reactor、不起线程，纯函数级验证：子树判定 / 账号映射 / TLV 线格式 /
 * 订阅集合语义。线格式断言是重点 —— 帧里一个字节写错，客户端侧表现为
 * "经纬度是乱码/时间不对"，而这种错在 e2e 里极难定位。 */

/* 从 TLV 序列里取指定 tag 的值指针/长度；找不到返回 NULL */
static const uint8_t *tlv_find(const uint8_t *buf, size_t len, uint16_t tag, uint16_t *vlen)
{
    size_t off = 0u;

    while (off + 4u <= len)
    {
        uint16_t       t, vl;
        const uint8_t *v;
        size_t         used;

        if (q_codec_tlv_decode(buf + off, len - off, &t, &v, &vl, &used) != 0)
            break;
        off += used;
        if (t == tag)
        {
            if (vlen) *vlen = vl;
            return v;
        }
    }
    return NULL;
}

static int check(int cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "ok" : "FAIL", what);
    return cond ? 0 : 1;
}

int push_selftest(void)
{
    int fails = 0;
    uint8_t buf[1024];
    size_t  blen;
    uint16_t vl;
    const uint8_t *v;
    uint64_t be;
    double   d;
    push_session_t s;

    printf("== push selftest ==\n");

    /* ---- 1. 组织子树判定 ---- */
    fails += check(push_org_in_subtree("1", "1") == 1, "subtree(1,1) 自身");
    fails += check(push_org_in_subtree("1", "21") == 1, "subtree(1,21) 跨两层");
    fails += check(push_org_in_subtree("2", "21") == 1, "subtree(2,21) 直属下级");
    fails += check(push_org_in_subtree("2", "31") == 0, "subtree(2,31) 旁支不匹配");
    fails += check(push_org_in_subtree("21", "2") == 0, "subtree(21,2) 反向不匹配");
    fails += check(push_org_in_subtree("", "21") == 0, "subtree(空,21) 拒绝");
    fails += check(push_org_in_subtree("2", "") == 0, "subtree(2,空) 拒绝");

    /* ---- 2. 账号 -> 组织 ---- */
    fails += check(strcmp(push_user_org("qiaoshui"), "1") == 0, "qiaoshui -> org 1");
    fails += check(strcmp(push_user_org("tayun"), "2") == 0, "tayun -> org 2");
    fails += check(strcmp(push_user_org("nosuch"), "1") == 0, "未知用户 -> 根组织(fail-open)");

    /* ---- 3. TRAJ 线格式 ---- */
    blen = push_build_traj(buf, sizeof(buf), "京A10001-1", 113.5729, 42.4431, 62u, 1u, 0x0002u);
    fails += check(blen > 0u, "build_traj 非空");
    v = tlv_find(buf, blen, Q_IPC_T_PUSH_VID, &vl);
    fails += check(v && vl == 11u && memcmp(v, "京A10001-1", 11u) == 0, "traj VID 文本");
    v = tlv_find(buf, blen, Q_IPC_T_PUSH_SPEED, &vl);
    fails += check(v && vl == 4u && v[0] == 0 && v[1] == 0 && v[2] == 0 && v[3] == 62, "traj SPEED=62 大端");
    v = tlv_find(buf, blen, Q_IPC_T_PUSH_ONLINE, &vl);
    fails += check(v && vl == 4u && v[3] == 1u, "traj ONLINE=1 大端");
    v = tlv_find(buf, blen, Q_IPC_T_PUSH_AFLAGS, &vl);
    fails += check(v && vl == 4u && v[3] == 0x02u, "traj AFLAGS=0x0002 大端");
    v = tlv_find(buf, blen, Q_IPC_T_PUSH_LON, &vl);
    fails += check(v && vl == 8u, "traj LON 8B");
    if (v && vl == 8u)
    {
        be = 0;
        for (int i = 0; i < 8; ++i) be = (be << 8) | v[i];
        memcpy(&d, &be, sizeof(d));
        fails += check(d > 113.5728 && d < 113.5730, "traj LON 值往返");
    }
    v = tlv_find(buf, blen, Q_IPC_T_PUSH_TV, &vl);
    fails += check(v && vl == 19u, "traj TV 19B (YYYY-MM-DD HH:MM:SS)");
    v = tlv_find(buf, blen, Q_IPC_T_PUSH_ADDR, &vl);
    fails += check(v && vl > 0u, "traj ADDR 非空");

    /* ---- 4. ALARM 线格式 ---- */
    blen = push_build_alarm(buf, sizeof(buf), "京A10003-2", "31", "京A10003",
                            Q_PUSH_ALARM_FATIGUE, 113.5601, 42.4390,
                            "疲劳驾驶：连续驾驶 4 小时未休息", "鄂尔多斯市东胜区");
    fails += check(blen > 0u, "build_alarm 非空");
    v = tlv_find(buf, blen, Q_IPC_T_PUSH_ATYPE, &vl);
    fails += check(v && vl == 4u && v[3] == 0x02u, "alarm ATYPE=FATIGUE(0x0002) 大端");
    v = tlv_find(buf, blen, Q_IPC_T_PUSH_ORG, &vl);
    fails += check(v && vl == 2u && memcmp(v, "31", 2u) == 0, "alarm ORG=31");
    v = tlv_find(buf, blen, Q_IPC_T_PUSH_PLATE, &vl);
    fails += check(v && vl > 0u, "alarm PLATE 非空");
    v = tlv_find(buf, blen, Q_IPC_T_PUSH_ATEXT, &vl);
    fails += check(v && vl > 0u, "alarm ATEXT 非空");

    /* ---- 4b. 外层包装（pushd -> netd_B 帧契约：TLV(CMD)+TLV(BODY)）----
       曾经的 bug：交付循环直接发 inner（推送域 TLV），探针按契约解析时
       表现为"一帧都收不到"且极难定位 —— 这里把外层钉死。 */
    {
        uint8_t wire[1024];
        size_t  wlen;

        blen = push_build_traj(buf, sizeof(buf), "京A10001-1", 113.5729, 42.4431, 62u, 1u, 0u);
        fails += check(blen > 0u, "wrap: inner 非空");
        wlen = push_wrap_cmd_body(Q_CHAN_CMD_PUSH_TRAJ, buf, blen, wire, sizeof(wire));
        fails += check(wlen > blen, "wrap: 外层比 inner 长");
        v = tlv_find(wire, wlen, Q_IPC_T_CMD, &vl);
        fails += check(v && vl == 2u && v[0] == 0x00 && v[1] == 0x52u, "wrap: CMD=0x0052 在首");
        v = tlv_find(wire, wlen, Q_IPC_T_BODY, &vl);
        fails += check(v && vl == (uint16_t)blen && memcmp(v, buf, blen) == 0,
                       "wrap: BODY 原样包含 inner");

        blen = push_build_alarm(buf, sizeof(buf), "京A10003-2", "31", "京A10003",
                                Q_PUSH_ALARM_FATIGUE, 113.5601, 42.4390, "疲劳驾驶", "东胜区");
        wlen = push_wrap_cmd_body(Q_CHAN_CMD_PUSH_ALARM, buf, blen, wire, sizeof(wire));
        v = tlv_find(wire, wlen, Q_IPC_T_CMD, &vl);
        fails += check(v && vl == 2u && v[0] == 0x00 && v[1] == 0x53u, "wrap: ALARM CMD=0x0053");
        v = tlv_find(wire, wlen, Q_IPC_T_BODY, &vl);
        fails += check(v && vl == (uint16_t)blen && memcmp(v, buf, blen) == 0,
                       "wrap: ALARM BODY 原样包含 inner");
    }

    /* ---- 5. 订阅集合语义（ADD 去重 / REMOVE 幂等 / 交换尾部删除） ---- */
    memset(&s, 0, sizeof(s));
    fails += check(push_sub_add(&s, "京A10001-1") == 0, "sub add #1");
    fails += check(push_sub_add(&s, "京A10002-1") == 0, "sub add #2");
    fails += check(push_sub_add(&s, "京A10001-1") == 0 && s.nvid == 2u, "sub add 去重");
    push_sub_remove(&s, "京A10001-1");
    fails += check(s.nvid == 1u && strcmp(s.vids[0], "京A10002-1") == 0, "sub remove 交换尾部");
    push_sub_remove(&s, "京A10009-9");
    fails += check(s.nvid == 1u, "sub remove 不存在项幂等");
    free(s.vids);

    /* ---- 6. 车辆种子 ---- */
    push_stub_reset(1000u);
    fails += check(push_veh_count() == 6u, "veh_count=6");
    fails += check(strcmp(push_veh_id(0), "京A10001-1") == 0, "veh[0] 主键=车牌+颜色");
    fails += check(strcmp(push_veh_org(4), "2") == 0, "veh[4] 挂中间组织 2");

    printf("== push selftest: %s ==\n", fails ? "FAIL" : "ALL PASS");
    return fails ? 1 : 0;
}
