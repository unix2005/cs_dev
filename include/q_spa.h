/**
 * @file q_spa.h
 * @brief l_3 协议层 SPA 单包授权库（libqspa）对外头文件
 * @layer  lib_src/l_3/q_spa  ->  产出 libqspa
 * @note   职责（技术方案 V2 §3）：单包授权报文构造 / 校验，给 knockd 与客户端使用。
 *         仅依赖 l_1（q_crypto 国密 / q_rand 随机），不依赖 l_2 / l_3（避免同层耦合）。
 *         报文定长、无响应（knockd 静默失败），天然抗 UDP 反射放大。
 */
#ifndef L_3_Q_SPA_H
#define L_3_Q_SPA_H

#include "include_stdio.h"

#define Q_SPA_MAGIC 0x53504101u /* "SPA\1" */
#define Q_SPA_VERSION 1
#define Q_SPA_MSG_OPEN 1
#define Q_SPA_USERID_LEN 32
#define Q_SPA_NONCE_LEN 12
#define Q_SPA_TAG_LEN 16
#define Q_SPA_CIPHER_LEN 32 /* client_ip_hint(16) || rnd(16) */
#define Q_SPA_MAX_LEN 256
#define Q_SPA_TS_WINDOW 30 /* 时间戳 ±30s */

/* SPA 报文（定长结构，全部网络字节序；总长 = 60 + 32 + 16 = 108 ≤ 256） */
typedef struct __attribute__((packed))
{
    uint8_t magic[4];
    uint8_t version;
    uint8_t key_id;   /* SPA 密钥版本，支持轮换 */
    uint8_t msg_type; /* 1 = OPEN */
    uint8_t reserved;
    uint32_t timestamp;  /* 秒，网络序 */
    uint8_t nonce[12];   /* GCM IV */
    uint8_t user_id[32]; /* 定长，SM3(用户名) 或固定编号 */
    uint16_t req_port;   /* 网络序 */
    uint16_t req_ttl;    /* 网络序 */
    uint8_t cipher[32];  /* SM4-GCM{ client_ip_hint(16) || rnd(16) } */
    uint8_t tag[16];
} q_spa_pkt_t;

/* 客户端构造 SPA 包：以 per-user spa_key 加密 {ip_hint(16) || rnd(16)}。
   成功 0，失败 -1。out 需 >= sizeof(q_spa_pkt_t)。 */
int q_spa_build(const uint8_t *spa_key, size_t key_len, uint8_t key_id, const uint8_t user_id[Q_SPA_USERID_LEN],
                uint16_t req_port, uint16_t req_ttl, const uint8_t ip_hint[16], q_spa_pkt_t *out);

/* knockd 校验 SPA 包：验魔数/版本/msg_type、时间戳 ±30s、SM4-GCM tag（AAD = 报文头 60 字节）。
   成功 0 并写回 out_req_port / out_req_ttl（主机序）；失败 -1（调用方须静默丢弃、不响应）。
   ip_hint 不从此处取用（knockd 应采用 recvfrom 实际源 IP，防 IP 欺骗）。 */
int q_spa_verify(const uint8_t *spa_key, size_t key_len, const q_spa_pkt_t *pkt, uint64_t now_sec,
                 uint16_t *out_req_port, uint16_t *out_req_ttl);

#endif /* L_3_Q_SPA_H */
