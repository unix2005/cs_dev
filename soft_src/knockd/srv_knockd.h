#ifndef _SRV_KNOCKD_H_
#define _SRV_KNOCKD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "q_common.h"
#include "q_reactor.h"
#include "q_spa.h"
#include <q_xcfg.h>             /* l_1 XML 配置库 */
#include <q_log.h>              /* l_1 结构化日志 / 审计库 */
#include <q_crypto.h>            /* l_1 国密：SM3 链式哈希（审计防篡改） */
#include "nft_min.h"             /* 本地最小 libnftables API（直连 netlink，无 fork/sh） */
#include <seccomp.h>             /* P0 seccomp 沙箱（libseccomp 高层 API） */

#ifndef AF_NETLINK               /* 限定 socket 地址族时用到；部分头未暴露则兜底 */
#define AF_NETLINK 16
#endif

#define KNOCKD_PORT 5001       /* listen_port 默认值 */
#define SPA_KEY_PATH "/etc/gtkvms/spa.key" /* spa_key_file 默认值 */
#define SPA_KEY_LEN 16         /* SM4 密钥长度（固定安全常数，不可配置） */
#define NFT_TABLE "inet spa"   /* nft_table 默认值 */
#define NFT_SET "allow4"       /* nft_set 默认值（IPv4 放行集合，type ipv4_addr） */
#define NFT_SET6 "allow6"      /* nft_set6 默认值（IPv6 放行集合，type ipv6_addr） */
#define KNOCKD_TTL_DEF 300     /* default_ttl 默认值（秒） */
#define KNOCKD_POLL_DEF 200    /* poll_timeout_ms 默认值 */
#define KNOCKD_CONF_PATH "/etc/gtkvms/knockd_conf.xml" /* 配置文件默认路径 */

/* 运行期配置（由 knockd_conf.xml 载入；listen_port/key 等可被环境变量覆盖） */
#define KNOCKD_PATH_MAX 256
typedef struct
{
    int   listen_port;                   /* UDP 监听端口 */
    int   default_ttl;                   /* 开门默认时长（秒），报文 req_ttl=0 时取此值 */
    int   poll_timeout_ms;               /* reactor 轮询超时（毫秒） */
    char  spa_key_file[KNOCKD_PATH_MAX]; /* SPA 共享密钥文件路径 */
    char  nft_table[64];                 /* nftables 表名 */
    char  nft_set[64];                   /* IPv4 放行集合名（type ipv4_addr，带 timeout 过期） */
    char  nft_set6[64];                  /* IPv6 放行集合名（type ipv6_addr，带 timeout 过期） */
    char  log_file[512];                  /* 日志文件路径（空=仅控制台） */
    char  log_level[16];                  /* 日志级别 trace/debug/info/warn/error/fatal */
    int   log_console;                    /* 是否同时输出到 stderr */
    int   log_max_size;                   /* 单文件轮转上限（字节，0=不轮转） */
} knockd_cfg_t;


/* 每个监听套接字绑定一个上下文，用于回调内区分应写入哪个 nft 集合（v4/v6） */
typedef struct
{
    const char *set;          /* 指向 g_cfg 中的集合名（运行期填充） */
} knock_ctx_t;

int knockd_install_seccomp(q_log_t *log);

#ifdef __cplusplus
}
#endif
#endif