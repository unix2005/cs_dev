/**
 * @file srv_netd.h
 * @brief netd 协议进程（公网 TCP）公共头文件
 * @platform Linux（reactor + chroot/seccomp 降权）；macOS 可编译运行（kqueue）
 * @note 聚合本进程所需的全部公共头与内部共享类型 / 常量 / 接口；
 *       各 .c 仅 #include "srv_netd.h"（与 knockd/srv_knockd.h 范式一致）。
 *
 * 并发模型要点（详见 main.c 头部）：q_chan 的 handshake/receive 是【阻塞多轮往返】
 * 语义，与 q_net_srv「非阻塞 fd + reactor 读空后按 chunk 派发」冲突，故本进程自建
 * accept 循环 + 有界线程池派发【整条连接】，在 worker 内跑阻塞式 q_chan 全流程。
 */
#ifndef _SRV_NETD_H_
#define _SRV_NETD_H_

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
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "include_thd.h"       /* q_mutex_t（pthread 别名） */
#include "include_ipc_proto.h" /* netd<->bizd wire 契约（与 bizd 共用同一份定义） */

#include "q_common.h"         /* l_1 守护进程公共：信号 / 降权 */
#include "q_reactor.h"        /* l_1 事件循环 + 有界线程池 */
#include "q_net.h"            /* l_2 socket 封装 / 阻塞收发 */
#include "q_chan.h"           /* l_3 安全通道 + 状态机 + 分发 + Token */
#include "q_sec.h"            /* l_2 限流计数器 / PoW */
#include "q_codec.h"          /* l_2 TLV 编解码 */
#include "q_ipc.h"            /* l_2 Unix Domain Socket */
#include "q_mem.h"            /* l_1 安全清零 */
#include "q_crypto.h"         /* l_1 国密 SM2/SM3 */
#include "q_log.h"            /* l_1 日志 / 审计 */
#include "q_xcfg.h"           /* l_1 XML 配置 */

/* ===================== 默认配置 ===================== */
#define NETD_PORT_DEF        18099
#define NETD_BACKLOG_DEF     64
#define NETD_NWORKERS_DEF    8
#define NETD_MAXQ_DEF        1024
#define NETD_POLL_MS_DEF     200
#define NETD_KEY_ID_DEF      1u

#define NETD_CONF_DEF        "/etc/gtkvms/netd_conf.xml"
#define NETD_IPC_PATH_DEF    "/var/run/gtkvms/bizd.sock"
#define NETD_PRIV_FILE_DEF   "/etc/gtkvms/netd_sm2.priv"
#define NETD_PUB_FILE_DEF    "/etc/gtkvms/netd_sm2.pub"
#define NETD_LOG_FILE_DEF    "/var/log/gtkvms/netd.log"

#define NETD_CONF_ENV        "GTKVMS_NETD_CONF"

/* ===================== §6.4 限流矩阵 ===================== */
#define NETD_UNAUTH_PKTS     4            /* 未认证连接最大收包数 */
#define NETD_UNAUTH_BYTES    (8u * 1024u)  /* 未认证连接最大字节数 */
#define NETD_UNAUTH_SEC      10           /* 未认证连接最大存续（秒） */
#define NETD_PERIP_CONN      8            /* 单 IP 并发连接上限 */
#define NETD_PERIP_NEW       5            /* 单 IP 新建连接速率（次/秒） */
#define NETD_PERIP_FAIL      20           /* 单 IP 登录失败次数上限 */
#define NETD_PERIP_FAIL_WIN  300          /* 登录失败统计窗口（秒） */
#define NETD_PERIP_BLK_SEC   1800         /* 触发后拉黑时长（秒） */
#define NETD_GLOBAL_CONN     4096         /* 全局并发连接上限 */

/* 握手协商用固定 seq0（双方必须一致），沿用 q_chan 内部约定常量值 1 */
#define NETD_SEQ0            ((uint64_t)1)

/* ---- netd 自身参数（非 wire 契约，故保留在模块头内） ---- */
#define NETD_PLAIN_MAX        Q_IPC_TLV_MAX  /* 业务体上限：与 IPC 单条 TLV 一致收口 */
#define NETD_RESP_MAX         (NETD_PLAIN_MAX + 256u)
#define NETD_IO_TIMEOUT_SEC   10             /* 收发超时（秒）：防异常连接长期占用 worker */
#define NETD_TOKEN_TTL_SEC    1800          /* 会话 Token 有效期：§6.3 默认 30 分钟 */

/* ===================== 类型定义 ===================== */
/* 运行期配置：由 netd_conf.xml 载入；环境变量 GTKVMS_NETD_CONF 指定配置路径 */
typedef struct
{
    int      listen_port;
    int      backlog;
    int      nworkers;
    int      max_queue;
    int      poll_ms;
    uint16_t key_id;
    char     ipc_path[256];
    char     priv_file[256];
    char     pub_file[256];
    char     log_file[256];
    int      log_level;      /* q_log_level_t */
    int      log_console;
    uint64_t log_max_size;
    int      enable_ipv6;
} netd_cfg_t;

typedef struct netd_ctx netd_ctx_t;

/* 每连接上下文：由 accept 循环分配，worker 线程独占（无需加锁） */
typedef struct
{
    q_fd_t         fd;
    uint32_t       src_ip;    /* 网络序 IPv4（IPv6 记为 0，见 TODO） */
    uint16_t       src_port;
    q_chan_st_t    st;
    uint8_t        channel_id[32];
    q_chan_token_t token;
    int            has_token;
    time_t         accept_ts;
    size_t         pkts;      /* 未认证阶段收包计数 */
    size_t         bytes;     /* 未认证阶段字节计数 */
    netd_ctx_t    *g;
} netd_conn_t;

struct netd_ctx
{
    q_reactor_t   *reactor;
    q_tpool_t     *pool;
    q_log_t       *log;
    q_fd_t         listen_fd;
    q_fd_t         listen_fd6;
    uint8_t       *sm2_priv;
    size_t         sm2_priv_len;
    uint8_t       *sm2_pub;
    size_t         sm2_pub_len;
    uint16_t       key_id;
    q_mutex_t      lock;      /* 保护配额计数与 IP 表 */
    int            conn_total;
};

/* IP 维度限流表项（定长表 + 线性探测） */
#define NETD_IPTAB 1024u
typedef struct
{
    uint32_t           ip;         /* 网络序；0 = 空槽 */
    int                conns;      /* 当前并发连接数 */
    q_sec_rate_limit_t *rl_new;    /* 新建速率 NETD_PERIP_NEW / 1s */
    q_sec_rate_limit_t *rl_fail;   /* 登录失败 NETD_PERIP_FAIL / 300s */
    time_t             blk_until;  /* 拉黑截止时间 */
} netd_ipent_t;

/* 处理器用户数据：把「全局上下文 + 当前连接」一起带给 handler */
typedef struct
{
    netd_ctx_t  *g;
    netd_conn_t *c;
} netd_udata_t;

/* ===================== 全局对象 ===================== */
extern netd_cfg_t g_cfg;   /* 定义于 netd_config.c */
extern netd_ctx_t g_netd;  /* 定义于 main.c */

/* ===================== 配置（netd_config.c） ===================== */
void cfg_defaults(void);
int  load_config(const char *path);
int  load_sm2_keypair(netd_ctx_t *g);

/* ===================== 工具（netd_util.c） ===================== */
uint64_t now_sec(void);
void     put_be16(uint8_t *p, uint16_t v);
void     put_be32(uint8_t *p, uint32_t v);
int      netd_set_io_timeout(q_fd_t fd, int sec);

/* ===================== 限流（netd_limit.c） ===================== */
int  conn_acquire(netd_ctx_t *g, uint32_t src_ip);
void conn_release(netd_ctx_t *g, uint32_t src_ip);
void fail_note(netd_ctx_t *g, uint32_t src_ip);

/* ===================== IPC 转发（netd_ipc.c） ===================== */
int ipc_forward(netd_ctx_t *g, uint32_t src_ip, uint16_t cmd,
                const uint8_t *body, size_t blen,
                const uint8_t chanid[32], const uint8_t userid[32],
                uint32_t *result, uint8_t *out, size_t outcap, size_t *out_len);

/* ===================== 业务处理 / 登录（netd_handler.c） ===================== */
int netd_disp_init(q_chan_disp_t *d, void *ud);
int do_login(netd_conn_t *c, const uint8_t *body, size_t blen,
             uint8_t *out, size_t outcap, size_t *out_len);

/* ===================== 服务器（netd_srv.c） ===================== */
void conn_worker(void *arg);
void on_accept(q_fd_t fd, uint32_t events, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _SRV_NETD_H_ */
