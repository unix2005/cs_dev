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

#ifdef __cplusplus
}
#endif

#endif /* QC_CLIENT_H */
