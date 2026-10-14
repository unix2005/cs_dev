/**
 * @file srv_bizd.h
 * @brief bizd 业务进程（仅 Unix Socket，不对外暴露公网）公共头文件
 * @platform Windows/Linux/macOS
 *
 * @note 职责（技术方案 V2 §1.2）：接收 netd 经 q_ipc 转发的【可信 TLV】业务请求，
 *       转换为 JSON 后经 TCP 下发给具体的下游微服务（在另一项目实现），
 *       再把微服务的 JSON 响应回传给 netd。DB 凭据与业务逻辑均在下游微服务侧，
 *       本进程不持有 DB 凭据。
 *
 *       虽为可信入站，但仍按 q_codec 字段校验表做结构校验，绝不信任原始字节。
 *
 * wire 契约（分帧 / TLV tag / 结果码）与 netd 共用 include_ipc_proto.h，
 *       禁止两端各自重复定义，避免协议漂移。
 *
 * ===================== 下游微服务 JSON 包格式 =====================
 * 【请求】bizd -> 微服务
 * {
 *   "ver": 1,                     // 协议版本，固定 1
 *   "id":  "17",                  // 请求 id（十进制字符串），响应原样回带用于关联
 *   "cmd": 32,                       // 指令字（数值，与 §5.3 指令表一致）
 *   "cmd_name": "BIZ_QUERY",      // 指令名（可读，便于排障；微服务以 cmd 为准）
 *   "ctx": {                         // 安全上下文（由 netd 转发）
 *     "src_ip":    "203.0.113.7", // 客户端真实源 IP
 *     "user_id":   "<64 hex>",    // SM3(用户名)
 *     "device_id": "<64 hex>",    // 设备指纹
 *     "channel_id":"<64 hex>"      // SM3(session_key)，通道绑定
 *   },
 *   "token": "<64 hex>",            // 会话 Token（LOGIN 时为空串）
 *   "params": { }                   // 业务参数（由业务体 TLV 解码而来）
 * }
 * 【响应】微服务 -> bizd
 * {
 *   "ver": 1, "id": "17",
 *   "result": 0,                    // 结果码：0 成功；非 0 失败（见上表）
 *   "message": "ok",
 *   "data": { }                     // 业务数据；bizd 原样序列化后回传 netd
 * }
 * ============================================================
 */
#ifndef _SRV_BIZD_H_
#define _SRV_BIZD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "include_thd.h"
#include "include_ipc_proto.h"
#include "include_xml.h"      /* 提供 cJSON（<cjson/cJSON.h>），与 knockd 一致 */

#include "q_common.h"
#include "q_reactor.h"
#include "q_net.h"        /* 下游 TCP 转发：q_net_connect / *_blocking / close */
#include "q_ipc.h"
#include "q_codec.h"
#include "q_chan.h"
#include "q_sec.h"
#include "q_mem.h"
#include <q_crypto.h>
#include <q_log.h>
#include <q_xcfg.h>

/* ===================== 默认配置 ===================== */
#define BIZD_IPC_PATH_DEF    "/var/run/gtkvms/bizd.sock"
#define BIZD_BACKLOG_DEF     64
#define BIZD_NWORKERS_DEF    8
#define BIZD_MAXQ_DEF        1024
#define BIZD_POLL_MS_DEF     200
#define BIZD_LOG_FILE_DEF    "/var/log/gtkvms/bizd.log"

#define BIZD_CONF_DEF        "/etc/gtkvms/bizd_conf.xml"
#define BIZD_CONF_ENV        "GTKVMS_BIZD_CONF"

/* 下游微服务默认端点（另一项目实现，TCP + JSON） */
#define BIZD_EP_HELLO_HOST_DEF  "127.0.0.1"
#define BIZD_EP_HELLO_PORT_DEF 9001
#define BIZD_EP_AUTH_HOST_DEF   "127.0.0.1"
#define BIZD_EP_AUTH_PORT_DEF  9002
#define BIZD_EP_BIZ_HOST_DEF    "127.0.0.1"
#define BIZD_EP_BIZ_PORT_DEF   9003
#define BIZD_EP_VIDEO_HOST_DEF  "127.0.0.1"
#define BIZD_EP_VIDEO_PORT_DEF 9004

/* 收发超时（秒）：防下游异常连接长期占用 worker */
#define BIZD_IO_TIMEOUT_SEC  10

/* ===================== 类型定义 ===================== */
/* 下游微服务端点 */
typedef struct
{
    char host[64];
    int  port;
} bizd_ep_t;

/* 运行期配置：由 bizd_conf.xml 载入 */
typedef struct
{
    char      ipc_path[256];   /* UDS 监听路径（netd 侧的 ipc_path 须一致） */
    int       backlog;
    int       nworkers;
    int       max_queue;
    int       poll_ms;
    bizd_ep_t ep_hello;        /* HELLO          能力协商 */
    bizd_ep_t ep_auth;         /* LOGIN/HEARTBEAT/LOGOUT  会话相关 */
    bizd_ep_t ep_biz;          /* BIZ_QUERY/BIZ_UPDATE    业务数据 */
    bizd_ep_t ep_video;        /* VIDEO_OPEN      视频调度 */
    char      log_file[256];
    int       log_level;
    int       log_console;
    uint64_t  log_max_size;
} bizd_cfg_t;

typedef struct bizd_ctx bizd_ctx_t;

/* 每连接上下文：UDS accept 后分配，worker 线程独占（无需加锁） */
typedef struct
{
    int              fd;               /* q_ipc 系列接口使用 int */
    uint8_t          src_fam;          /* 地址族：4=IPv4 / 6=IPv6（来自 IPC SRCIP TLV） */
    uint8_t          src_ip[16];       /* 客户端源地址（网络序，扩为 16B 以支持 IPv6） */
    uint16_t         cmd;              /* netd 转发的业务指令字 */
    uint8_t          channel_id[32];   /* 通道绑定 ID */
    uint8_t          user_id[32];      /* SM3(用户名) */
    uint8_t          device_id[32];  /* 设备标识 */
    uint8_t          token[32];         /* 会话 Token */
    int              has_token;
    const uint8_t   *body;             /* 业务体（内层 TLV 序列） */
    size_t           body_len;
    char             req_id[32];       /* 本请求 id：bizd 转发时生成，与下游 JSON "id" 一致，用于跨服务对账 */
    bizd_ctx_t      *g;
} bizd_conn_t;

/* 客户端身份（来自 netd IPC 转发的 Q_IPC_T_SRCIP，仅 IPv4、**无端口**）。
   薄封装：委托通用 q_net_addr_ntop 格式化；因 IPC 不携带源端口，port 恒传 0，
   日志表现为 "ip:0"（排障以 IP 定位即可）。返回 buf；c==NULL 或缓冲不足回传 "?"。
   调用方需提供 char buf[INET6_ADDRSTRLEN + 8]。 */
static inline const char *bizd_conn_ntop(const bizd_conn_t *c, char *buf, size_t buflen)
{
    if (!c || buflen < INET6_ADDRSTRLEN + 8)
        return "?";
    return q_net_addr_ntop((c->src_fam == 6) ? AF_INET6 : AF_INET,
                           (const void *)c->src_ip, 0, buf, buflen);
}

/* 跨服务对账标识：src_ip#req_id（req_id 即发给下游 JSON 的 "id"，两端一致便于 grep 关联同一请求）；
   已登录请求（user_id 非全 0）附 (uid=<SM3(用户名) 64hex>)。返回 buf；c==NULL 或缓冲不足回传 "?"。 */
static inline const char *bizd_cid(const bizd_conn_t *c, char *buf, size_t buflen)
{
    char        ep[INET6_ADDRSTRLEN + 8];
    const char *ip;
    size_t      i;
    int         nz = 0;

    if (!c || buflen < INET6_ADDRSTRLEN + 128)
        return "?";
    ip = bizd_conn_ntop(c, ep, sizeof(ep));
    if (c->req_id[0])
    {
        for (i = 0u; i < 32u; i++)
            if (c->user_id[i]) { nz = 1; break; }
        if (nz)
        {
            char hex[65];
            for (i = 0u; i < 32u; i++)
                snprintf(hex + i * 2u, 3u, "%02x", (unsigned)c->user_id[i]);
            snprintf(buf, buflen, "%s#%s(uid=%s)", ip, c->req_id, hex);
        }
        else
            snprintf(buf, buflen, "%s#%s", ip, c->req_id);
    }
    else
        snprintf(buf, buflen, "%s", ip);
    return buf;
}

struct bizd_ctx
{
    q_reactor_t   *reactor;
    q_tpool_t     *pool;
    q_chan_disp_t *disp;
    q_log_t       *log;
    int              listen_fd;
    q_mutex_t        lock;
};

/* 处理器用户数据：把「全局上下文 + 当前连接」带给 handler */
typedef struct
{
    bizd_ctx_t  *g;
    bizd_conn_t *c;
} bizd_udata_t;

/* ===================== 全局对象 ===================== */
extern bizd_cfg_t g_bcfg;   /* 定义于 bizd_config.c */
extern bizd_ctx_t g_bizd;   /* 定义于 main.c */

/* ===================== 配置（bizd_config.c） ===================== */
void bizd_cfg_defaults(void);
int  bizd_load_config(const char *path);

/* ===================== 工具（bizd_util.c） ===================== */
uint64_t bizd_now_sec(void);
void     bizd_put_be16(uint8_t *p, uint16_t v);
void     bizd_put_be32(uint8_t *p, uint32_t v);
uint16_t bizd_get_be16(const uint8_t *p);
uint32_t bizd_get_be32(const uint8_t *p);
int      bizd_set_io_timeout(int fd, int sec);
int      bizd_send_frame(int fd, const uint8_t *buf, size_t len);
int      bizd_recv_frame(int fd, uint8_t *buf, size_t cap, size_t *outlen);

/* ===================== 路由转发（bizd_route.c） ===================== */
/* 按 cmd 选下游端点，发 JSON 收 JSON；成功返回 0，*rsp_json 须由调用方 free */
int bizd_route_call(uint16_t cmd, const char *req_json, char **rsp_json);

/* ===================== 业务分发（bizd_handler.c） ===================== */
int bizd_disp_init(q_chan_disp_t *d, void *ud);

/* ===================== 服务器（bizd_srv.c） ===================== */
void bizd_conn_worker(void *arg);
void bizd_on_accept(q_fd_t fd, uint32_t events, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _SRV_BIZD_H_ */
