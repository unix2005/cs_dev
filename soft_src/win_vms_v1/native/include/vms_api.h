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
/* v5：vms_query.ids 由 uint32[] 改为「库表主键字符串数组」char[][VMS_KEY_MAX]，
       并新增 vms_query.org_id（d_org_info.org_id，子树根）。
   原因：d_vehicleinfo.vehicle_id 是 CHAR(32)，注释原文「车辆ID，车牌号+颜色」，
        即 `京A12345-1` 这种**非数字**业务主键。v4 用 uint32 + "%06u" 反向编码只是桩服务的
        临时约定，接真实库后 `京A12345-1` 根本编码不进 uint32 —— 这是正确性问题，不是便利问题。
        *** v5 是 breaking change（ids 元素类型变了）***：旧 UI 配新 DLL 会静默错位，
        因此必须靠 vms_abi_version() 自检拦住（C# 侧 ExpectedAbiVersion 同步抬到 5）。
        新字段 org_id 仍放在结构体尾部，v4 调用方不碰 ids 时可继续编译。
   v4：vms_company 追加 org_id、vms_vehicle 追加 vehicle_id/org_id（M3 业务列表）。
   v3：新增 vms_credentials + vms_login/vms_logout/vms_heartbeat（M1 登录闭环）。 */
#define VMS_ABI_VERSION  5u
#define VMS_VER_MAJOR    1
#define VMS_VER_MINOR    1
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
/* 库表字符串主键容量：d_org_info.org_id CHAR(36) / d_vehicleinfo.vehicle_id CHAR(32) */
#define VMS_KEY_MAX       40

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
    /* v4：库表主键 d_org_info.org_id（字符串，可能是 UUID）。
       UI 勾选/过滤仍用上面的 uint32 id；按主键向后台检索时用本字段。 */
    char     org_id[VMS_KEY_MAX];
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
    /* v4：库表主键 d_vehicleinfo.vehicle_id 与归属组织 d_org_info.org_id。
       后续按主键拉定位/视频时用它，不要用 uint32 id（那只是 UI 行号）。 */
    char     vehicle_id[VMS_KEY_MAX];
    char     org_id[VMS_KEY_MAX];
} vms_vehicle;

#define VMS_ALARM_NONE      0x0000u
#define VMS_ALARM_OVERSPEED 0x0001u  /* 超速 */
#define VMS_ALARM_FATIGUE   0x0002u  /* 疲劳驾驶 */
#define VMS_ALARM_OFFLINE   0x0004u  /* 掉线 */
#define VMS_ALARM_GEOFENCE  0x0008u  /* 越界 */
#define VMS_ALARM_GNSS_LOST 0x0010u  /* GNSS 失锁 */

/* 单次查询最多按多少个车辆 ID 过滤（覆盖"在树上勾选一整页"的场景）。
   v5：单页上限是 100（见 BIZ_PAGE_LIMIT_MAX），勾选不会超过一页，128 留足余量；
       元素从 4B 涨到 40B，256 个会让 vms_query 从 1120B 涨到 10KB，故下调。 */
#define VMS_QUERY_MAX_IDS  128

/* 查询条件：分页拉取，配合 ListView 增量加载，支持 3 万+ 车辆不卡 UI。
   注：company_id 是 uint32 UI 序号，native 内部会把它翻译成库表字符串 org_id（见
   vms_company.org_id）。v5 起可直接填 org_id 跳过这次翻译（推荐）。 */
typedef struct vms_query {
    uint32_t company_id;   /* 0 = 全部；非 0 = 该组织**及其所有下级组织**（子树过滤） */
    uint32_t offset;
    uint32_t limit;
    uint32_t online_only;  /* 0/1 */
    uint32_t alarm_only;   /* 0/1 */
    uint32_t alarm_mask;   /* 与 alarm_flags 做 & 判断 */
    uint32_t reserved;
    char     keyword[VMS_TEXT_MAX]; /* 车牌/自编号模糊匹配，UTF-8，空串=不过滤 */
    /* —— 按车辆**主键**精确过滤（树中勾选若干车辆 → 定位数据栏只显示这些）——
       id_count==0 表示不过滤；否则只返回 ids[0..id_count) 里的车辆。

       v5 起元素类型是「库表主键字符串」d_vehicleinfo.vehicle_id（CHAR(32)，车牌+颜色），
       不再是 uint32 行号 —— 直接把 vms_vehicle.vehicle_id 拷进来即可，native 不做任何编码转换。
       定长数组 + 计数，保持结构体 blittable，避免跨 ABI 传指针数组。 */
    char     ids[VMS_QUERY_MAX_IDS][VMS_KEY_MAX];
    uint32_t id_count;
    /* v5 新增：组织子树根，直接用库表主键 d_org_info.org_id（CHAR(36)）。
       非空时**优先于** company_id：省掉"uint32 序号 -> 字符串"的翻译，
       且不再依赖本机组织树缓存（未加载组织树时也能按组织查）。
       为空则回退 company_id（v4 行为，native 内部查组织树翻译）。 */
    char     org_id[VMS_KEY_MAX];
} vms_query;

/* ============================ M1：登录凭据 ============================ */
/* 三端共用同一套语义：host/port = netd；pubkey_path = 服务端静态 SM2 公钥 DER（抗中间人）；
   knock/spa_key_path 仅在部署了 knockd 时需要（UDP 单包授权，端口隐身）。
   口令仅用于本地派生 PBKDF2-SM3，**不落盘、不外发**（外发的只是 32B 派生值）。 */
#define VMS_HOST_MAX       64
#define VMS_USER_MAX       64
#define VMS_PASS_MAX       64
#define VMS_OTP_MAX        16
#define VMS_DEVID_HEX_MAX  65
#define VMS_PATH_MAX       260

typedef struct vms_credentials {
    char     host[VMS_HOST_MAX];         /* netd 主机：IPv4 点分十进制或域名 */
    uint16_t port;                       /* netd 端口（默认 18099） */
    uint16_t reserved16;                 /* 对齐填充，置 0 */
    char     user[VMS_USER_MAX];         /* 用户名（UTF-8） */
    char     password[VMS_PASS_MAX];     /* 口令（UTF-8，仅内存） */
    char     otp[VMS_OTP_MAX];           /* 动态口令（服务端未启用校验时可为空） */
    char     devid_hex[VMS_DEVID_HEX_MAX]; /* 设备指纹 hex（偶数长度；空则用内置默认） */
    char     pubkey_path[VMS_PATH_MAX];  /* 服务端 SM2 公钥 DER 路径（**必填**） */
    char     knock[VMS_HOST_MAX];        /* knockd 的 "ip:port"；空 = 不敲门 */
    char     spa_key_path[VMS_PATH_MAX]; /* SPA 密钥文件（16B）；随 knock 使用 */
    uint32_t pbkdf2_iter;                /* 0 = 用协议默认 Q_PBKDF2_ITER_DEFAULT(100000) */
    uint32_t reserved;
} vms_credentials;

typedef struct vms_context vms_context; /* 不透明句柄（opaque handle） */

#pragma pack(pop)

/* ------------------------ struct 尺寸自检（ABI 守卫） ---------------------- */
typedef enum vms_struct_kind {
    VMS_STRUCT_ABI_INFO = 1,
    VMS_STRUCT_STATS    = 2,
    VMS_STRUCT_COMPANY  = 3,
    VMS_STRUCT_VEHICLE  = 4,
    VMS_STRUCT_QUERY    = 5,
    VMS_STRUCT_CREDENTIALS = 6
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

/* -------------------------- M1：真实登录闭环 -------------------------- */
/* 与后台建立会话：SPA 敲门（可选）-> TCP + SM2 握手 -> HELLO -> SALT_QUERY
   -> 本地 PBKDF2-SM3 派生 -> LOGIN（取 32B Token）。
   返回 VMS_OK 表示已认证；>0 为服务端结果码（见 include_ipc_proto.h Q_IPC_R_*，
   3 = Token/凭据问题，需重新登录）；<0 为本库错误码（vms_last_error 取详情）。
   失败时 ctx 保持可用，可修正凭据后重试；已登录时再次调用会先登出再登录。 */
VMS_API int32_t VMS_CALL vms_login(vms_context* ctx, const vms_credentials* cred);
/* 登出（保持 TCP 连接可复用）；返回 VMS_OK 或负错误码 */
VMS_API int32_t VMS_CALL vms_logout(vms_context* ctx);
/* 心跳保活；注意：后台心跳**不续期** Token，TTL(1800s) 到期后业务会返回 3 */
VMS_API int32_t VMS_CALL vms_heartbeat(vms_context* ctx);

/* --------------------------------- 数据 ---------------------------------- */
/* 全量统计。
 * 语义（M2 起）：**已登录**时向后台发一次 BIZ_QUERY(biz_type=stats) 取真实数据并更新缓存；
 *               **未登录**时返回内置 Mock（让 UI 在无后端时仍可跑）。
 * 已登录但查询失败时**不**回退 Mock —— 宁可让 UI 报错，也不能给运维看假数字；
 * 返回 >0 = 服务端结果码（见 Q_IPC_R_*），<0 = 本地错误。
 * 注意：本函数会阻塞做一次网络往返，调用方须放后台线程。 */
VMS_API int32_t VMS_CALL vms_get_stats(vms_context* ctx, vms_stats* out_stats);

/* 组织机构树（d_org_info）。
 * 语义（M3 起）：**已登录**时向后台发一次 BIZ_QUERY(biz_type=companies) 拉全量并刷新缓存；
 *               **未登录**时返回内置 Mock（让 UI 在无后端时仍可跑）。
 *               已登录但查询失败时**不**回退 Mock（与 vms_get_stats 同一取舍）。
 * 注意：本函数会阻塞做一次网络往返，调用方须放后台线程。 */
VMS_API int32_t VMS_CALL vms_get_companies(const vms_context* ctx,
                                           vms_company* buf, size_t cap, size_t* out_count);

/* 车辆分页查询（d_vehicleinfo + 定位/报警派生字段）。
 * 语义（M3 起）：**已登录**时把 (company_id/keyword/online_only/alarm_only/alarm_mask/ids)
 *               翻译成 biz_param JSON 发给后台，由后台完成过滤 + 分页；
 *               *out_count 回填的是**过滤后的总行数**（不是本页条数）。
 *               **未登录**时在本地 Mock 全量里过滤。
 * 返回 VMS_OK 表示 buf 已装下全部命中行；VMS_ERR_BUFFER_SMALL 表示后面还有。
 * 注意：本函数会阻塞做一次网络往返，调用方须放后台线程。 */
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
