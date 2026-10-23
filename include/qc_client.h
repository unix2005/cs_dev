/**
 * @file qc_client.h
 * @brief 跨三端共享的纯 C 客户端通信内核（libqclientcomm）对外唯一公共头
 * @platform Windows / Linux / macOS
 * @layer  lib_src/l_4/q_clientcomm  ->  产出 libqclientcomm(.a/.so/.dll)
 * @note   本头文件是「稳定 C ABI 门面」：所有函数 extern "C"，句柄为不透明指针，
 *         便于 WinUI3(C# P/Invoke) / GTK4(C) / macOS(Swift 桥接) 三端干净绑定。
 *         UI 层**绝不**直接碰 q_fd_t / q_chan_t。
 *
 *         内部封装：q_chan(SM2 ECDH + SM4-GCM 安全通道) / q_net(阻塞收发) /
 *         q_codec(TLV) / q_crypto(PBKDF2-SM3) / q_mem(敏感清零)。
 *         协议语义见《客户端通信接口与开发指南.md》。
 */
#ifndef QC_CLIENT_H
#define QC_CLIENT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 不透明会话句柄（内部封装 q_chan_t + q_fd_t + 缓存 Token） */
typedef struct qc_session qc_session_t;

/**
 * 创建会话。
 *   server_pub_der/len : 服务端静态公钥 DER（必须内置，用于验签服务端身份，抗中间人）
 *   seq0               : 必须与 netd 一致（=1；仅用于会话密钥派生，不影响消息 seq 计数器）
 *   key_id             : 必须与 netd 配置一致（默认 1）
 * 返回 NULL 表示参数非法或内存不足（应视为致命，不可继续）。
 */
qc_session_t *qc_session_create(const uint8_t *server_pub_der, size_t server_pub_len,
                                uint64_t seq0, uint16_t key_id);

/** 销毁会话：内部清零会话密钥/Token/口令派生值并关闭连接。s 可为 NULL。 */
void qc_session_destroy(qc_session_t *s);

/**
 * SPA 单包敲门（可选，取决于服务端是否部署 knockd + nftables 端口隐身）。
 * 在 qc_connect 之前调用：向 knockd 的 UDP 端口发一个 SM4-GCM 加密的单包，
 * 由 knockd 把本客户端源 IP 加入放行集合，随后 TCP 端口才可达。
 *   knock_host/port : knockd 的 UDP 监听地址（默认 5001）
 *   spa_key/key_len : 与 knockd 共享的 SPA 密钥（16B = SM4 密钥长度）
 *   key_id          : SPA 密钥版本（须与 knockd 一致）
 *   user_id         : 32B 用户标识（纳入 AAD 认证）
 *   req_port/req_ttl: 申请放行的端口与时长（秒）；req_ttl=0 时由 knockd 取默认
 * 返回 0 已发出；<0 参数非法 / socket 失败 / 发送失败。
 * 注意：SPA 为静默协议，服务端不回包 —— 「发出成功」不代表「门已开」；
 *       若随后 qc_connect 超时，应重试敲门（时间戳 ±30s 窗口内报文有效）。
 */
int qc_knock(const char *knock_host, int knock_port,
             const uint8_t *spa_key, size_t key_len, uint8_t key_id,
             const uint8_t user_id[32], uint16_t req_port, uint16_t req_ttl);

/**
 * 建立 TCP 连接并完成 SM2 握手。
 *   host/port : netd 地址（IPv4）
 * 返回 0 成功；<0 网络/握手失败（应销毁会话重来）。
 * 内部流程：q_net_connect -> q_chan_handshake（KEY_EXCHANGE 由库内带外完成，非应用指令）。
 */
int qc_connect(qc_session_t *s, const char *host, int port);

/** HELLO 协商（握手后第一条应用指令）。返回 0 成功；<0 断连；>0 为结果码。 */
int qc_hello(qc_session_t *s);

/**
 * 登录前拉取该账号的 salt（方案A：服务端按账号下发的随机盐，防彩虹表）。
 *   username     : 用户名（UTF-8）
 *   salt_out     : 输出 salt 字节（建议 16~32B 缓冲）
 *   salt_cap     : salt_out 容量
 *   salt_len_out : 回写 salt 实际长度
 * 返回 0 成功；<0 断连；>0 为结果码。
 * 调用方随后用 salt 本地派生 pwd_hash（qc_pbkdf2_sm3）再 qc_login。
 */
int qc_fetch_salt(qc_session_t *s, const char *username,
                  uint8_t *salt_out, size_t salt_cap, size_t *salt_len_out);

/**
 * 登录。
 *   username    : 用户名（UTF-8）
 *   pwd_hash    : 32B PBKDF2-SM3(口令, salt, iter>=100000)，由调用方派生（见 qc_pbkdf2_sm3）
 *   otp         : 动态口令文本（如 6 位）；**OTP 种子严禁在 PC 落盘**
 *   devid       : 设备指纹（建议 16~32B）
 *   devid_len   : devid 长度
 *   cver        : 客户端版本文本
 *   token_out   : 输出 32B 会话 Token（仅登录成功有意义）
 * 返回 0 成功；<0 断连；>0 为结果码（见 include_ipc_proto.h：3=Token/凭据问题）。
 */
int qc_login(qc_session_t *s,
             const char *username,
             const uint8_t pwd_hash[32],
             const char *otp,
             const uint8_t *devid, size_t devid_len,
             const char *cver,
             uint8_t token_out[32]);

/**
 * 业务请求（已认证态）。内部：加密发送 -> 阻塞接收 -> 解密，严格成对（lockstep）。
 *   cmd              : 业务指令字（BIZ_QUERY/BIZ_UPDATE/VIDEO_OPEN…）
 *   body/body_len    : 业务体内层 TLV（**必须**以 Q_IPC_T_AUTHTOK(0x0106,32B) 携带会话 Token）
 *   resp_out/resp_cap: 输出明文响应缓冲（TLV 序列）及其容量
 *   resp_len_out     : 回写响应明文长度
 *   cmd_out          : 回写响应命令字（可为 NULL）
 * 返回 0 成功；<0 断连；>0 为结果码。禁止在收到回包前再次调用。
 */
int qc_request(qc_session_t *s,
               uint16_t cmd,
               const uint8_t *body, size_t body_len,
               uint8_t *resp_out, size_t resp_cap, size_t *resp_len_out,
               uint16_t *cmd_out);

/**
 * 业务查询（BIZ_QUERY, 0x0020）便捷封装 —— 三端共用，避免各端手拼 TLV 造成协议漂移。
 *
 * 请求体固定形状（见 include_ipc_proto.h「业务参数段」）：
 *   AUTHTOK(0x0106,32B) + BIZ_TYPE(0x0201,文本) + [BIZ_PARAM(0x0202,文本)]
 *   + [PAGE_OFFSET(0x0203,u32) + PAGE_LIMIT(0x0204,u32)]
 *
 *   biz_type   : 业务子类型，如 "stats" / "companies" / "vehicles"（服务端按此分发）
 *   param_json : 业务参数（UTF-8 JSON 文本）；无参数传 NULL
 *   offset/limit: 分页；**limit==0 表示不分页**（不发送分页 TLV，服务端返回全量）
 *   resp_out   : 明文响应 TLV 序列（RESULT + BODY），由调用方解析 —— 本门面不解析 JSON
 *                （客户端约定：TLV-only，JSON 解析留在各自的 UI 门面层）
 * 返回 0 成功；<0 断连 / 参数非法；>0 服务端结果码（见 Q_IPC_R_*）。
 * 前置：已 qc_login 成功（会话 Token 由本函数自动随带，调用方无需关心）。
 */
int qc_biz_query(qc_session_t *s, const char *biz_type, const char *param_json,
                 uint32_t offset, uint32_t limit,
                 uint8_t *resp_out, size_t resp_cap, size_t *resp_len_out);

/** 心跳（保活 + Token 续期），体为空。返回 0 成功；<0 断连；>0 结果码。 */
int qc_heartbeat(qc_session_t *s);

/** 注销。返回 0 成功；<0 断连；>0 结果码。 */
int qc_logout(qc_session_t *s);

/** 主动关闭连接（不销毁会话对象；可重建连接 qc_connect）。 */
void qc_close(qc_session_t *s);

/**
 * 口令派生辅助（仅本地运算，不触网）：PBKDF2-HMAC-SM3(password, salt, iter) -> out(32)。
 *   iter 建议用 Q_PBKDF2_ITER_DEFAULT（必须与登录服务一致，见 include_ipc_proto.h）；
 *   salt 由 qc_fetch_salt 从服务端拉取（方案A）。
 * 成功 0，失败 -1。登录前由调用方派生出 pwd_hash 再传给 qc_login。
 */
int qc_pbkdf2_sm3(const uint8_t *pass, size_t plen,
                  const uint8_t *salt, size_t slen,
                  uint32_t iter, uint8_t out[32]);

/* ===================== M4：会话治理（keeper） =====================
 * 背景：协议是严格 lockstep（一问一答），后台 Token 有 TTL(1800s) 且心跳**不续期**，
 *       客户端未来会有多个业务线程（列表 / 定位 / 视频）。把心跳、断线重连、
 *       Token 失效重登、请求串行化收敛在通信内核，三端共用一份，避免各端各写一套
 *       导致"偶发卡死 / 偶发结果码 3 / 偶发解包错位"这类最难排查的问题。
 *
 * 线程模型：**一会话一 gate**。
 *   - keeper 线程按 hb_interval 发心跳；失败退避重连；心跳或业务返回 3 时重新登录。
 *   - 业务线程在调用 qc_request / qc_biz_query **前** qc_keeper_lock()，
 *     返回后 qc_keeper_unlock()。心跳与业务因此互斥，lockstep 不被破坏。
 *   - 未来「一个线程一条长连接」的模型（定位 / 视频各一条）天然成立：
 *     每条连接各自 qc_session_create + qc_keeper_start，互不干涉。
 *
 * 口令红线：keeper **不保存口令，也不保存派生值**。重登需要的 pwd_hash
 *           通过 cfg.on_relogin 向 UI 索取（UI 可缓存、可弹窗、可读系统凭据管理器）。
 * 完整说明见模块内部头 qc_keeper.h。
 */
typedef enum qc_state {
    QC_STATE_IDLE         = 0,  /* 未登录 / keeper 未启动 */
    QC_STATE_ONLINE       = 1,  /* 已认证，心跳正常 */
    QC_STATE_RECONNECTING = 2,  /* 连接不可用，退避重连中 */
    QC_STATE_REAUTHING    = 3,  /* 连接仍在，Token 失效，重新登录中 */
    QC_STATE_OFFLINE      = 4   /* 连续失败超上限，需 UI 介入 */
} qc_state_t;

/* 重登凭据回调：UI 侧派生 pwd_hash 填入。返回 0 = 可继续；非 0 = 无法提供（转 OFFLINE） */
typedef int (*qc_relogin_cb)(void *ud, const uint8_t *salt, size_t salt_len,
                             uint32_t iter, uint8_t pwd_hash_out[32]);
/* 状态变化回调：**由 keeper 线程调用**，实现方必须编组回自己的 UI 线程 */
typedef void (*qc_state_cb)(void *ud, int state, int rc, const char *msg);

/* 重连材料（不含口令）：登录成功后由调用方填入 */
typedef struct qc_reconnect {
    char     host[64];
    uint16_t port;
    uint16_t reserved;
    char     user[64];
    char     knock[64];          /* 空 = 不敲门 */
    uint8_t  spa_key[32];
    size_t   spa_key_len;
    uint8_t  spa_key_id;
    uint8_t  devid[32];
    size_t   devid_len;
    char     cver[32];
    uint32_t pbkdf2_iter;        /* 0 = 用协议默认 */
} qc_reconnect_t;

typedef struct qc_keeper_cfg {
    uint32_t hb_interval_sec;    /* 0 = 默认 60 */
    uint32_t backoff_base_sec;   /* 0 = 默认 1 */
    uint32_t backoff_max_sec;    /* 0 = 默认 30 */
    uint32_t max_retry;          /* 连续失败多少次转 OFFLINE；0 = 无限 */
    uint32_t tick_ms;            /* 主循环轮询间隔；0 = 默认 500 */
    uint32_t fault;              /* 故障注入（QC_FAULT_*），生产恒为 0 */
    qc_relogin_cb on_relogin;    /* 必需 */
    void         *relogin_ud;
    qc_state_cb   on_state;      /* 可为 NULL */
    void         *state_ud;
} qc_keeper_cfg_t;

typedef struct qc_keeper_stat {
    int32_t  state;
    uint32_t attempts;        /* 当前连续失败次数 */
    uint32_t hb_sent;
    uint32_t hb_failed;
    uint32_t reconnects;      /* 重建连接成功次数 */
    uint32_t relogins;        /* 自动重登成功次数 */
    uint32_t reserved;
    uint64_t last_ok_ms;      /* 最近一次成功（单调毫秒） */
    uint64_t next_hb_ms;
    uint64_t next_retry_ms;
    char     detail[96];
} qc_keeper_stat_t;

/* 故障注入掩码（仅供联调 / UT） */
#define QC_FAULT_NONE         0x0000u
#define QC_FAULT_HB_FAIL      0x0001u  /* 心跳视为断连 */
#define QC_FAULT_HB_RC3       0x0002u  /* 心跳返回结果码 3（Token 过期） */
#define QC_FAULT_RELOGIN_FAIL 0x0004u  /* 重登必定失败 */

/**
 * 启动会话治理线程。必须在 qc_login 成功**之后**调用（未登录时心跳无意义）。
 *   cfg 可为 NULL：取默认（60s 心跳 / 1s 起 30s 封顶退避 / 无限重连），
 *   但 on_relogin 为 NULL 时只能重连、不能自动重登（Token 过期会转 OFFLINE）。
 * 返回 0 已启动；-1 参数非法 / 未登录；-2 线程创建失败（退化为无治理，门面仍可用）。
 */
int qc_keeper_start(qc_session_t *s, const qc_keeper_cfg_t *cfg);

/** 停止治理线程并等待其退出（幂等）。不会关闭连接（连接由 qc_close / destroy 负责）。 */
void qc_keeper_stop(qc_session_t *s);

/** 取会话状态与统计。out 可为 NULL（仅探测返回值）。返回 0；-1 参数非法。 */
int qc_keeper_state(qc_session_t *s, qc_keeper_stat_t *out);

/**
 * 串行化 gate：业务线程在 qc_request / qc_biz_query 前后成对调用。
 * keeper 心跳也走同一把锁，故心跳与业务严格互斥 —— lockstep 协议不被并发破坏。
 * 注意：**不要**在持锁期间做重活（UI 渲染 / 大批量解析），否则心跳会被拖到超时。
 * 返回 0；-1 参数非法。
 */
int qc_keeper_lock(qc_session_t *s);
int qc_keeper_unlock(qc_session_t *s);

/**
 * 把一次业务请求的结果告诉 keeper：结果码 3（Q_IPC_R_UNAUTHORIZED）时立即触发重新登录。
 * 返回 0 = 无需处理；>0 = 已重登且返回重登的结果码；<0 = 重登失败（连接已废，
 * 调用方应把本次请求视为失败，由 keeper 重连后重试）。
 */
int qc_keeper_note_result(qc_session_t *s, int rc);

/** 唤醒 keeper 线程立即动作（立即心跳 / 立即重连）。返回 0；-1 参数非法或 keeper 未运行。 */
int qc_keeper_wake(qc_session_t *s);

/** 故障注入（联调 / UT 用）。生产恒传 QC_FAULT_NONE。 */
void qc_keeper_set_fault(qc_session_t *s, uint32_t mask);

/**
 * 填入重连材料（不含口令）：登录成功后调用一次， keeper 断线重建时用它重新敲门 + 连接 + 登录。
 * 返回 0；-1 参数非法。
 */
int qc_set_reconnect_info(qc_session_t *s, const qc_reconnect_t *info);

/* ==================== 客户端配置（XML，三端共用一份键名与默认值） ====================
 *
 * 为什么要下沉到 l_4：
 *   配置里装的不是"某个键的值"，而是**业务语义** —— host/port/pubkey 决定抗中间人强度、
 *   pbkdf2_iter 决定口令派生强度、keeper 三个参数决定断线后的重连风暴半径。
 *   若各端 UI 自己解析 XML，必然出现「三份键名不一致的默认值」：Windows 默认心跳 60s、
 *   GTK 默认 5s，表现为"这一端老是掉线、那一端没事"，而日志里完全看不出来。
 *   故解析与默认值都收敛在这里，UI 各端只负责"把配置文件路径告诉我"。
 *
 * XML 形状（键为点分路径，由 l_1/q_xcfg 转 XPath 求值）：
 *   <vms_client>
 *     <server>  <host/> <port/> <pubkey_path/> <seq0/> <key_id/> </server>
 *     <auth>    <user/> <password/> <otp/> <devid_hex/> <pbkdf2_iter/> </auth>
 *     <knock>   <endpoint/> <spa_key_path/> <req_port/> <req_ttl/> <key_id/> </knock>
 *     <keeper>  <heartbeat_ms/> <backoff_base_ms/> <backoff_max_ms/> <max_retry/> <auto_start/> </keeper>
 *     <app>     <client_version/> <log_level/> <connect_timeout_ms/> </app>
 *     <push>    <host/> <port/> <knock/> </push>   <!-- M5：可省略，省略则回退 server.* -->
 *   </vms_client>
 * 全部节点可选：缺失即取默认值，故配置文件可以只写差异项（对运维友好）。
 *
 * 口令（安全红线）：
 *   配置文件**允许**出现 <password>，但加载时会置 QC_CONF_F_PLAIN_PASSWORD 标志并由
 *   调用方决定是否告警 —— 明文口令落盘在等保三级/密评下是不合格项，
 *   正式环境应留空、由登录框输入。本层不阻止是为了保住联调与自动化场景，
 *   但不静默：标志位 + 日志双管齐下，避免"没人知道口令其实躺在一个 xml 里"。
 */
#define QC_HOST_MAX       64    /* 主机名 / IPv4 / "ip:port" */
#define QC_PATH_MAX       260   /* 文件路径（与 VMS_PATH_MAX 对齐） */
#define QC_USER_MAX       64
#define QC_PASS_MAX       64
#define QC_OTP_MAX        16
#define QC_DEVID_HEX_MAX  65    /* 32B 设备指纹的 hex 文本 + '\0' */
#define QC_VER_MAX        32
#define QC_LEVEL_MAX      16

/* 配置标志位 */
#define QC_CONF_F_NONE            0x0000u
#define QC_CONF_F_PLAIN_PASSWORD  0x0001u  /* 配置文件里出现了明文口令 */
#define QC_CONF_F_DEFAULTED       0x0002u  /* 关键项走了默认值（host/port/pubkey 之一缺失） */

typedef struct qc_server_conf {
    char     host[QC_HOST_MAX];        /* netd 主机：IPv4 或域名 */
    uint16_t port;                     /* netd 端口（默认 18099） */
    uint16_t reserved16;
    char     pubkey_path[QC_PATH_MAX]; /* 服务端静态 SM2 公钥 DER（**必填**，抗中间人） */
    uint64_t seq0;                     /* 必须与 netd 一致（默认 1） */
    uint16_t key_id;                   /* 必须与 netd 一致（默认 1） */
    uint16_t reserved16b;
    uint32_t reserved;
} qc_server_conf_t;

typedef struct qc_auth_conf {
    char     user[QC_USER_MAX];
    char     password[QC_PASS_MAX];    /* 明文口令；仅联调用，生产留空 */
    char     otp[QC_OTP_MAX];
    char     devid_hex[QC_DEVID_HEX_MAX];
    uint32_t pbkdf2_iter;              /* 0 = 用协议默认 Q_PBKDF2_ITER_DEFAULT */
    uint32_t reserved;
} qc_auth_conf_t;

typedef struct qc_knock_conf {
    char     endpoint[QC_HOST_MAX];    /* knockd 的 "ip:port"；空 = 不敲门 */
    char     spa_key_path[QC_PATH_MAX];/* SPA 密钥文件（16B SM4 密钥）*/
    uint16_t req_port;                 /* 申请放行的端口；0 = 取 server.port */
    uint16_t req_ttl;                  /* 放行时长（秒）；0 = knockd 默认 */
    uint8_t  key_id;                   /* SPA 密钥版本，须与 knockd 一致 */
    uint8_t  reserved8[3];
    uint32_t reserved;
} qc_knock_conf_t;

typedef struct qc_keeper_conf {
    uint32_t heartbeat_ms;             /* 心跳间隔（默认 60000） */
    uint32_t backoff_base_ms;          /* 退避基数（默认 1000） */
    uint32_t backoff_max_ms;           /* 退避上限（默认 30000） */
    uint32_t max_retry;                /* 0 = 无限重试 */
    uint32_t auto_start;               /* 登录成功后自动起 keeper（默认 1） */
    uint32_t reserved;
} qc_keeper_conf_t;

typedef struct qc_app_conf {
    char     client_version[QC_VER_MAX];
    char     log_level[QC_LEVEL_MAX];
    uint32_t connect_timeout_ms;
    uint32_t reserved;
} qc_app_conf_t;

/* M5 推送段：推送长连接的独立寻址（与命令连接是两条独立 TCP / 两套 knockd+netd 实例）。
 * 三项全部可省略：host 空 = 用 server.host，port 0 = 用 server.port，
 * knock 空 = 用 knock.endpoint（单栈部署的兼容形态）。 */
typedef struct qc_push_conf {
    char     host[QC_HOST_MAX];        /* netd_B 主机：IPv4 或**域名**（连接层 getaddrinfo） */
    uint16_t port;                     /* netd_B 的 TCP 端口（默认回退 server.port） */
    uint16_t reserved16;
    char     knock[QC_HOST_MAX];       /* knockd_B 的 UDP 端点 "host:port"；空 = 用 knock.endpoint */
    uint32_t reserved;
} qc_push_conf_t;

typedef struct qc_client_conf {
    qc_server_conf_t server;
    qc_auth_conf_t   auth;
    qc_knock_conf_t  knock;
    qc_keeper_conf_t keeper;
    qc_app_conf_t    app;
    qc_push_conf_t   push;             /* M5：推送长连接寻址 */
    uint32_t flags;                    /* QC_CONF_F_* */
    uint32_t reserved;
    char     source_path[QC_PATH_MAX]; /* 实际加载的文件路径（排障用） */
} qc_client_conf_t;

/** 填充出厂默认值（不读文件）。c 必须非 NULL。返回 0。 */
int qc_conf_default(qc_client_conf_t *c);

/**
 * 加载 XML 配置文件：先 qc_conf_default 再逐项覆盖。
 *   path : 配置文件路径；NULL 或空串 = 用 qc_conf_default_path() 的默认路径
 * 返回 0 成功（缺失项走默认，flags 会记 QC_CONF_F_DEFAULTED）；
 *       -1 参数非法；-2 文件不存在或打不开；-3 XML 解析失败；-4 内存不足。
 * 注意：本函数不校验业务必填项（host/user/pubkey），校验由 qc_conf_validate 负责 ——
 *       分开是为了让 UI 能先"加载出一份可读的配置"再决定怎么提示，而不是一错到底。
 */
int qc_conf_load(const char *path, qc_client_conf_t *out);

/**
 * 业务校验：host/port/pubkey_path 必填；port 在 1..65535；pbkdf2_iter 为 0 或 >= 100000。
 *   err/err_cap : 可选的人类可读原因（口令不会写进去）
 * 返回 0 合法；-1 参数非法；>0 第一个不合法项的编号（1=host 2=port 3=pubkey 4=iter）。
 */
int qc_conf_validate(const qc_client_conf_t *c, char *err, size_t err_cap);

/** 清零敏感字段（口令/OTP）并整体归零。c 可为 NULL。 */
void qc_conf_free(qc_client_conf_t *c);

/**
 * 三端默认配置文件路径。查找顺序：
 *   1) 环境变量 VMS_CLIENT_CONF（显式指定，联调/多实例用）
 *   2) 平台惯例：Win=%LOCALAPPDATA%\vms\vms_client.xml（无则用 .\vms_client.xml）
 *                Linux=$XDG_CONFIG_HOME|~/.config/vms/vms_client.xml
 *                macOS=~/Library/Application Support/vms/vms_client.xml
 * out 为 NULL 时仅返回"是否有可用默认路径"（1/0），便于调用方先探测。
 * 返回 0 成功写入 out；-1 参数非法或无法确定（此时 out[0]='\0'）。
 */
int qc_conf_default_path(char *out, size_t cap);

/** 打印配置（调试用，输出到 fp）。口令与 OTP 一律打 ****，防止日志泄漏。 */
void qc_conf_dump(const qc_client_conf_t *c, void *fp);

#ifdef __cplusplus
}
#endif

#endif /* QC_CLIENT_H */
