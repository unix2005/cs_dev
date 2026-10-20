/* =============================================================================
 * vms_core.c — vms_api.h 的参考实现（Mock / 联调替身）
 * -----------------------------------------------------------------------------
 * 作用：
 *   1. 让 C# + WinUI3 客户端在**没有真实后端**的情况下也能端到端跑起来；
 *   2. 把 ABI 契约用代码固化下来 —— 你要接自己的库时，照这个 .h 实现即可，
 *      C# 侧一行不用改。
 *
 * 编译（llvm-mingw / MinGW-w64，x64）：
 *   gcc -shared -O2 -DVMS_BUILD_DLL -Iinclude -o build/vms_core.dll src/vms_core.c \
 *       -Wl,--out-implib,build/libvms_core.a -static-libgcc
 * MSVC：
 *   cl /LD /O2 /DVMS_BUILD_DLL /Iinclude src\vms_core.c /Fe:build\vms_core.dll
 *
 * 数据规模刻意与真实系统对齐：34554 台车、18714 台归属运输公司，用于压 UI。
 * ===========================================================================*/
#define VMS_BUILD_DLL 1
#include "vms_api.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#  include <windows.h>
#endif

/* --------------------------------- 常量 ----------------------------------- */
#define VMS_MOCK_TOTAL_VEHICLES  34554u
#define VMS_MOCK_TOTAL_ONLINE    10846u
#define VMS_MOCK_TOTAL_OFFLINE   23708u
#define VMS_MOCK_ALARM_UNHANDLED 46343u
#define VMS_MAX_COMPANY          64u
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

#ifdef _WIN32
    CRITICAL_SECTION lock;
    HANDLE           push_thread;
    volatile LONG    push_stop;
#endif
};

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
    ctx->connected = 0;
    return VMS_OK;
}

int32_t VMS_CALL vms_is_connected(const vms_context* ctx)
{
    return (ctx && ctx->connected) ? 1 : 0;
}

int32_t VMS_CALL vms_get_stats(const vms_context* ctx, vms_stats* out_stats)
{
    if (!ctx || !out_stats) return VMS_ERR_INVALID_ARG;
    *out_stats = ctx->stats;
    return VMS_OK;
}

int32_t VMS_CALL vms_get_companies(const vms_context* ctx, vms_company* buf, size_t cap, size_t* out_count)
{
    if (!ctx || !out_count) return VMS_ERR_INVALID_ARG;
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
