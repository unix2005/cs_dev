/* =============================================================================
 * vms_api.h — 车辆管理系统 native 核心库对外 ABI（Application Binary Interface）
 * -----------------------------------------------------------------------------
 * 设计原则（跨模块边界，任何一条被破坏都会导致后续无法排查的崩溃）：
 *   1. 只暴露 C ABI，不用 C++ 类型/异常/STL —— 保证任何语言、任何编译器都能调。
 *   2. 调用约定显式写成 __cdecl（VMS_CALL）。x64 只有一种约定，但显式写清，
 *      将来若出 32 位版本不会踩坑。
 *   3. 结构体只用定宽类型（uint32_t/float/double/char[]），不用 int/long/bool，
 *      不用位域、不用隐式填充依赖。C# 侧 struct 用同样的 Pack 与字段顺序。
 *   4. 字符串一律 UTF-8，且 **只在调用方缓冲区里产出** —— 绝不返回需要
 *      free() 的堆指针（谁分配谁释放是跨 DLL 堆最经典的踩雷点）。
 *   5. 所有函数返回 int32_t 状态码；细节用 vms_last_error() 取。
 *   6. 「两次调用」范式拿变长数据：先传 cap=0 探测所需条数，再按需分配。
 *   7. ABI 自检：vms_abi_version() + vms_struct_size() 让 C# 侧在启动时校验
 *      struct 布局与版本是否一致，不一致直接Fail Fast，不带着错误布局跑。
 *
 * 版本策略：新增函数只加不删；结构体只允许在尾部追加字段 + 抬升 abi_version。
 * ===========================================================================*/
#ifndef VMS_API_H
#define VMS_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------- 导出/调用约定 ------------------------------ */
#if defined(_WIN32)
#  if defined(VMS_BUILD_DLL)
#    define VMS_API  __declspec(dllexport)
#  else
#    define VMS_API  __declspec(dllimport)
#  endif
#  define VMS_CALL   __cdecl
#else
#  define VMS_API    __attribute__((visibility("default")))
#  define VMS_CALL
#endif

/* --------------------------------- 版本 ----------------------------------- */
#define VMS_ABI_VERSION  2u
#define VMS_VER_MAJOR    1
#define VMS_VER_MINOR    0
#define VMS_VER_PATCH    0

/* -------------------------------- 错误码 ---------------------------------- */
typedef enum vms_status {
    VMS_OK                =  0,
    VMS_ERR_INVALID_ARG   = -1,   /* 入参非法（空指针 / 越界） */
    VMS_ERR_NO_MEMORY     = -2,
    VMS_ERR_NOT_FOUND     = -3,
    VMS_ERR_BUFFER_SMALL  = -4,   /* 缓冲区不足；*out_count 回填所需条数 */
    VMS_ERR_NOT_CONNECTED = -5,
    VMS_ERR_TIMEOUT       = -6,
    VMS_ERR_ABI_MISMATCH  = -7,   /* 头文件与 DLL 不同版本 */
    VMS_ERR_INTERNAL      = -8
} vms_status;

/* ----------------------------- 字段长度上限 -------------------------------- */
#define VMS_CODE_MAX      24   /* 自编号 */
#define VMS_PLATE_MAX     16   /* 车牌号（UTF-8，含 '\0'） */
#define VMS_TEXT_MAX      64   /* 通用短文本：公司名 / 位置 */
#define VMS_TIME_MAX      24   /* "2026-10-16 10:03:49" */
#define VMS_ISSUE_MAX     40   /* 报警类型名 */

/* -------------------------------- 结构体 ---------------------------------- */
#pragma pack(push, 8)

/* 启动自检用：abi_version 必须等于 VMS_ABI_VERSION，struct_size 必须等于 sizeof。 */
typedef struct vms_abi_info {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t ver_major;
    uint32_t ver_minor;
    uint32_t ver_patch;
    uint32_t reserved;
} vms_abi_info;

/* 底部状态栏统计 */
typedef struct vms_stats {
    uint32_t total;         /* 车辆总数 */
    uint32_t online;        /* 在线数 */
    uint32_t offline;       /* 离线数 */
    uint32_t excluded;      /* 不纳入统计数 */
    uint32_t alarm_unhandled; /* 未处理报警数 */
    float    online_rate;   /* 上线率 % */
    uint32_t reserved;
} vms_stats;

/* 组织机构（运输公司）—— 用「扁平数组 + parent_id」表达树，
 * 避免跨越 ABI 传指针链表（内存归属会变得无法解释）。 */
typedef struct vms_company {
    uint32_t id;
    uint32_t parent_id;     /* 0 = 根节点 */
    uint32_t online;        /* 括号里的分子：(8960/18714) */
    uint32_t count;         /* 括号里的分母 */
    uint32_t child_count;
    uint32_t reserved;
    char     name[VMS_TEXT_MAX];
} vms_company;

/* 车辆一行（表格 + 地图打点共用）。
 * 注意：坐标是 GCJ-02（火星坐标系），与腾讯/高德地图一致；
 * 若后端是 WGS-84 原始 GPS，必须在 native 层完成换算，不要把换算泄漏到 UI。 */
typedef struct vms_vehicle {
    uint32_t id;
    uint32_t online;          /* 0 离线 / 1 在线 */
    uint32_t offline_seconds; /* 离线时长（秒） */
    uint32_t alarm_flags;     /* 位掩码，见 VMS_ALARM_* */
    float    speed_kph;
    double   lon;             /* GCJ-02 经度 */
    double   lat;             /* GCJ-02 纬度 */
    char     self_code[VMS_CODE_MAX];
    char     plate[VMS_PLATE_MAX];
    char     time[VMS_TIME_MAX];
    char     location[VMS_TEXT_MAX];
    char     day[VMS_TIME_MAX];
    char     issue[VMS_ISSUE_MAX]; /* 报警类型（实时报警页用） */
} vms_vehicle;

#define VMS_ALARM_NONE      0x0000u
#define VMS_ALARM_OVERSPEED 0x0001u  /* 超速 */
#define VMS_ALARM_FATIGUE   0x0002u  /* 疲劳驾驶 */
#define VMS_ALARM_OFFLINE   0x0004u  /* 掉线 */
#define VMS_ALARM_GEOFENCE  0x0008u  /* 越界 */
#define VMS_ALARM_GNSS_LOST 0x0010u  /* GNSS 失锁 */

/* 单次查询最多按多少个车辆 ID 过滤（覆盖"在树上勾选一整页"的场景）。 */
#define VMS_QUERY_MAX_IDS  256

/* 查询条件：分页拉取，配合 ListView 增量加载，支持 3 万+ 车辆不卡 UI */
typedef struct vms_query {
    uint32_t company_id;   /* 0 = 全部；非 0 = 该组织**及其所有下级组织**（子树过滤） */
    uint32_t offset;
    uint32_t limit;
    uint32_t online_only;  /* 0/1 */
    uint32_t alarm_only;   /* 0/1 */
    uint32_t alarm_mask;   /* 与 alarm_flags 做 & 判断 */
    uint32_t reserved;
    char     keyword[VMS_TEXT_MAX]; /* 车牌/自编号模糊匹配，UTF-8，空串=不过滤 */
    /* —— v2 新增：按车辆 ID 精确过滤（树中勾选若干车辆 → 定位数据栏只显示这些）——
       id_count==0 表示不过滤；否则只返回 ids[0..id_count) 里的车辆。
       定长数组 + 计数，保持结构体 blittable，避免跨 ABI 传指针数组。 */
    uint32_t ids[VMS_QUERY_MAX_IDS];
    uint32_t id_count;
} vms_query;

typedef struct vms_context vms_context; /* 不透明句柄（opaque handle） */

#pragma pack(pop)

/* ------------------------ struct 尺寸自检（ABI 守卫） ---------------------- */
typedef enum vms_struct_kind {
    VMS_STRUCT_ABI_INFO = 1,
    VMS_STRUCT_STATS    = 2,
    VMS_STRUCT_COMPANY  = 3,
    VMS_STRUCT_VEHICLE  = 4,
    VMS_STRUCT_QUERY    = 5
} vms_struct_kind;

/* ------------------------------- 回调类型 --------------------------------- */
/* 实时推送：items 指向的数组在回调返回后即失效，调用方必须在回调内拷贝。 */
typedef void (VMS_CALL *vms_push_cb)(const vms_vehicle* items, size_t count, void* user_data);
/* 日志回调，level: 0=debug 1=info 2=warn 3=error */
typedef void (VMS_CALL *vms_log_cb)(int32_t level, const char* msg, void* user_data);

/* -------------------------------- 生命周期 -------------------------------- */
VMS_API uint32_t    VMS_CALL vms_abi_version(void);
VMS_API const char* VMS_CALL vms_version_string(void);
VMS_API size_t      VMS_CALL vms_struct_size(int32_t kind);

/* config_json: UTF-8 配置（可为 NULL 或空串）；失败时返回负错误码 */
VMS_API int32_t VMS_CALL vms_create(const char* config_json, vms_context** out_ctx);
VMS_API void    VMS_CALL vms_destroy(vms_context* ctx);

/* -------------------------------- 会话管理 -------------------------------- */
VMS_API int32_t VMS_CALL vms_connect(vms_context* ctx, const char* endpoint);
VMS_API int32_t VMS_CALL vms_disconnect(vms_context* ctx);
VMS_API int32_t VMS_CALL vms_is_connected(const vms_context* ctx);

/* --------------------------------- 数据 ---------------------------------- */
VMS_API int32_t VMS_CALL vms_get_stats(const vms_context* ctx, vms_stats* out_stats);

/* 两次调用范式：cap=0 时只回填 *out_count = 总条数，返回 VMS_ERR_BUFFER_SMALL。
 * 缓冲区够则返回 VMS_OK，*out_count = 实际写入条数。 */
VMS_API int32_t VMS_CALL vms_get_companies(const vms_context* ctx,
                                           vms_company* buf, size_t cap, size_t* out_count);
VMS_API int32_t VMS_CALL vms_query_vehicles(const vms_context* ctx, const vms_query* q,
                                            vms_vehicle* buf, size_t cap, size_t* out_count);

/* -------------------------------- 实时推送 -------------------------------- */
VMS_API int32_t VMS_CALL vms_subscribe(vms_context* ctx, vms_push_cb cb, void* user_data);
VMS_API int32_t VMS_CALL vms_unsubscribe(vms_context* ctx);
VMS_API int32_t VMS_CALL vms_set_log_callback(vms_context* ctx, vms_log_cb cb, void* user_data);

/* -------------------------------- 错误详情 -------------------------------- */
/* 返回线程本地 last error 文本；生命周期与线程绑定，调用方不要 free。 */
VMS_API const char* VMS_CALL vms_last_error(const vms_context* ctx);
/* 仅用于释放本库自己的分配（当前 ABI 未用到，保留给将来返回堆指针的接口） */
VMS_API void VMS_CALL vms_free(void* p);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* VMS_API_H */
