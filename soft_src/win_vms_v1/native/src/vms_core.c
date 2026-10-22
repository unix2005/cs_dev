/* =============================================================================
 * vms_core.c — vms_api.h 的参考实现
 * -----------------------------------------------------------------------------
 * 两套数据源并存（按是否登录自动切换）：
 *   1. **未登录**：内置 Mock 数据 —— 让 C# + WinUI3 客户端在**没有真实后端**时
 *      也能端到端跑起来（压 UI、调布局、演示）。
 *   2. **已登录**（vms_login 成功）：会话由 l_4 的 qc_* 门面承载，
 *      数据面在 M2/M3 逐步替换为真实业务请求；M1 先打通登录闭环。
 *
 * 会话链路（M1）：
 *   C# -> vms_login -> libqclientcomm(qc_*) -> q_chan/q_net/q_codec/q_crypto
 *       -> [UDP SPA 敲门] -> netd:18099 -> UDS bizd -> 登录微服务 9002
 *   UI 层绝不直接 P/Invoke 底层 q_* 库（帧封装与 seq 窗口由库管理，手拼必错）。
 *
 * 编译（llvm-mingw / MinGW-w64，x64）：见 shell/build_vmscore_win.sh
 *   （需链接 libqclientcomm 及闭包 + Tongsuo libcrypto）
 *
 * 数据规模刻意与真实系统对齐：34554 台车、18714 台归属运输公司，用于压 UI。
 * ===========================================================================*/
#define VMS_BUILD_DLL 1
#include "vms_api.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "qc_client.h"        /* l_4 跨三端共享的客户端通信门面（qc_*） */
#include "include_ipc_proto.h" /* Q_PBKDF2_ITER_DEFAULT / Q_IPC_R_* 结果码 / 业务参数 tag */
#include "q_codec.h"          /* q_codec_tlv_decode：解析业务响应 TLV */
#include "cJSON.h"            /* 下游 JSON 解析（**不走** include_xml.h：后者依赖 libxml2） */

#ifdef _WIN32
#  include <windows.h>
#endif

/* --------------------------------- 常量 ----------------------------------- */
#define VMS_MOCK_TOTAL_VEHICLES  34554u
#define VMS_MOCK_TOTAL_ONLINE    10846u
#define VMS_MOCK_TOTAL_OFFLINE   23708u
#define VMS_MOCK_ALARM_UNHANDLED 46343u
/* 组织机构缓存上限：真实系统省级单位 + 地市级配送中心可到几百个，
   64 会在拉全量建树时被截断，故抬到 512（512 * sizeof(vms_company) = 64KB，随 ctx 一次分配）。 */
#define VMS_MAX_COMPANY          512u
#define VMS_PUSH_INTERVAL_MS     800u
#define VMS_PUSH_BATCH           24u

/* 线程本地错误串：不分配堆、不需要调用方 free */
#ifdef _MSC_VER
#  define VMS_TLS __declspec(thread)
#else
#  define VMS_TLS __thread
#endif

static VMS_TLS char g_last_error[256] = { 0 };

static void set_error(const char* fmt, int32_t code)
{
    (void)code;
    snprintf(g_last_error, sizeof(g_last_error), "%s", fmt ? fmt : "");
}

/* ------------------------------- 上下文定义 -------------------------------- */
struct vms_context {
    uint32_t   abi_version;
    int32_t    connected;
    vms_stats  stats;

    vms_company companies[VMS_MAX_COMPANY];
    size_t      company_count;

    vms_vehicle* vehicles;      /* 连续内存，一次分配，便于作为数组 blit 给 C# */
    size_t       vehicle_count;

    /* 每台车归属的公司 id（0 = 未分配） */
    uint32_t*    vehicle_company;

    vms_push_cb  push_cb;
    void*        push_user;
    vms_log_cb   log_cb;
    void*        log_user;

    /* —— M1：真实会话（qc_* 门面）—— 未登录时 sess==NULL，数据面走 mock —— */
    qc_session_t* sess;
    int32_t       logged_in;
    uint8_t       token[32];      /* 会话 Token（业务请求随带；本进程内留存，不外泄） */
    char          host[VMS_HOST_MAX];  /* 重连用：不保存口令，只保存地址与账号 */
    uint16_t      port;
    char          user[VMS_USER_MAX];
    char          pubkey_path[VMS_PATH_MAX];
    char          knock[VMS_HOST_MAX];
    char          spa_key_path[VMS_PATH_MAX];
    uint32_t      pbkdf2_iter;

#ifdef _WIN32
    CRITICAL_SECTION lock;
    HANDLE           push_thread;
    volatile LONG    push_stop;
#endif
};

/* M1：真实会话释放（定义在下方「M1：真实登录闭环」段，此处提前声明供 destroy/disconnect 使用） */
static void vms_session_close(struct vms_context* ctx);

/* --------------------------------- 工具 ----------------------------------- */
static uint64_t xorshift64(uint64_t* s)
{
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

static uint32_t rnd_range(uint64_t* s, uint32_t lo, uint32_t hi)
{
    if (hi <= lo) return lo;
    return lo + (uint32_t)(xorshift64(s) % (uint64_t)(hi - lo + 1));
}

static void now_string(char* dst, size_t cap, int offset_sec)
{
#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    /* 允许回退若干秒，模拟"最后定位时间" */
    if (offset_sec > 0) {
        FILETIME ft;
        ULARGE_INTEGER u;
        SystemTimeToFileTime(&st, &ft);
        u.LowPart  = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        u.QuadPart -= (ULONGLONG)offset_sec * 10000000ULL;
        ft.dwLowDateTime  = u.LowPart;
        ft.dwHighDateTime = u.HighPart;
        FileTimeToSystemTime(&ft, &st);
    }
    snprintf(dst, cap, "%04u-%02u-%02u %02u:%02u:%02u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
#else
    (void)offset_sec;
    snprintf(dst, cap, "2026-10-16 10:03:49");
#endif
}

static void day_string(char* dst, size_t cap)
{
#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(dst, cap, "%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
#else
    snprintf(dst, cap, "2026-10-16");
#endif
}

/* --------------------------------- 种子数据 -------------------------------- */
/* 组织机构：数值取自现有系统截图（截图右侧被截断的分公司用了合理估算值）。
 * 说明：root=(8960/18714) 小于所有分公司 count 之和(9392)之外的部分，
 * 语义上归入"未分配车辆"，因此分公司之和不必等于根节点 —— 这是真实系统的常态。 */
typedef struct seed_company { uint32_t id, parent, online, count; const char* name; } seed_company;

static const seed_company k_company_seed[] = {
    {  1, 0, 8960, 18714, "运输公司"      },
    {  2, 1,  293,   886, "塔运司"        },
    {  3, 1,  110,   464, "沙运司"        },
    {  4, 1,   89,   862, "一公司"        },
    {  5, 1, 1844,  2038, "长庆运输公司"  },
    {  6, 1,    0,   876, "华北运输公司"  },
    {  7, 1,   70,   450, "准噶尔分公司"  },
    {  8, 1,   87,   130, "冀东运输公司"  },
    {  9, 1,  316,   500, "黑龙江分公司"  },
    { 10, 1,  332,   615, "吉林分公司"    },
    { 11, 1,  269,   470, "辽宁分公司"    },
    { 12, 1,   93,   158, "大连分公司"    },
    { 13, 1,  210,   520, "内蒙古分公司"  },
    { 14, 1,  149,   398, "北京分公司"    },
    { 15, 1,  215,   272, "河北分公司"    },
    { 16, 1,   57,   107, "天津分公司"    },
    { 17, 1,  116,   167, "山西分公司"    },
    { 18, 1,  272,   344, "山东分公司"    },
    { 19, 1,   82,   135, "江苏分公司"    },

    /* —— 三级组织：省级分公司 → 地市级"配送中心" ——
     * 北京 / 河北 / 辽宁 三个省级单位下设配送中心，用于验证组织树的**多层**展开与
     * "点父节点只看下级单位、点叶子单位才加载车辆"这条交互约定。
     * 计数刻意让子项之和 == 父项（online 也同样对齐），这样父节点的聚合值与下级自洽。 */
    { 1101, 11, 150,  250, "沈阳配送中心"     },
    { 1102, 11, 119,  220, "大连配送中心"     },
    { 1401, 14,  80,  210, "北京朝阳配送中心" },
    { 1402, 14,  69,  188, "北京海淀配送中心" },
    { 1501, 15, 120,  160, "石家庄配送中心"   },
    { 1502, 15,  95,  112, "保定配送中心"     },
};

static const char* k_plate_prefix[] = { "京", "津", "冀", "晋", "蒙", "辽", "吉", "黑", "鲁", "苏", "陕", "甘" };
static const char* k_cities[] = {
    "北京市朝阳区", "北京市海淀区", "天津市滨海新区", "石家庄市裕华区", "太原市小店区",
    "呼和浩特市赛罕区", "沈阳市铁西区", "长春市朝阳区", "哈尔滨市南岗区", "济南市历下区",
    "西安市雁塔区", "兰州市城关区", "保定市莲池区", "唐山市路北区", "青岛市市南区",
    "银川市金凤区", "乌海市海勃湾区", "鄂尔多斯市东胜区", "榆林市榆阳区", "克拉玛依市克拉玛依区"
};
static const char* k_issues[] = {
    "超速报警", "疲劳驾驶", "越界报警", "掉线告警", "GNSS 失锁", "夜间行驶", "急加速", "急减速"
};

/* 经度 113.57 / 纬度 42.44 附近（截图中心点）撒点 */
static void seed_vehicle(vms_vehicle* v, uint32_t idx, uint64_t* rs)
{
    char plate_letter;
    memset(v, 0, sizeof(*v));
    v->id = idx + 1;

    snprintf(v->self_code, sizeof(v->self_code), "QC%05u", idx % 100000u);
    plate_letter = (char)('A' + rnd_range(rs, 0, 25));
    snprintf(v->plate, sizeof(v->plate), "%s%c%05u",
             k_plate_prefix[rnd_range(rs, 0, (uint32_t)(sizeof(k_plate_prefix) / sizeof(char*)) - 1)],
             plate_letter, rnd_range(rs, 1000, 99999));

    /* 前 10846 台在线，其余离线 —— 与状态栏统计严格对齐 */
    v->online = (idx < VMS_MOCK_TOTAL_ONLINE) ? 1u : 0u;
    v->offline_seconds = v->online ? 0u : rnd_range(rs, 60, 86400 * 7);
    v->speed_kph = v->online ? (float)rnd_range(rs, 0, 95) : 0.0f;
    v->lon = 113.572906 + ((double)rnd_range(rs, 0, 4000) - 2000.0) / 10000.0;
    v->lat = 42.443101 + ((double)rnd_range(rs, 0, 3000) - 1500.0) / 10000.0;
    now_string(v->time, sizeof(v->time), v->online ? 0 : (int)rnd_range(rs, 60, 86400));
    day_string(v->day, sizeof(v->day));
    snprintf(v->location, sizeof(v->location), "%s",
             k_cities[rnd_range(rs, 0, (uint32_t)(sizeof(k_cities) / sizeof(char*)) - 1)]);
    v->alarm_flags = (rnd_range(rs, 0, 100) < 12) ? (1u << rnd_range(rs, 0, 4)) : VMS_ALARM_NONE;
    if (v->alarm_flags) {
        snprintf(v->issue, sizeof(v->issue), "%s",
                 k_issues[rnd_range(rs, 0, (uint32_t)(sizeof(k_issues) / sizeof(char*)) - 1)]);
    }
}

/* 该组织下面是否还挂着下级组织（决定它是不是"叶子单位"） */
static int has_company_children(const struct vms_context* ctx, uint32_t id)
{
    size_t k;
    for (k = 0; k < ctx->company_count; ++k) {
        if (ctx->companies[k].id != id && ctx->companies[k].parent_id == id)
            return 1;
    }
    return 0;
}

static void build_company_assignment(struct vms_context* ctx)
{
    /* 按各分公司 count 顺序切分车辆索引区间；超出公司覆盖范围的归 0（未分配）。
     *
     * 关键一条：**有下级组织的节点不单独分配车辆**。
     * 它的 count 是下级之和（如 北京分公司 = 朝阳 + 海淀 配送中心），
     * 车辆实体挂在叶子单位上；否则父节点会把同一批车再认领一遍，
     * "点北京分公司"和"点它下面的配送中心"就会查出两套互不相干的车辆。 */
    uint32_t cursor = 0;
    size_t   i;
    for (i = 1; i < ctx->company_count; ++i) {
        uint32_t n, k;
        if (has_company_children(ctx, ctx->companies[i].id)) continue;

        n = ctx->companies[i].count;
        for (k = 0; k < n && cursor < ctx->vehicle_count; ++k, ++cursor)
            ctx->vehicle_company[cursor] = ctx->companies[i].id;
    }
}

/* -------------------------------- 推送线程 --------------------------------- */
#ifdef _WIN32
static DWORD WINAPI push_thread_proc(LPVOID param)
{
    struct vms_context* ctx = (struct vms_context*)param;
    vms_vehicle batch[VMS_PUSH_BATCH];
    uint64_t rs = 0x9E3779B97F4A7C15ULL;

    while (InterlockedCompareExchange(&ctx->push_stop, 0, 0) == 0) {
        size_t n = 0;
        size_t i;

        Sleep(VMS_PUSH_INTERVAL_MS);
        if (InterlockedCompareExchange(&ctx->push_stop, 0, 0) != 0) break;
        if (!ctx->push_cb) continue;

        /* 在锁内只做数据快照，回调放到锁外调用 —— 回调里若再调本库 API 不会自锁死 */
        EnterCriticalSection(&ctx->lock);
        for (i = 0; i < VMS_PUSH_BATCH && ctx->vehicle_count > 0; ++i) {
            /* 偏向低位索引：客户端首屏加载的是前 200 行，让推送落在可见行上，
             * "实时"才看得出来。真实系统是后端按需推全量，这里只是 mock 的取舍。 */
            uint64_t pick = xorshift64(&rs);
            size_t idx = (pick % 4 == 0) ? (size_t)(pick % ctx->vehicle_count) : (size_t)(pick % 400);
            vms_vehicle* v = &ctx->vehicles[idx];
            v->speed_kph = v->online ? (float)rnd_range(&rs, 0, 95) : 0.0f;
            v->lon += ((double)rnd_range(&rs, 0, 200) - 100.0) / 100000.0;
            v->lat += ((double)rnd_range(&rs, 0, 200) - 100.0) / 100000.0;
            now_string(v->time, sizeof(v->time), 0);
            batch[n++] = *v;
        }
        LeaveCriticalSection(&ctx->lock);

        ctx->push_cb(batch, n, ctx->push_user);
    }
    return 0;
}
#endif

/* ================================ 导出实现 ================================ */
uint32_t VMS_CALL vms_abi_version(void) { return VMS_ABI_VERSION; }

const char* VMS_CALL vms_version_string(void)
{
    static char ver[32];
    if (!ver[0]) snprintf(ver, sizeof(ver), "%u.%u.%u", VMS_VER_MAJOR, VMS_VER_MINOR, VMS_VER_PATCH);
    return ver;
}

size_t VMS_CALL vms_struct_size(int32_t kind)
{
    switch (kind) {
    case VMS_STRUCT_ABI_INFO: return sizeof(vms_abi_info);
    case VMS_STRUCT_STATS:    return sizeof(vms_stats);
    case VMS_STRUCT_COMPANY:  return sizeof(vms_company);
    case VMS_STRUCT_VEHICLE:  return sizeof(vms_vehicle);
    case VMS_STRUCT_QUERY:    return sizeof(vms_query);
    case VMS_STRUCT_CREDENTIALS: return sizeof(vms_credentials);
    default:                  return 0;
    }
}

int32_t VMS_CALL vms_create(const char* config_json, vms_context** out_ctx)
{
    struct vms_context* ctx;
    uint64_t rs = 0xC0FFEE123456789ULL;
    size_t   i;
    (void)config_json;

    if (!out_ctx) { set_error("out_ctx is null", VMS_ERR_INVALID_ARG); return VMS_ERR_INVALID_ARG; }
    *out_ctx = NULL;

    ctx = (struct vms_context*)calloc(1, sizeof(*ctx));
    if (!ctx) return VMS_ERR_NO_MEMORY;

    ctx->abi_version  = VMS_ABI_VERSION;
    ctx->vehicle_count = VMS_MOCK_TOTAL_VEHICLES;

    ctx->vehicles = (vms_vehicle*)calloc(ctx->vehicle_count, sizeof(vms_vehicle));
    ctx->vehicle_company = (uint32_t*)calloc(ctx->vehicle_count, sizeof(uint32_t));
    if (!ctx->vehicles || !ctx->vehicle_company) {
        free(ctx->vehicles); free(ctx->vehicle_company); free(ctx);
        return VMS_ERR_NO_MEMORY;
    }

    /* 组织机构 */
    ctx->company_count = sizeof(k_company_seed) / sizeof(k_company_seed[0]);
    if (ctx->company_count > VMS_MAX_COMPANY) ctx->company_count = VMS_MAX_COMPANY;
    for (i = 0; i < ctx->company_count; ++i) {
        const seed_company* s = &k_company_seed[i];
        ctx->companies[i].id = s->id;
        ctx->companies[i].parent_id = s->parent;
        ctx->companies[i].online = s->online;
        ctx->companies[i].count = s->count;
        snprintf(ctx->companies[i].name, sizeof(ctx->companies[i].name), "%s", s->name);
    }
    for (i = 0; i < ctx->company_count; ++i) {
        size_t k;
        uint32_t child = 0;
        for (k = 0; k < ctx->company_count; ++k)
            if (ctx->companies[k].parent_id == ctx->companies[i].id) ++child;
        ctx->companies[i].child_count = child;
    }

    for (i = 0; i < ctx->vehicle_count; ++i)
        seed_vehicle(&ctx->vehicles[i], (uint32_t)i, &rs);

    build_company_assignment(ctx);

    ctx->stats.total           = VMS_MOCK_TOTAL_VEHICLES;
    ctx->stats.online          = VMS_MOCK_TOTAL_ONLINE;
    ctx->stats.offline         = VMS_MOCK_TOTAL_OFFLINE;
    ctx->stats.excluded        = 0;
    ctx->stats.alarm_unhandled = VMS_MOCK_ALARM_UNHANDLED;
    ctx->stats.online_rate     = (float)VMS_MOCK_TOTAL_ONLINE * 100.0f / (float)VMS_MOCK_TOTAL_VEHICLES;

#ifdef _WIN32
    InitializeCriticalSection(&ctx->lock);
    ctx->push_stop = 0;
#endif

    *out_ctx = ctx;
    return VMS_OK;
}

void VMS_CALL vms_destroy(vms_context* ctx)
{
    if (!ctx) return;
#ifdef _WIN32
    vms_unsubscribe(ctx);
    DeleteCriticalSection(&ctx->lock);
#endif
    vms_session_close(ctx);          /* M1：真实会话（含 Token 清零） */
    free(ctx->vehicles);
    free(ctx->vehicle_company);
    free(ctx);
}

int32_t VMS_CALL vms_connect(vms_context* ctx, const char* endpoint)
{
    if (!ctx) return VMS_ERR_INVALID_ARG;
    /* 真实实现里此处做 TCP/HTTP 握手；mock 直接置位 */
    (void)endpoint;
    ctx->connected = 1;
    return VMS_OK;
}

int32_t VMS_CALL vms_disconnect(vms_context* ctx)
{
    if (!ctx) return VMS_ERR_INVALID_ARG;
    vms_unsubscribe(ctx);
    vms_session_close(ctx);          /* M1：断开即释放会话与 Token */
    ctx->connected = 0;
    return VMS_OK;
}

int32_t VMS_CALL vms_is_connected(const vms_context* ctx)
{
    return (ctx && ctx->connected) ? 1 : 0;
}

/* ======================= M1：真实登录闭环（qc_* 门面） ======================= */

static uint8_t* vms_read_file(const char* path, size_t* out_len)
{
    FILE*    f;
    uint8_t* buf;
    long     sz;

    if (!path || !out_len) return NULL;
    *out_len = 0;
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0) { fclose(f); return NULL; }
    rewind(f);
    buf = (uint8_t*)malloc((size_t)sz + 1u);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_len = (size_t)sz;
    return buf;
}

static int vms_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* hex -> 字节；返回字节数，失败 -1 */
static int vms_from_hex(const char* hex, uint8_t* out, size_t cap)
{
    size_t i, n;
    if (!hex || !out) return -1;
    n = strlen(hex) / 2u;
    if (n == 0u || n > cap || (strlen(hex) % 2u) != 0u) return -1;
    for (i = 0u; i < n; i++)
    {
        int hi = vms_hexval(hex[2u * i]);
        int lo = vms_hexval(hex[2u * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return (int)n;
}

/* "ip:port" -> host + port */
static int vms_split_ep(const char* s, char* host, size_t cap, int* port)
{
    const char* p;
    if (!s || !host || !port || !(p = strrchr(s, ':')) || p == s) return -1;
    if ((size_t)(p - s) >= cap) return -1;
    memcpy(host, s, (size_t)(p - s));
    host[p - s] = '\0';
    *port = atoi(p + 1);
    return (*port > 0) ? 0 : -1;
}

/* 关闭并释放真实会话（幂等）；Token 立即清零 */
static void vms_session_close(struct vms_context* ctx)
{
    if (!ctx) return;
    if (ctx->sess) { qc_session_destroy(ctx->sess); ctx->sess = NULL; }
    ctx->logged_in = 0;
    memset(ctx->token, 0, sizeof(ctx->token));
}

int32_t VMS_CALL vms_login(vms_context* ctx, const vms_credentials* cred)
{
    uint8_t* pub = NULL;
    size_t   pub_len = 0u;
    uint8_t  devid[32];
    size_t   devid_len = 0u;
    uint8_t  salt[64];
    size_t   salt_len = 0u;
    uint8_t  pwd_hash[32];
    uint32_t iter;
    int      rc;

    if (!ctx || !cred) return VMS_ERR_INVALID_ARG;
    if (!cred->host[0] || cred->port == 0 || !cred->user[0] || !cred->pubkey_path[0])
    {
        set_error("凭据不完整：host/port/user/pubkey_path 必填", VMS_ERR_INVALID_ARG);
        return VMS_ERR_INVALID_ARG;
    }

    /* 已登录则先关闭旧会话（一连接 = 一会话，不复用） */
    vms_session_close(ctx);

    /* 设备指纹：缺省用内置固定值（真实部署应取硬件指纹并妥善保管） */
    if (cred->devid_hex[0])
    {
        int n = vms_from_hex(cred->devid_hex, devid, sizeof(devid));
        if (n <= 0) { set_error("devid_hex 非法", VMS_ERR_INVALID_ARG); return VMS_ERR_INVALID_ARG; }
        devid_len = (size_t)n;
    }
    else
    {
        memcpy(devid, "QV_WIN_0001", 11);
        devid_len = 11u;
    }
    iter = cred->pbkdf2_iter ? cred->pbkdf2_iter : Q_PBKDF2_ITER_DEFAULT;

    /* 服务端静态公钥：握手时校验服务端身份，抗中间人 —— 必须内置/随客户端分发 */
    pub = vms_read_file(cred->pubkey_path, &pub_len);
    if (!pub || pub_len == 0u)
    {
        free(pub);
        set_error("读取服务端 SM2 公钥失败", VMS_ERR_INVALID_ARG);
        return VMS_ERR_INVALID_ARG;
    }

    ctx->sess = qc_session_create(pub, pub_len, 1u, 1u);
    free(pub);
    if (!ctx->sess)
    {
        set_error("qc_session_create 失败（公钥非法或内存不足）", VMS_ERR_INTERNAL);
        return VMS_ERR_INTERNAL;
    }

    /* SPA 敲门（可选）：未部署 knockd 时跳过 */
    if (cred->knock[0])
    {
        char        khost[VMS_HOST_MAX];
        int         kport = 0;
        const char* keyp  = cred->spa_key_path[0] ? cred->spa_key_path : NULL;
        uint8_t     key[32];
        size_t      keylen = 0u;
        uint8_t     uid[32];
        size_t      ulen;

        if (vms_split_ep(cred->knock, khost, sizeof(khost), &kport) != 0 || !keyp)
        {
            set_error("knock 参数非法（需 ip:port 且给出 spa_key_path）", VMS_ERR_INVALID_ARG);
            vms_session_close(ctx);
            return VMS_ERR_INVALID_ARG;
        }
        {
            uint8_t* kb = vms_read_file(keyp, &keylen);
            if (!kb || keylen == 0u || keylen > sizeof(key))
            {
                free(kb);
                set_error("SPA 密钥读取失败（需 16 字节）", VMS_ERR_INVALID_ARG);
                vms_session_close(ctx);
                return VMS_ERR_INVALID_ARG;
            }
            memcpy(key, kb, keylen);
            memset(kb, 0, keylen);
            free(kb);
        }
        memset(uid, 0, sizeof(uid));
        ulen = strlen(cred->user);
        if (ulen > sizeof(uid)) ulen = sizeof(uid);
        memcpy(uid, cred->user, ulen);

        rc = qc_knock(khost, kport, key, keylen, 0, uid,
                      cred->port, 300);
        memset(key, 0, sizeof(key));
        if (rc != 0)
        {
            set_error("SPA 敲门发送失败", VMS_ERR_NOT_CONNECTED);
            vms_session_close(ctx);
            return VMS_ERR_NOT_CONNECTED;
        }
    }

    rc = qc_connect(ctx->sess, cred->host, (int)cred->port);
    if (rc != 0)
    {
        set_error("TCP 连接或 SM2 握手失败（未敲门 / 端口不通 / 公钥不匹配？）", VMS_ERR_NOT_CONNECTED);
        vms_session_close(ctx);
        return VMS_ERR_NOT_CONNECTED;
    }

    rc = qc_hello(ctx->sess);
    if (rc != 0)
    {
        set_error("HELLO 失败", VMS_ERR_NOT_CONNECTED);
        vms_session_close(ctx);
        return (rc > 0) ? rc : VMS_ERR_NOT_CONNECTED;
    }

    memset(salt, 0, sizeof(salt));
    rc = qc_fetch_salt(ctx->sess, cred->user, salt, sizeof(salt), &salt_len);
    if (rc != 0 || salt_len == 0u)
    {
        set_error("拉取账号 salt 失败", VMS_ERR_NOT_CONNECTED);
        vms_session_close(ctx);
        return (rc > 0) ? rc : VMS_ERR_NOT_CONNECTED;
    }

    /* 本地派生：口令**不出进程**，外发的只是 32B 派生值 */
    rc = qc_pbkdf2_sm3((const uint8_t*)cred->password, strlen(cred->password),
                       salt, salt_len, iter, pwd_hash);
    if (rc != 0)
    {
        set_error("PBKDF2-SM3 派生失败", VMS_ERR_INTERNAL);
        vms_session_close(ctx);
        return VMS_ERR_INTERNAL;
    }

    rc = qc_login(ctx->sess, cred->user, pwd_hash,
                  cred->otp[0] ? cred->otp : "000000",
                  devid, devid_len, "vms_core/1.1", ctx->token);
    memset(pwd_hash, 0, sizeof(pwd_hash));   /* 派生值不滞留 */
    memset(salt, 0, sizeof(salt));
    if (rc != 0)
    {
        if (rc > 0)
            snprintf(g_last_error, sizeof(g_last_error), "登录被拒绝：结果码 %d", rc);
        else
            set_error("登录失败：连接中断", VMS_ERR_NOT_CONNECTED);
        vms_session_close(ctx);
        return (rc > 0) ? rc : VMS_ERR_NOT_CONNECTED;
    }

    ctx->logged_in = 1;
    ctx->connected = 1;
    /* 保存重连所需的最小信息（**不含口令**） */
    snprintf(ctx->host, sizeof(ctx->host), "%s", cred->host);
    ctx->port = cred->port;
    snprintf(ctx->user, sizeof(ctx->user), "%s", cred->user);
    snprintf(ctx->pubkey_path, sizeof(ctx->pubkey_path), "%s", cred->pubkey_path);
    snprintf(ctx->knock, sizeof(ctx->knock), "%s", cred->knock);
    snprintf(ctx->spa_key_path, sizeof(ctx->spa_key_path), "%s", cred->spa_key_path);
    ctx->pbkdf2_iter = iter;
    g_last_error[0] = '\0';
    return VMS_OK;
}

int32_t VMS_CALL vms_logout(vms_context* ctx)
{
    int rc;

    if (!ctx) return VMS_ERR_INVALID_ARG;
    if (!ctx->sess) { ctx->logged_in = 0; return VMS_OK; }

    rc = qc_logout(ctx->sess);
    vms_session_close(ctx);
    ctx->connected = 0;
    return (rc <= 0) ? VMS_OK : rc;
}

int32_t VMS_CALL vms_heartbeat(vms_context* ctx)
{
    int rc;

    if (!ctx || !ctx->sess) return VMS_ERR_NOT_CONNECTED;
    rc = qc_heartbeat(ctx->sess);
    if (rc < 0) return VMS_ERR_NOT_CONNECTED;
    return rc;   /* 0 = OK；>0 = 服务端结果码 */
}

/* ==================== M2：真实业务查询（BIZ_QUERY + cJSON） ==================== */

/* 业务响应缓冲上限：TLV(RESULT) + TLV(BODY) 中 BODY 为下游 JSON，8KB 对 stats 足够 */
#define VMS_BIZ_RESP_MAX  8192u

/* 在明文响应 TLV 里找指定 tag（返回 val 指针与长度；未找到返回 NULL） */
static const uint8_t* vms_tlv_find(const uint8_t* buf, size_t len, uint16_t tag, uint16_t* out_len)
{
    size_t off = 0u;

    while (off < len)
    {
        uint16_t t = 0u, vlen = 0u;
        const uint8_t* val = NULL;
        size_t used = 0u;

        if (q_codec_tlv_decode(buf + off, len - off, &t, &val, &vlen, &used) != 0)
            break;
        off += used;
        if (t == tag)
        {
            if (out_len) *out_len = vlen;
            return val;
        }
    }
    return NULL;
}

/* 用下游 JSON 的 data 段刷新 ctx->stats；成功 0，失败 -1（错误信息已 set_error） */
static int vms_stats_from_json(struct vms_context* ctx, const char* json)
{
    cJSON* root = cJSON_Parse(json);
    cJSON* data;
    cJSON* it;

    if (!root) { set_error("业务响应 JSON 解析失败", VMS_ERR_INTERNAL); return -1; }

    /* 下游 result：bizd 通用转发只回"转发成功"，真实业务结果在下游 JSON 里 */
    it = cJSON_GetObjectItem(root, "result");
    if (it && cJSON_IsNumber(it) && (int)it->valuedouble != 0)
    {
        int r = (int)it->valuedouble;
        const char* msg = "";
        cJSON* m = cJSON_GetObjectItem(root, "message");
        if (m && cJSON_IsString(m) && m->valuestring) msg = m->valuestring;
        snprintf(g_last_error, sizeof(g_last_error), "业务查询被拒绝：结果码 %d %s", r, msg);
        cJSON_Delete(root);
        return r > 0 ? r : -1;
    }

    data = cJSON_GetObjectItem(root, "data");
    if (!data || !cJSON_IsObject(data))
    {
        cJSON_Delete(root);
        set_error("业务响应缺少 data 段", VMS_ERR_INTERNAL);
        return -1;
    }

#define VMS_PICK_NUM(field, dst)                                        \
    do {                                                                \
        cJSON* f = cJSON_GetObjectItem(data, (field));                  \
        if (f && cJSON_IsNumber(f)) (dst) = (uint32_t)f->valuedouble;    \
    } while (0)

    VMS_PICK_NUM("total",           ctx->stats.total);
    VMS_PICK_NUM("online",          ctx->stats.online);
    VMS_PICK_NUM("offline",         ctx->stats.offline);
    VMS_PICK_NUM("excluded",        ctx->stats.excluded);
    VMS_PICK_NUM("alarm_unhandled", ctx->stats.alarm_unhandled);
#undef VMS_PICK_NUM

    it = cJSON_GetObjectItem(data, "online_rate");
    if (it && cJSON_IsNumber(it))
        ctx->stats.online_rate = (float)it->valuedouble;
    else if (ctx->stats.total > 0u)
        ctx->stats.online_rate = (float)ctx->stats.online * 100.0f / (float)ctx->stats.total;

    cJSON_Delete(root);
    return 0;
}

/* 拉一次真实统计；成功 0；>0 服务端结果码；<0 本地错误 */
static int32_t vms_fetch_stats(struct vms_context* ctx)
{
    uint8_t  resp[VMS_BIZ_RESP_MAX];
    size_t   rlen = 0u;
    uint16_t blen = 0u;
    const uint8_t* body;
    char     json[VMS_BIZ_RESP_MAX];
    int      rc;

    rc = qc_biz_query(ctx->sess, "stats", NULL, 0u, 0u, resp, sizeof(resp), &rlen);
    if (rc < 0)
    {
        set_error("业务查询失败：连接中断", VMS_ERR_NOT_CONNECTED);
        return VMS_ERR_NOT_CONNECTED;
    }
    if (rc > 0)
    {
        snprintf(g_last_error, sizeof(g_last_error), "业务查询被拒绝：结果码 %d", rc);
        return (int32_t)rc;
    }

    body = vms_tlv_find(resp, rlen, Q_IPC_T_BODY, &blen);
    if (!body || blen == 0u)
    {
        set_error("业务响应缺少 BODY", VMS_ERR_INTERNAL);
        return VMS_ERR_INTERNAL;
    }
    if ((size_t)blen >= sizeof(json))
    {
        set_error("业务响应过长", VMS_ERR_BUFFER_SMALL);
        return VMS_ERR_BUFFER_SMALL;
    }
    memcpy(json, body, blen);
    json[blen] = '\0';

    rc = vms_stats_from_json(ctx, json);
    if (rc < 0)
        return VMS_ERR_INTERNAL;
    return (int32_t)rc;   /* 0 或下游结果码 */
}

/* ==================== M3：业务列表（companies / vehicles + cJSON） ====================
 *
 * 与 M2 的 stats 同一套约定：
 *   - 已登录：走 qc_biz_query -> 下游 JSON（data 域字段 = 库表列名的 camelCase）；
 *   - 失败**不**回退 Mock（宁可让 UI 报错，也不能给运维看假数字）；
 *   - 未登录：仍走内置 Mock，保证无后端时 UI 可跑。
 *
 * 字段映射见《biz微服务接口约定.md》，这里只做"列名 -> 结构体字段"的搬运，
 * 不做任何业务聚合（聚合是下游微服务的事）。
 */

/* 单帧 BIZ 响应上限：TLV val 上限 60000（Q_IPC_TLV_MAX）+ RESULT TLV 余量 */
#define VMS_BIZ_BUF_MAX   (60000u + 256u)

/* 一次业务响应 JSON 的打开结果：root 持有所有权，data/items 是借用指针 */
typedef struct
{
    cJSON *root;
    cJSON *data;
    cJSON *items;
} vms_json_t;

static void vms_json_close(vms_json_t *j)
{
    if (!j) return;
    if (j->root) cJSON_Delete(j->root);
    j->root = NULL; j->data = NULL; j->items = NULL;
}

/* 打开下游响应：0=成功；>0=下游结果码；<0=本地解析错误（错误串已 set_error） */
static int vms_json_open(const char *json, vms_json_t *j)
{
    cJSON *root, *r, *data;

    memset(j, 0, sizeof(*j));
    root = cJSON_Parse(json);
    if (!root) { set_error("业务响应 JSON 解析失败", VMS_ERR_INTERNAL); return -1; }
    j->root = root;

    /* 下游 result：bizd 通用转发只回"转发成功"，真实业务结果在下游 JSON 里 */
    r = cJSON_GetObjectItem(root, "result");
    if (r && cJSON_IsNumber(r) && (int)r->valuedouble != 0)
    {
        int rc = (int)r->valuedouble;
        const char *msg = "";
        cJSON *m = cJSON_GetObjectItem(root, "message");
        if (m && cJSON_IsString(m) && m->valuestring) msg = m->valuestring;
        snprintf(g_last_error, sizeof(g_last_error), "业务查询被拒绝：结果码 %d %s", rc, msg);
        return rc > 0 ? rc : -1;
    }

    data = cJSON_GetObjectItem(root, "data");
    if (!data || !cJSON_IsObject(data))
    {
        set_error("业务响应缺少 data 段", VMS_ERR_INTERNAL);
        return -1;
    }
    j->data  = data;
    j->items = cJSON_GetObjectItem(data, "items");
    return 0;
}

static const char *js_str(const cJSON *o, const char *k)
{
    cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL;
    return (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : NULL;
}

static double js_num(const cJSON *o, const char *k, double def)
{
    cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL;
    return (v && cJSON_IsNumber(v)) ? v->valuedouble : def;
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!src) { dst[0] = '\0'; return; }
    snprintf(dst, cap, "%s", src);
}

/* 字符串主键 -> uint32 UI 序号：纯数字串直接转；非数字（UUID / "车牌+颜色"）用 fallback 行号。
   注：真实系统若主键不是数字，UI 勾选应改用字符串主键（vms_vehicle.vehicle_id）。 */
static uint32_t key_id(const char *key, uint32_t fallback)
{
    const char *p;
    uint32_t    v = 0u;

    if (!key || !key[0]) return fallback;
    for (p = key; *p; p++)
    {
        if (*p < '0' || *p > '9') return fallback;
        v = v * 10u + (uint32_t)(*p - '0');
    }
    return v;
}

/* company_id(uint32 序号) -> 库表字符串 org_id（用于向后台发过滤条件） */
static const char *org_id_of(const struct vms_context *ctx, uint32_t company_id)
{
    size_t i;
    for (i = 0; i < ctx->company_count; ++i)
        if (ctx->companies[i].id == company_id)
            return ctx->companies[i].org_id;
    return NULL;
}

/* 发一次 BIZ_QUERY 并取回下游 JSON 原文（malloc，调用方 free）；
   失败返回 NULL，*out_rc 为本地错误码（<0）或服务端结果码（>0） */
static char *vms_biz_call(struct vms_context *ctx, const char *biz_type,
                          const char *param_json, uint32_t offset, uint32_t limit,
                          int32_t *out_rc)
{
    uint8_t      *resp;
    size_t        rlen = 0u;
    uint16_t      blen = 0u;
    const uint8_t *body;
    char         *json;
    int           rc;

    *out_rc = VMS_OK;
    resp = (uint8_t *)malloc(VMS_BIZ_BUF_MAX);
    if (!resp) { set_error("内存不足", VMS_ERR_NO_MEMORY); *out_rc = VMS_ERR_NO_MEMORY; return NULL; }

    rc = qc_biz_query(ctx->sess, biz_type, param_json, offset, limit,
                      resp, VMS_BIZ_BUF_MAX, &rlen);
    if (rc < 0)
    {
        free(resp);
        set_error("业务查询失败：连接中断", VMS_ERR_NOT_CONNECTED);
        *out_rc = VMS_ERR_NOT_CONNECTED;
        return NULL;
    }
    if (rc > 0)
    {
        free(resp);
        snprintf(g_last_error, sizeof(g_last_error), "业务查询被拒绝：结果码 %d", rc);
        *out_rc = (int32_t)rc;
        return NULL;
    }

    body = vms_tlv_find(resp, rlen, Q_IPC_T_BODY, &blen);
    if (!body || blen == 0u)
    {
        free(resp);
        set_error("业务响应缺少 BODY", VMS_ERR_INTERNAL);
        *out_rc = VMS_ERR_INTERNAL;
        return NULL;
    }
    json = (char *)malloc((size_t)blen + 1u);
    if (!json) { free(resp); set_error("内存不足", VMS_ERR_NO_MEMORY); *out_rc = VMS_ERR_NO_MEMORY; return NULL; }
    memcpy(json, body, blen);
    json[blen] = '\0';
    free(resp);
    return json;
}

/* 拉全量组织机构（不分页：组织树要一次建好）并刷新 ctx->companies */
static int32_t vms_fetch_companies(struct vms_context *ctx)
{
    char      *json;
    int32_t    rc = VMS_OK;
    int        r;
    vms_json_t j;
    size_t     i, n;

    json = vms_biz_call(ctx, "companies", NULL, 0u, 0u, &rc);
    if (!json) return rc;
    r = vms_json_open(json, &j);
    free(json);
    if (r != 0)
    {
        vms_json_close(&j);
        return (r > 0) ? (int32_t)r : VMS_ERR_INTERNAL;
    }
    if (!j.items || !cJSON_IsArray(j.items))
    {
        vms_json_close(&j);
        set_error("companies 响应缺少 items 数组", VMS_ERR_INTERNAL);
        return VMS_ERR_INTERNAL;
    }

    n = (size_t)cJSON_GetArraySize(j.items);
    if (n > VMS_MAX_COMPANY) n = VMS_MAX_COMPANY;

    memset(ctx->companies, 0, sizeof(ctx->companies));
    for (i = 0; i < n; ++i)
    {
        cJSON       *it    = cJSON_GetArrayItem(j.items, (int)i);
        vms_company *c     = &ctx->companies[i];
        const char  *oid   = js_str(it, "orgId");
        const char  *pid   = js_str(it, "parentOrgId");
        const char  *sname = js_str(it, "orgShortName");
        const char  *lname = js_str(it, "orgLongName");

        if (!it) break;
        copy_str(c->org_id, sizeof(c->org_id), oid);
        c->id = key_id(oid, (uint32_t)(i + 1u));
        /* d_org_info 约定：根节点 parent_org_id = "-1"；统一映射成 0（= 无父级） */
        c->parent_id    = (!pid || !pid[0] || strcmp(pid, "-1") == 0) ? 0u : key_id(pid, 0u);
        copy_str(c->name, sizeof(c->name),
                 (sname && sname[0]) ? sname : ((lname && lname[0]) ? lname : ""));
        c->count       = (uint32_t)js_num(it, "vehicleCount", 0.0);
        c->online      = (uint32_t)js_num(it, "onlineCount", 0.0);
        c->child_count = (uint32_t)js_num(it, "childCount", 0.0);
    }
    ctx->company_count = n;

    /* 服务端没给 childCount 时按 parent 关系补算（树控件依赖它判断叶子） */
    for (i = 0; i < ctx->company_count; ++i)
    {
        size_t k;
        uint32_t child = 0u;
        if (ctx->companies[i].child_count) continue;
        for (k = 0; k < ctx->company_count; ++k)
            if (ctx->companies[k].parent_id == ctx->companies[i].id) ++child;
        ctx->companies[i].child_count = child;
    }

    vms_json_close(&j);
    return VMS_OK;
}

/* 构造 vehicles 的 biz_param JSON（malloc，调用方 free） */
static char *vms_build_veh_param(const struct vms_context *ctx, const vms_query *q)
{
    cJSON *o = cJSON_CreateObject();
    char  *s;

    if (!o) return NULL;

    if (q && q->company_id)
    {
        const char *oid = org_id_of(ctx, q->company_id);
        if (oid && oid[0])
            cJSON_AddStringToObject(o, "org_id", oid);
    }
    cJSON_AddNumberToObject(o, "recursive", 1);   /* 按组织过滤 = 该组织及其所有下级 */
    if (q && q->keyword[0])
        cJSON_AddStringToObject(o, "keyword", q->keyword);
    if (q && q->online_only)
        cJSON_AddNumberToObject(o, "online_only", 1);
    if (q && q->alarm_only)
    {
        cJSON_AddNumberToObject(o, "alarm_only", 1);
        cJSON_AddNumberToObject(o, "alarm_mask",
                                (double)(q->alarm_mask ? q->alarm_mask : 0xFFFFFFFFu));
    }
    if (q && q->id_count)
    {
        cJSON *arr = cJSON_CreateArray();
        uint32_t k;
        char     vid[24];
        if (arr)
        {
            /* ids 是 uint32 UI 序号；服务端按字符串主键比对，桩服务的主键规则是 "%06u"。
               真实系统主键非数字时，应改传 vms_vehicle.vehicle_id（ABI v5 待办）。 */
            for (k = 0; k < q->id_count && k < VMS_QUERY_MAX_IDS; ++k)
            {
                snprintf(vid, sizeof(vid), "%06u", q->ids[k]);
                cJSON_AddItemToArray(arr, cJSON_CreateString(vid));
            }
            cJSON_AddItemToObject(o, "ids", arr);
        }
    }

    s = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    return s;
}

/* 一条车辆 JSON -> vms_vehicle */
static void vms_vehicle_from_json(vms_vehicle *v, const cJSON *it, size_t row)
{
    const char *s;

    memset(v, 0, sizeof(*v));

    s = js_str(it, "vehicleId");
    copy_str(v->vehicle_id, sizeof(v->vehicle_id), s);
    v->id = key_id(s, (uint32_t)(row + 1u));

    s = js_str(it, "orgId");
    copy_str(v->org_id, sizeof(v->org_id), s);

    /* 自编号：user_defined_code 优先（UI 语义），空则退到 erp_code，再空用主键 */
    s = js_str(it, "userDefinedCode");
    if (!s || !s[0]) s = js_str(it, "erpCode");
    copy_str(v->self_code, sizeof(v->self_code), s && s[0] ? s : v->vehicle_id);

    copy_str(v->plate, sizeof(v->plate), js_str(it, "lisPlate"));

    /* 时间：扩展字段 lastTime（定位时间）-> time / day */
    s = js_str(it, "lastTime");
    copy_str(v->time, sizeof(v->time), s);
    if (s && strlen(s) >= 10u)
    {
        memcpy(v->day, s, 10u);
        v->day[10] = '\0';
    }
    copy_str(v->location, sizeof(v->location), js_str(it, "location"));
    copy_str(v->issue,    sizeof(v->issue),    js_str(it, "alarmText"));

    v->online          = (uint32_t)js_num(it, "online", 0.0);
    v->offline_seconds = (uint32_t)js_num(it, "offlineSeconds", 0.0);
    v->alarm_flags     = (uint32_t)js_num(it, "alarmFlags", 0.0);
    v->speed_kph       = (float)js_num(it, "speedKph", 0.0);
    v->lon             = js_num(it, "lon", 0.0);
    v->lat             = js_num(it, "lat", 0.0);
}

/* 拉一页车辆：total 进 *out_count，命中行写 buf（最多 cap 条） */
static int32_t vms_fetch_vehicles(struct vms_context *ctx, const vms_query *q,
                                  vms_vehicle *buf, size_t cap, size_t *out_count)
{
    char      *param, *json;
    int32_t    rc = VMS_OK;
    int        r;
    vms_json_t j;
    uint32_t   offset, limit, total;
    size_t     i, n, written = 0u;

    offset = q ? q->offset : 0u;
    limit  = (q && q->limit) ? q->limit : (uint32_t)(cap ? cap : 50u);
    if (limit == 0u)  limit = 50u;
    /* 单页上限 100：服务端与本机都有同样的约束（一条车辆 JSON 约 480B，
       超过 100 条会撞上 Q_IPC_TLV_MAX=60000，响应被截断成非法 JSON）。 */
    if (limit > 100u) limit = 100u;

    param = vms_build_veh_param(ctx, q);
    if (!param) { set_error("内存不足", VMS_ERR_NO_MEMORY); return VMS_ERR_NO_MEMORY; }
    json = vms_biz_call(ctx, "vehicles", param, offset, limit, &rc);
    free(param);
    if (!json) return rc;

    r = vms_json_open(json, &j);
    free(json);
    if (r != 0)
    {
        vms_json_close(&j);
        return (r > 0) ? (int32_t)r : VMS_ERR_INTERNAL;
    }

    total = (uint32_t)js_num(j.data, "total", 0.0);
    if (j.items && cJSON_IsArray(j.items))
    {
        n = (size_t)cJSON_GetArraySize(j.items);
        for (i = 0; i < n && written < cap; ++i)
        {
            cJSON *it = cJSON_GetArrayItem(j.items, (int)i);
            if (!it) break;
            vms_vehicle_from_json(&buf[written], it, offset + written);
            ++written;
        }
    }
    vms_json_close(&j);

    *out_count = (size_t)total;
    /* 还有剩余（buf 装不下或本页之后还有）-> 沿用既有约定：BUFFER_SMALL */
    if (written < cap && (size_t)total > (size_t)offset + written)
        return VMS_ERR_BUFFER_SMALL;
    if (written >= cap && (size_t)total > (size_t)offset + written)
        return VMS_ERR_BUFFER_SMALL;
    return VMS_OK;
}

int32_t VMS_CALL vms_get_stats(vms_context* ctx, vms_stats* out_stats)
{
    if (!ctx || !out_stats) return VMS_ERR_INVALID_ARG;

    if (ctx->logged_in && ctx->sess)
    {
        int32_t rc = vms_fetch_stats(ctx);
        if (rc != VMS_OK) return rc;   /* 已登录：失败即报错，不回退 Mock */
    }

    *out_stats = ctx->stats;
    return VMS_OK;
}

int32_t VMS_CALL vms_get_companies(const vms_context* ctx, vms_company* buf, size_t cap, size_t* out_count)
{
    if (!ctx || !out_count) return VMS_ERR_INVALID_ARG;

    /* 已登录：向后台拉全量组织树（失败即报错，不回退 Mock） */
    if (ctx->logged_in && ctx->sess)
    {
        int32_t rc = vms_fetch_companies((struct vms_context*)ctx);
        if (rc != VMS_OK) return rc;
    }

    *out_count = ctx->company_count;
    if (!buf || cap < ctx->company_count) return VMS_ERR_BUFFER_SMALL;
    memcpy(buf, ctx->companies, ctx->company_count * sizeof(vms_company));
    return VMS_OK;
}

/* 车辆/组织是否落在 root_id 这棵子树里（root_id==0 表示"全部"）。
 *
 * 语义（也是产品语义）：按组织过滤 = **该组织及其所有下级组织**。
 * 真实系统里车辆挂在最末级单位（配送中心/车队），
 * 若按 id 精确匹配，点"北京分公司"会一台车都查不到 —— 显然不对。
 * 这里沿 parent_id 逐级上溯（深度一般在 3 层内），不用额外建索引。 */
static int company_in_subtree(const struct vms_context* ctx, uint32_t company_id, uint32_t root_id)
{
    int guard = 0;
    if (root_id == 0) return 1;

    while (company_id != 0 && guard++ < (int)VMS_MAX_COMPANY) {
        size_t i;
        if (company_id == root_id) return 1;

        for (i = 0; i < ctx->company_count; ++i) {
            if (ctx->companies[i].id == company_id) {
                company_id = ctx->companies[i].parent_id;
                break;
            }
        }
        if (i == ctx->company_count) break;   /* 组织表里查不到这个 id：按"不属于"处理 */
    }
    return 0;
}

static int vehicle_match(const struct vms_context* ctx, size_t idx, const vms_query* q)
{
    const vms_vehicle* v = &ctx->vehicles[idx];
    if (q->company_id && !company_in_subtree(ctx, ctx->vehicle_company[idx], q->company_id)) return 0;
    if (q->online_only && !v->online) return 0;
    if (q->alarm_only && (v->alarm_flags & q->alarm_mask) == 0) return 0;
    if (q->keyword[0] && !strstr(v->plate, q->keyword) && !strstr(v->self_code, q->keyword)) return 0;
    /* v2：按车辆 ID 精确过滤（树上勾选若干车辆）。id_count==0 不过滤。 */
    if (q->id_count) {
        uint32_t k, hit = 0;
        for (k = 0; k < q->id_count; ++k)
            if (q->ids[k] == v->id) { hit = 1; break; }
        if (!hit) return 0;
    }
    return 1;
}

int32_t VMS_CALL vms_query_vehicles(const vms_context* ctx, const vms_query* q,
                                    vms_vehicle* buf, size_t cap, size_t* out_count)
{
    size_t idx, matched = 0, written = 0;
    if (!ctx || !out_count) return VMS_ERR_INVALID_ARG;
    if (q && q->limit == 0) return VMS_ERR_INVALID_ARG;

    /* 已登录：交给后台做过滤 + 分页（本地只搬数据）；
       组织过滤需要 uint32 序号 -> 库表字符串 org_id 的映射，故先确保组织树已加载。 */
    if (ctx->logged_in && ctx->sess)
    {
        struct vms_context* m = (struct vms_context*)ctx;
        if (m->company_count == 0)
        {
            int32_t rc = vms_fetch_companies(m);
            if (rc != VMS_OK) return rc;
        }
        if (!buf || cap == 0)
        {
            /* 仅探测条数：limit 取 1，只取 total */
            vms_vehicle probe;
            return vms_fetch_vehicles(m, q, &probe, 1u, out_count);
        }
        return vms_fetch_vehicles(m, q, buf, cap, out_count);
    }

    for (idx = 0; idx < ctx->vehicle_count; ++idx) {
        if (q && !vehicle_match(ctx, idx, q)) continue;
        if (matched >= (q ? q->offset : 0) && written < cap)
            buf[written++] = ctx->vehicles[idx];
        ++matched;
    }

    *out_count = matched;
    if (written < cap) return VMS_OK;
    /* cap 用尽：继续统计总数后以 BUFFER_SMALL 告知调用方"还有剩余" */
    for (; idx < ctx->vehicle_count; ++idx) {
        if (!q || vehicle_match(ctx, idx, q)) ++matched;
    }
    *out_count = matched;
    return VMS_ERR_BUFFER_SMALL;
}

int32_t VMS_CALL vms_subscribe(vms_context* ctx, vms_push_cb cb, void* user_data)
{
    if (!ctx || !cb) return VMS_ERR_INVALID_ARG;
    ctx->push_cb = cb;
    ctx->push_user = user_data;
#ifdef _WIN32
    if (!ctx->push_thread) {
        InterlockedExchange(&ctx->push_stop, 0);
        ctx->push_thread = CreateThread(NULL, 0, push_thread_proc, ctx, 0, NULL);
        if (!ctx->push_thread) { ctx->push_cb = NULL; return VMS_ERR_INTERNAL; }
    }
#endif
    return VMS_OK;
}

int32_t VMS_CALL vms_unsubscribe(vms_context* ctx)
{
    if (!ctx) return VMS_ERR_INVALID_ARG;
#ifdef _WIN32
    if (ctx->push_thread) {
        InterlockedExchange(&ctx->push_stop, 1);
        WaitForSingleObject(ctx->push_thread, 3000);
        CloseHandle(ctx->push_thread);
        ctx->push_thread = NULL;
    }
#endif
    ctx->push_cb = NULL;
    ctx->push_user = NULL;
    return VMS_OK;
}

int32_t VMS_CALL vms_set_log_callback(vms_context* ctx, vms_log_cb cb, void* user_data)
{
    if (!ctx) return VMS_ERR_INVALID_ARG;
    ctx->log_cb = cb;
    ctx->log_user = user_data;
    return VMS_OK;
}

const char* VMS_CALL vms_last_error(const vms_context* ctx) { (void)ctx; return g_last_error; }

void VMS_CALL vms_free(void* p) { free(p); }
