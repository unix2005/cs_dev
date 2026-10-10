/**
 * @file q_chan.h
 * @brief l_3 协议层 安全通道 + 协议语义库（libqchan）对外头文件
 * @layer  lib_src/l_3/q_chan  ->  产出 libqchan
 * @note   职责（AI_SKILL_RULE §3.1 l_3，已合并原 l_3/q_proto）：
 *         【安全通道】SM2 ECDH 密钥协商（服务端静态公钥认证 + 前向安全）、
 *           SM4-GCM 收发（IV = nonce_base XOR seq；AAD = 包头全部字段）、通道绑定（SM3(session_key)）；
 *         【协议状态机】连接态迁移 NEW→KEYED→AUTHED 与未认证态指令白名单；
 *         【指令分发】cmd→handler 注册表（守护进程注册业务处理）；
 *         【会话/Token】32 字节随机 Token，绑定 channel_id+user_id+device_id+src_ip。
 *         复用 l_1/q_crypto（SM2/SM4-GCM/SM3）、l_1/q_sec（会话密钥派生 / seq 滑动窗口 / PoW）、
 *         l_1/q_rand（随机数）、l_2/q_codec（pkt_hdr / TLV 编解码 / 字段校验）、
 *         l_2/q_net（传输阻塞收发）。
 *         注：TLV 编解码位于 l_2/q_codec（全局唯一实现）；本库只做协议语义与状态。
 */
#ifndef L_3_Q_CHAN_H
#define L_3_Q_CHAN_H

#include "include_stdio.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define Q_CHAN_MODE_CLIENT 0
#define Q_CHAN_MODE_SERVER 1

    typedef struct q_chan q_chan_t;

    /* 创建安全通道。
   mode        : Q_CHAN_MODE_CLIENT / Q_CHAN_MODE_SERVER
   local_priv/local_pub : 本端 SM2 密钥对 DER。
                服务端必须提供静态密钥对（用于身份认证）；客户端可传 NULL（握手时生成临时密钥）。
   peer_pub    : 对端静态公钥 DER（客户端侧必填，用于校验服务端身份；服务端传 NULL）。
   peer_len    : peer_pub 长度（为 0 表示 NULL）。
   seq0        : 协商用固定序列号（双方必须一致，仅用于会话密钥派生，不影响消息 seq 计数器）。
   key_id      : 写入包头的密钥标识。
   成功返回通道句柄，失败返回 NULL。 */
    q_chan_t *q_chan_create(int mode, const uint8_t *local_priv, size_t local_priv_len, const uint8_t *local_pub,
                            size_t local_pub_len, const uint8_t *peer_pub, size_t peer_len, uint64_t seq0,
                            uint16_t key_id);

    void q_chan_destroy(q_chan_t *c);

    /* 执行 SM2 ECDH 握手（阻塞，经 fd 收发协商材料）。成功 0，失败 -1。
   握手后通道进入可收发状态，并生成会话密钥与通道绑定材料。 */
    int q_chan_handshake(q_chan_t *c, int fd);

    /* 加密编码：plain(plen) -> out（包头 + SM4-GCM 密文 + tag）。
   outcap 需 >= 44 + plen + 16；outlen 返回编码后字节数。成功 0，失败 -1。 */
    int q_chan_send(q_chan_t *c, const uint8_t *plain, size_t plen, uint16_t cmd, uint8_t *out, size_t outcap,
                    size_t *outlen);

    /* 解密校验：pkt(pktlen) -> plain。校验包头/字段/AAD/tag/seq 窗口/时间窗。
   plaincap 需 >= body_len；plainlen 返回明文长度；cmd_out 可选返回命令字。成功 0，失败 -1。 */
    int q_chan_recv(q_chan_t *c, const uint8_t *pkt, size_t pktlen, uint8_t *plain, size_t plaincap, size_t *plainlen,
                    uint16_t *cmd_out);

    /* 便捷传输：经 fd 阻塞发送（内部 q_chan_send + q_net 阻塞写）。成功 0，失败 -1。 */
    int q_chan_transmit(q_chan_t *c, int fd, const uint8_t *plain, size_t plen, uint16_t cmd);

    /* 便捷接收：经 fd 阻塞读取完整数据包并解密（内部 q_net 阻塞读 + q_chan_recv）。成功 0，失败 -1。 */
    int q_chan_receive(q_chan_t *c, int fd, uint8_t *plain, size_t plaincap, size_t *plainlen, uint16_t *cmd_out);

    /* 通道绑定：SM3(session_key) -> out(32)。未握手返回 -1。
   用于上层将 Token / 会话绑定到本通道，防止跨通道伪造。 */
    int q_chan_binding(q_chan_t *c, uint8_t out[32]);

/* ============================================================
 *  以下为合并原 l_3/q_proto 的协议语义（状态机 / 指令分发 / 会话Token）
 * ============================================================ */

/* ---------- 指令字（对齐技术方案 V2 §5.3） ---------- */
#define Q_CHAN_CMD_HELLO 0x0001u
#define Q_CHAN_CMD_KEY_EXCHANGE 0x0002u
#define Q_CHAN_CMD_LOGIN 0x0003u
#define Q_CHAN_CMD_HEARTBEAT 0x0010u
#define Q_CHAN_CMD_BIZ_QUERY 0x0020u
#define Q_CHAN_CMD_BIZ_UPDATE 0x0021u
#define Q_CHAN_CMD_VIDEO_OPEN 0x0030u
#define Q_CHAN_CMD_LOGOUT 0x0040u

    /* ---------- 连接状态机 ---------- */
    typedef enum
    {
        Q_CHAN_ST_NEW = 0, /* 通道已建，尚未完成密钥交换 */
        Q_CHAN_ST_HELLO,   /* 已收到 HELLO（能力协商） */
        Q_CHAN_ST_KEYED,   /* 密钥交换完成（q_chan_handshake 成功后由调用方置此态） */
        Q_CHAN_ST_AUTHED,  /* 登录校验通过 */
        Q_CHAN_ST_CLOSED   /* 已关闭 / 应断连 */
    } q_chan_st_t;

    typedef enum
    {
        Q_CHAN_EV_HANDSHAKE_OK = 0, /* q_chan_handshake 成功 */
        Q_CHAN_EV_HELLO,            /* 收到 HELLO */
        Q_CHAN_EV_LOGIN_OK,         /* 登录校验通过 */
        Q_CHAN_EV_LOGIN_FAIL,       /* 登录校验失败 */
        Q_CHAN_EV_AUTH_CMD,         /* 已认证态业务命令 */
        Q_CHAN_EV_TIMEOUT           /* 超时 / 错误 */
    } q_chan_ev_t;

    /* 处理事件并推进状态：合法迁移返回 0 并写回新状态；非法迁移返回 -1（调用方应断连）。 */
    int q_chan_sm_transition(q_chan_st_t *st, q_chan_ev_t ev);

    /* 白名单：当前状态下是否允许收到该 cmd；允许返回 1，禁止返回 0。
   未认证态（NEW/HELLO/KEYED）仅允许 HELLO/LOGIN（KEY_EXCHANGE 由 q_chan_handshake 以
   带外方式完成，不占用应用层指令白名单）；AUTHED 仅允许业务指令。 */
    int q_chan_sm_cmd_allowed(q_chan_st_t st, uint16_t cmd);

    /* 收包一站式处理：先查白名单，再按 cmd 推进状态（LOGIN 用 login_ok 决定成败）。
   返回 0 表示状态合法可继续；返回 -1 表示应断连。 */
    int q_chan_sm_on_cmd(q_chan_st_t *st, uint16_t cmd, int login_ok);

    /* ---------- 指令分发（守护进程注册业务处理） ---------- */
    typedef int (*q_chan_handler_t)(uint16_t cmd, const uint8_t *body, size_t body_len, uint8_t *out, size_t outcap,
                                    size_t *out_len, void *ud);

#define Q_CHAN_DISP_MAX 32 /* 固定槽位，避免动态分配失控 */
    struct q_chan_disp
    {
        uint16_t cmd[Q_CHAN_DISP_MAX];
        q_chan_handler_t fn[Q_CHAN_DISP_MAX];
        void *ud[Q_CHAN_DISP_MAX];
        int n;
    };

    typedef struct q_chan_disp q_chan_disp_t;
    q_chan_disp_t *q_chan_disp_create(void);
    void q_chan_disp_destroy(q_chan_disp_t *d);
    /* 注册 cmd→handler（满表返回 -1；重复 cmd 覆盖旧值）。 */
    int q_chan_disp_register(q_chan_disp_t *d, uint16_t cmd, q_chan_handler_t fn, void *ud);
    /* 调用已注册 handler；未注册返回 -1（未知命令），handler 返回非 0 亦返回 -1。 */
    int q_chan_disp_invoke(q_chan_disp_t *d, uint16_t cmd, const uint8_t *body, size_t body_len, uint8_t *out,
                           size_t outcap, size_t *out_len);

/* ---------- 会话 / Token（技术方案 V2 §6.3） ---------- */
#define Q_CHAN_TOKEN_LEN 32  /* 密码学随机 Token 长度 */
#define Q_CHAN_CHANID_LEN 32 /* channel_id = SM3(session_key) */
#define Q_CHAN_USERID_LEN 32 /* 定长 user_id（SM3(用户名)，与 SPA 一致） */
#define Q_CHAN_DEVID_LEN 32  /* 定长 device_id */

    typedef struct
    {
        uint8_t token[Q_CHAN_TOKEN_LEN];
        uint8_t channel_id[Q_CHAN_CHANID_LEN];
        uint8_t user_id[Q_CHAN_USERID_LEN];
        uint8_t device_id[Q_CHAN_DEVID_LEN];
        uint32_t src_ip;      /* 网络序 IPv4 */
        uint64_t issue_time;  /* 秒 */
        uint64_t expire_time; /* 秒 */
    } q_chan_token_t;

    /* 签发 Token：token 取 32 字节随机，绑定传入的 channel_id/user_id/device_id/src_ip，
   有效期 ttl_sec（默认 1800）。成功 0，失败 -1（需先经 q_chan_binding 取得 channel_id）。 */
    int q_chan_token_issue(const uint8_t channel_id[Q_CHAN_CHANID_LEN], const uint8_t user_id[Q_CHAN_USERID_LEN],
                           const uint8_t device_id[Q_CHAN_DEVID_LEN], uint32_t src_ip, uint64_t ttl_sec,
                           q_chan_token_t *out);

    /* 校验 Token：比对绑定字段 + 有效期（now_sec）。全部通过返回 0，否则 -1。 */
    int q_chan_token_validate(const q_chan_token_t *t, const uint8_t channel_id[Q_CHAN_CHANID_LEN],
                              const uint8_t user_id[Q_CHAN_USERID_LEN], const uint8_t device_id[Q_CHAN_DEVID_LEN],
                              uint32_t src_ip, uint64_t now_sec);

#ifdef __cplusplus
}
#endif

#endif /* L_3_Q_CHAN_H */
