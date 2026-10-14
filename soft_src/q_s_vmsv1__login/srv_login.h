/**
 * @file srv_login.h
 * @brief 登录微服务（q_service_login）公共头文件
 * @platform Windows/Linux/macOS
 *
 * @note 定位：bizd 的【下游微服务】（“另一项目”侧），经 TCP 接收 bizd 转发的 JSON 信封，
 *       完成登录鉴权并回写 JSON 响应。用于打通  客户端 -> netd -> bizd -> login 全链路测试。
 *
 *       wire 契约（与 bizd srv_bizd.h 头部注释一致，勿漂移）：
 *       - 传输：TCP；分帧 [4 字节大端长度][JSON]。
 *       - 请求（bizd -> login）：
 *         { "ver":1, "id":"..", "cmd":<num>,
 *           "ctx":{"src_ip":"..","channel_id":"..","user_id":"..","device_id":".."},
 *           "token":"..",
 *           "params":{ "username":"<hex>", "pwd_hash":"<hex>", ... } }   // params 值均 hex
 *       - 响应（login -> bizd）：
 *         { "ver":1, "id":"..", "result":0, "message":"ok", "data":{...} }
 *
 *       指令码复用 q_chan.h 的 Q_CHAN_CMD_*（bizd 按 cmd 路由：
 *       LOGIN/HEARTBEAT/LOGOUT -> 本服务，默认端口 9002 = bizd ep_auth）。
 */
#ifndef _SRV_LOGIN_H_
#define _SRV_LOGIN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "include_stdio.h"
#include "include_net.h"
#include "include_thd.h"
#include "include_time.h"
#include "include_xml.h"      /* 提供 cJSON */

#include "q_common.h"
#include "q_reactor.h"
#include "q_net.h"
#include "q_log.h"
#include "q_rand.h"
#include "q_chan.h"           /* 仅取 Q_CHAN_CMD_* 指令码，避免重复定义 wire 契约 */

/* ===================== 默认配置 ===================== */
#define LOGIN_PORT_DEF        9002    /* 对齐 bizd ep_auth（LOGIN/HEARTBEAT/LOGOUT） */
#define LOGIN_BACKLOG_DEF     64
#define LOGIN_NWORKERS_DEF    8
#define LOGIN_POLL_MS_DEF     200
#define LOGIN_IO_TIMEOUT_SEC  10
#define LOGIN_MAXMSG          65536
#define LOGIN_LENPFX          4
#define LOGIN_TOKEN_LEN       32      /* token 明文字节数；hex 后 64 字符 */
#define LOGIN_USER_DEF        "qiaoshui"
#define LOGIN_PASS_DEF        "123456"

/* 响应 result 码（login 自有，语义对齐 srv_bizd.h 响应） */
#define LOGIN_R_OK           0
#define LOGIN_R_BADREQ       1   /* 请求解析失败 / 缺字段 */
#define LOGIN_R_BADCRED      2   /* 用户名或密码不匹配 */
#define LOGIN_R_UNSUPPORTED  3   /* 不支持的指令 */
#define LOGIN_R_INTERNAL     4

/* ===================== 运行期配置 ===================== */
typedef struct
{
    int       port;
    int       backlog;
    int       nworkers;
    int       poll_ms;
    int       io_timeout_sec;
    char      user[64];
    char      pass[64];
    char      log_file[256];  /* 空 = 仅控制台 */
    int       log_level;
    int       log_console;
    uint64_t  log_max_size;
} login_cfg_t;

struct login_ctx
{
    q_reactor_t    *reactor;
    q_tpool_t      *pool;
    q_log_t        *log;
    q_fd_t          listen_fd;
};

/* 每连接上下文：accept 后分配，worker 线程独占（无需加锁） */
typedef struct
{
    q_fd_t           fd;
    struct login_ctx *g;
} login_conn_t;

/* ===================== 全局对象 ===================== */
extern login_cfg_t       g_lcfg;   /* 定义于 main.c */
extern struct login_ctx  g_login;  /* 定义于 main.c */

/* ===================== 配置（main.c） ===================== */
void login_cfg_defaults(void);

/* ===================== 工具（login_util.c） ===================== */
void     login_put_be32(uint8_t *p, uint32_t v);
uint32_t login_get_be32(const uint8_t *p);
int      login_hex_encode(const uint8_t *b, size_t len, char *out, size_t outcap);
int      login_hex_decode(const char *hex, uint8_t *out, size_t outcap);
int      login_set_io_timeout(q_fd_t fd, int sec);
int      login_send_frame(q_fd_t fd, const uint8_t *buf, size_t len);
int      login_recv_frame(q_fd_t fd, uint8_t *buf, size_t cap, size_t *outlen);

/* ===================== 业务处理（login_handler.c） ===================== */
/* 解析 bizd JSON 信封 -> 生成响应 JSON（调用方 free）；解析失败返回 NULL */
char *login_handle(struct login_ctx *g, const char *req_json, size_t req_len);

/* ===================== 服务器（login_srv.c） ===================== */
void login_on_accept(q_fd_t fd, uint32_t events, void *ctx);
void login_conn_worker(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* _SRV_LOGIN_H_ */
