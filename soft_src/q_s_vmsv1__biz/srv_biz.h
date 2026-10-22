/**
 * @file srv_biz.h
 * @brief 业务微服务（q_service_biz）公共头文件
 * @platform Windows/Linux/macOS
 *
 * @note 定位：bizd 的【下游微服务】（“另一项目”侧），经 TCP 接收 bizd 转发的 JSON 信封，
 *       按 params.biz_type 返回业务数据，回写 JSON 响应。用于打通
 *       客户端 -> netd -> bizd -> biz 全链路测试（M2：先打通 stats 一条业务查询）。
 *
 *       wire 契约（与 bizd srv_bizd.h 头部注释一致，勿漂移）：
 *       - 传输：TCP；分帧 [4 字节大端长度][JSON]。
 *       - 请求（bizd -> biz）：
 *         { "ver":1, "id":"..", "cmd":32,
 *           "ctx":{"src_ip":"..","channel_id":"..","user_id":"..","device_id":".."},
 *           "token":"<64 hex>",
 *           "params":{ "token":"<hex>", "biz_type":"stats", "biz_param":"{...}",
 *                      "offset":0, "limit":100 } }
 *       - 响应（biz -> bizd）：
 *         { "ver":1, "id":"..", "result":0, "message":"ok", "data":{...} }
 *
 *       指令码复用 q_chan.h 的 Q_CHAN_CMD_*（bizd 按 cmd 路由：
 *       BIZ_QUERY/BIZ_UPDATE -> 本服务，默认端口 9003 = bizd ep_biz）。
 *
 *       注意：bizd 的通用转发**不解析**下游 result，仅把整个响应 JSON 塞进
 *       Q_IPC_T_BODY 回传，故 result 的语义由【客户端在 C 层解析】——
 *       本服务的 result 必须与 include_ipc_proto.h 的 Q_IPC_R_* 对齐。
 */
#ifndef _SRV_BIZ_H_
#define _SRV_BIZ_H_

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
#include "include_ipc_proto.h" /* Q_IPC_R_* 结果码 / 业务参数 tag（与三端共用同一事实来源） */

/* ===================== 默认配置 ===================== */
#define BIZ_PORT_DEF        9003    /* 对齐 bizd ep_biz（BIZ_QUERY / BIZ_UPDATE） */
#define BIZ_BACKLOG_DEF     64
#define BIZ_NWORKERS_DEF    8
#define BIZ_POLL_MS_DEF     200
#define BIZ_IO_TIMEOUT_SEC  10
#define BIZ_MAXMSG          65536
#define BIZ_LENPFX          4
#define BIZ_TOKEN_LEN       32      /* token 明文字节数；hex 后 64 字符 */

/* 桩服务返回的统计基线（刻意与客户端 mock 一致，便于一眼分辨数据来源；
   可用 --total/--online/--alarm 覆盖，用来证明"状态栏数字确实来自服务端"） */
#define BIZ_TOTAL_DEF           34554u
#define BIZ_ONLINE_DEF          10846u
#define BIZ_OFFLINE_DEF         23708u
#define BIZ_EXCLUDED_DEF        0u
#define BIZ_ALARM_UNHANDLED_DEF 46343u

/* 支持的业务子类型（params.biz_type） */
#define BIZ_TYPE_STATS      "stats"
#define BIZ_TYPE_COMPANIES  "companies"
#define BIZ_TYPE_VEHICLES   "vehicles"

/* ===================== M3：桩数据规模（不连库的替身） =====================
 * 真实部署时 d_org_info / d_vehicleinfo 由 DB 提供；此处用确定性算法按行号生成，
 * 保证同一 (offset,limit) 每次返回完全一致（分页翻页不会重复/丢行）。 */
#define BIZ_VTOTAL_DEF      34554u  /* d_vehicleinfo 行数（桩；--vtotal 可覆盖） */
#define BIZ_VONLINE_DEF     10846u  /* 在线数（桩；--vonline 可覆盖） */
#define BIZ_ID_MAX          40      /* 主键字符串容量：org_id CHAR(36) / vehicle_id CHAR(32) */
#define BIZ_KW_MAX          64      /* 关键字（车牌/自编号）最大长度 */
#define BIZ_IDS_MAX         256     /* 单次按主键精确过滤的车辆数上限（对应 VMS_QUERY_MAX_IDS） */
#define BIZ_PAGE_LIMIT_DEF  50u     /* 未指定 limit 时的默认页大小 */
#define BIZ_PAGE_LIMIT_MAX  100u    /* 单页上限：受 Q_IPC_TLV_MAX(60000B) 约束 ——
                                       一条车辆 JSON 约 480B，100 条约 48KB 已接近单帧上限，
                                       再大会被 bizd 按 TLV 长度截断，客户端拿到残缺 JSON。 */

/* ===================== 运行期配置 ===================== */
typedef struct
{
    int       port;
    int       backlog;
    int       nworkers;
    int       poll_ms;
    int       io_timeout_sec;

    /* 统计基线（桩数据；真实部署应由 DB / 缓存提供） */
    uint32_t  total;
    uint32_t  online;
    uint32_t  offline;
    uint32_t  excluded;
    uint32_t  alarm_unhandled;

    /* M3：d_vehicleinfo 桩规模（车辆总数 / 在线数 / 单页上限） */
    uint32_t  vehicle_total;
    uint32_t  vehicle_online;
    uint32_t  page_limit_max;

    int       require_token;   /* 1=校验 token 非全零（默认开；0=放行，便于裸协议调试） */
    char      log_file[256];   /* 空 = 仅控制台 */
    int       log_level;
    int       log_console;
    uint64_t  log_max_size;
} biz_cfg_t;

struct biz_ctx
{
    q_reactor_t   *reactor;
    q_tpool_t     *pool;
    q_log_t       *log;
    q_fd_t         listen_fd;
};

/* 每连接上下文：accept 后分配，worker 线程独占（无需加锁） */
typedef struct
{
    q_fd_t         fd;
    struct biz_ctx *g;
} biz_conn_t;

/* ===================== 全局对象 ===================== */
extern biz_cfg_t       g_bcfg;    /* 定义于 main.c */
extern struct biz_ctx  g_biz;     /* 定义于 main.c */

/* ===================== 配置（main.c） ===================== */
void biz_cfg_defaults(void);

/* ===================== 工具（biz_util.c） ===================== */
void     biz_put_be32(uint8_t *p, uint32_t v);
uint32_t biz_get_be32(const uint8_t *p);
int      biz_hex_encode(const uint8_t *b, size_t len, char *out, size_t outcap);
int      biz_hex_decode(const char *hex, uint8_t *out, size_t outcap);
int      biz_set_io_timeout(q_fd_t fd, int sec);
int      biz_send_frame(q_fd_t fd, const uint8_t *buf, size_t len);
int      biz_recv_frame(q_fd_t fd, uint8_t *buf, size_t cap, size_t *outlen);

/* ===================== M3：车辆查询过滤条件 =====================
 * 由 params.biz_param（UTF-8 JSON 文本）解析而来；字段语义见《biz微服务接口约定.md》。
 * 注意：org_id / vehicle_id 在库里是字符串主键（CHAR(36)/CHAR(32)），
 *       **不是**整数 —— 这里一律按字符串比较，不做 atoi。 */
typedef struct
{
    char     org_id[BIZ_ID_MAX];   /* 空串 = 全部；否则按该组织过滤 */
    int      recursive;            /* 1=含所有下级组织（子树，默认）；0=仅本级直属车辆 */
    char     keyword[BIZ_KW_MAX];  /* 车牌/自编号模糊匹配（UTF-8）；空串 = 不过滤 */
    int      online_only;          /* 1=只要在线 */
    int      alarm_only;           /* 1=只保留 alarm_flags & alarm_mask != 0 */
    uint32_t alarm_mask;           /* 报警位掩码；0 时按"任一报警"处理 */
    /* 按主键精确过滤（组织树上勾选若干车辆）；id_count==0 表示不过滤 */
    char     ids[BIZ_IDS_MAX][BIZ_ID_MAX];
    int      id_count;
} biz_veh_filter_t;

/* ===================== 业务处理（biz_handler.c） ===================== */
/* 解析 bizd JSON 信封 -> 生成响应 JSON（调用方 free）；解析失败返回 NULL */
char *biz_handle(struct biz_ctx *g, const char *req_json, size_t req_len);

/* ===================== 桩数据（biz_stub.c） =====================
 * 只生成 [offset, offset+limit) 这一段，total 始终回"过滤后的总行数"
 * （客户端据此算页数）。items 由调用方创建并拥有。 */
int biz_stub_orgs(cJSON *items, const char *parent_id, int recursive,
                  uint32_t offset, uint32_t limit, uint32_t *total_out);
int biz_stub_vehicles(cJSON *items, const biz_veh_filter_t *f,
                      uint32_t offset, uint32_t limit, uint32_t *total_out);

/* ===================== 服务器（biz_srv.c） ===================== */
void biz_on_accept(q_fd_t fd, uint32_t events, void *ctx);
void biz_conn_worker(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* _SRV_BIZ_H_ */
