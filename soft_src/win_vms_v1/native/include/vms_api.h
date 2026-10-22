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
/* v6：新增会话治理（M4）：vms_keeper_start/stop、vms_get_session_info、
        vms_set_session_callback / vms_set_relogin_callback、vms_lock/unlock、
        vms_derive_pwd_hash。仅新增函数与结构（尾部追加），不改既有布局。
    v5：vms_query.ids 由 uint32[] 改为「库表主键字符串数组」char[][VMS_KEY_MAX]，
       并新增 vms_query.org_id（d_org_info.org_id，子树根）。
   原因：d_vehicleinfo.vehicle_id 是 CHAR(32)，注释原文「车辆ID，车牌号+颜色」，
        即 `京A12345-1` 这种**非数字**业务主键。v4 用 uint32 + "%06u" 反向编码只是桩服务的
        临时约定，接真实库后 `京A12345-1` 根本编码不进 uint32 —— 这是正确性问题，不是便利问题。
        *** v5 是 breaking change（ids 元素类型变了）***：旧 UI 配新 DLL 会静默错位，
        因此必须靠 vms_abi_version() 自检拦住（C# 侧 ExpectedAbiVersion 同步抬到 5）。
        新字段 org_id 仍放在结构体尾部，v4 调用方不碰 ids 时可继续编译。
   v4：vms_company 追加 org_id、vms_vehicle 追加 vehicle_id/org_id（M3 业务列表）。
   v3：新增 vms_credentials + vms_login/vms_logout/vms_heartbeat（M1 登录闭环）。 */
#define VMS_ABI_VERSION  6u
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

/* ======================== M4：会话治理（Session Keeper） ========================
 * 背景：协议严格 lockstep（一问一答），后台 Token 有 TTL(1800s) 且心跳**不续期**，
 *       客户端又有多个业务线程（列表 / 定位 / 视频）。把心跳、断线退避重连、
 *       Token 失效自动重登、请求串行化收敛在通信内核（l_4 q_clientcomm），三端共用。
 *
 * 状态机：
 *   ONLINE        --心跳ok--> ONLINE
 *                 --心跳返回3--> REAUTHING     : 原连接上重发 SALT+LOGIN（不拆连接）
 *                 --心跳失败-->  RECONNECTING  : 退避后重建链路（含重新敲门）
 *   REAUTHING     --重登ok--> ONLINE；失败 --> RECONNECTING
 *   RECONNECTING  --重建ok--> ONLINE；超限 --> OFFLINE
 *   OFFLINE       --vms_keeper_wake/重新登录--> 恢复
 *
 * 口令红线：内核**不保存口令，也不保存派生值**。自动重登需要的 pwd_hash 通过
 *           vms_relogin_cb 向 UI 索取（UI 可缓存口令 / 弹窗重输 / 读系统凭据管理器）。
 *           回调返回非 0 = 无法提供，会话转 OFFLINE，UI 应弹登录框。
 */
typedef enum vms_session_state {
    VMS_SS_IDLE         = 0,  /* 未登录 / keeper 未启动 */
    VMS_SS_ONLINE       = 1,  /* 已认证，心跳正常 */
    VMS_SS_RECONNECTING = 2,  /* 连接不可用，退避重连中 */
    VMS_SS_REAUTHING    = 3,  /* Token 失效，重新登录中 */
    VMS_SS_OFFLINE      = 4   /* 连续失败超上限或缺凭据，需 UI 介入 */
} vms_session_state;

typedef struct vms_session_info {
    int32_t  state;             /* vms_session_state */
    uint32_t attempts;          /* 当前连续失败次数 */
    uint32_t hb_sent;           /* 已发心跳次数 */
    uint32_t hb_failed;         /* 心跳失败次数 */
    uint32_t reconnects;        /* 重建连接成功次数 */
    uint32_t relogins;          /* 自动重新登录成功次数 */
    uint64_t last_ok_ms;        /* 最近一次成功的单调时刻（进程内相对值，非墙上时间） */
    uint64_t next_hb_ms;        /* 下次心跳时刻 */
    uint64_t next_retry_ms;     /* 下次重连时刻（RECONNECTING 有效） */
    char     detail[96];        /* 最近一次事件描述（UTF-8） */
} vms_session_info;

typedef struct vms_keeper_cfg {
    uint32_t hb_interval_sec;   /* 心跳间隔；0 = 默认 60 */
    uint32_t backoff_base_sec;  /* 退避基数；0 = 默认 1（序列 1/2/4/8/16/30 封顶） */
    uint32_t backoff_max_sec;   /* 退避上限；0 = 默认 30 */
    uint32_t max_retry;         /* 连续失败多少次转 OFFLINE；0 = 无限重连 */
    uint32_t tick_ms;           /* 主循环轮询间隔；0 = 默认 500 */
    uint32_t fault;             /* 故障注入（联调用，见 VMS_FAULT_*）；生产恒 0 */
} vms_keeper_cfg;

/* 故障注入位（仅供联调 / 冒烟脚本，用于验证重连与重登路径） */
#define VMS_FAULT_NONE         0x0000u
#define VMS_FAULT_HB_FAIL      0x0001u  /* 心跳视为断连 */
#define VMS_FAULT_HB_RC3       0x0002u  /* 心跳返回结果码 3（Token 过期） */
#define VMS_FAULT_RELOGIN_FAIL 0x0004u  /* 重登必定失败 */

typedef struct vms_context vms_context; /* 不透明句柄（opaque handle） */

#pragma pack(pop)

/* ------------------------ struct 尺寸自检（ABI 守卫） ---------------------- */
typedef enum vms_struct_kind {
    VMS_STRUCT_ABI_INFO = 1,
    VMS_STRUCT_STATS    = 2,
    VMS_STRUCT_COMPANY  = 3,
    VMS_STRUCT_VEHICLE  = 4,
    VMS_STRUCT_QUERY    = 5,
    VMS_STRUCT_CREDENTIALS = 6,
    VMS_STRUCT_SESSION_INFO = 7,     /* v6：M4 会话状态（state / 统计 / 事件描述） */
    VMS_STRUCT_KEEPER_CFG  = 8       /* v6：M4 会话治理参数 */
} vms_struct_kind;

/* ------------------------------- 回调类型 --------------------------------- */
/* 实时推送：items 指向的数组在回调返回后即失效，调用方必须在回调内拷贝。 */
typedef void (VMS_CALL *vms_push_cb)(const vms_vehicle* items, size_t count, void* user_data);
/* 日志回调，level: 0=debug 1=info 2=warn 3=error */
typedef void (VMS_CALL *vms_log_cb)(int32_t level, const char* msg, void* user_data);

/* M4：会话状态变化回调。**由 native 心跳线程调用**，实现方必须自行编组到 UI 线程
   （C#: DispatcherQueue；GTK: g_idle_add；macOS: dispatch_async）——
   在该回调里直接碰 UI 控件必然崩（WinUI3 会抛 RPC_E_WRONG_THREAD）。
   state 见 vms_session_state；rc: 0 正常 / >0 服务端结果码 / <0 断连。 */
typedef void (VMS_CALL *vms_session_cb)(int32_t state, int32_t rc, const char* msg, void* user_data);

/* M4：重新登录时向 UI 索取口令派生值。
   UI 侧实现：拿自己缓存的口令 + 传入的 salt 调 vms_derive_pwd_hash() 填 pwd_hash_out。
   返回 0 = 已填好；非 0 = 无法提供（口令未缓存 / 用户取消）→ 会话转 OFFLINE。
   注意：pwd_hash_out 是 32 字节调用方缓冲，写满即可；内核用完即清零。 */
typedef int  (VMS_CALL *vms_relogin_cb)(const uint8_t* salt, size_t salt_len, uint32_t iter,
                                        uint8_t pwd_hash_out[32], void* user_data);

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
/* 心跳保活；注意：后台心跳**不续期** Token，TTL(1800s) 到期后业务会返回 3。
   M4 起：keeper 已启动时本函数返回最近一次心跳的结果（不发新心跳，避免与 keeper 抢锁）；
   keeper 未启动则同步发一次心跳。返回 0 = 最近心跳正常；>0 服务端结果码；<0 本库错误码。 */
VMS_API int32_t VMS_CALL vms_heartbeat(vms_context* ctx);

/* -------------------------- M4：会话治理 -------------------------- */
/* 启动会话治理线程（心跳 + 退避重连 + Token 失效自动重登）。
   vms_login 成功后会**自动**按默认参数启动（60s 心跳 / 1s 起 30s 封顶退避 / 无限重连）；
   需要改参数时，在 login **之前**调用本函数传入 cfg（cfg 可为 NULL = 全默认）。
   返回 VMS_OK 已启动；VMS_ERR_INVALID_ARG 未登录；VMS_ERR_INTERNAL 线程创建失败
   （此时门面仍可用，只是失去自动治理 —— 降级，不致命）。 */
VMS_API int32_t VMS_CALL vms_keeper_start(vms_context* ctx, const vms_keeper_cfg* cfg);
/* 停止治理线程（幂等）。登出/销毁前会自动调用。 */
VMS_API int32_t VMS_CALL vms_keeper_stop(vms_context* ctx);
/* 唤醒治理线程立即动作（立即心跳 / 立即重连；OFFLINE 时给一轮新的重试机会）。 */
VMS_API int32_t VMS_CALL vms_keeper_wake(vms_context* ctx);
/* 读取会话状态与统计。 */
VMS_API int32_t VMS_CALL vms_get_session_info(vms_context* ctx, vms_session_info* out_info);

/* 注册会话状态回调（状态变化由 native 线程发出，实现方须编组到 UI 线程）。
   建议在 vms_login **之前**注册，以便收到"建立 / 重连 / 掉线"全过程。 */
VMS_API int32_t VMS_CALL vms_set_session_callback(vms_context* ctx,
                                                  vms_session_cb cb, void* user_data);
/* 注册重新登录凭据回调。**必须在 vms_login 之前注册**，否则 Token 过期无法自动重登，
   会话会转 OFFLINE。 */
VMS_API int32_t VMS_CALL vms_set_relogin_callback(vms_context* ctx,
                                                  vms_relogin_cb cb, void* user_data);

/* 串行化 gate：多线程调用业务接口时，用这一对函数包住一次完整的请求-响应，
   避免两路请求交错破坏 lockstep 协议（表现为偶发解包错位 / 卡死）。
   单次业务接口（vms_get_stats 等）内部已经自己加锁，本接口供调用方把
   「多次调用」合并成原子序列时用（例如先查组织树再查车辆）。 */
VMS_API int32_t VMS_CALL vms_lock(vms_context* ctx);
VMS_API int32_t VMS_CALL vms_unlock(vms_context* ctx);

/* PBKDF2-SM3 派生（仅本地运算）：供 vms_relogin_cb 的实现方把口令派生成 pwd_hash。
   口令不出 UI 进程到 native 的长期存储 —— 这也是自动重登必须由 UI 提供 pwd_hash 的原因。
   返回 VMS_OK；失败返回负错误码。 */
VMS_API int32_t VMS_CALL vms_derive_pwd_hash(const char* password,
                                             const uint8_t* salt, size_t salt_len,
                                             uint32_t iter, uint8_t out_hash[32]);

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
