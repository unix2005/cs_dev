#ifndef L_3_Q_CHAN_INT_H
#define L_3_Q_CHAN_INT_H

#include "headers.h"
#include "q_chan.h"
#include "q_crypto.h"
#include "q_sec.h"
#include "q_codec.h"
#include "q_net.h"
#include "q_rand.h"
#include "q_mem.h"
#include "q_log.h"

/* 会话密钥长度（q_sec_session_key 输出 32 字节） */
#define Q_CHAN_KEY_LEN        32
/* SM4 使用 128 位密钥（取会话密钥前 16 字节） */
#define Q_CHAN_SM4_KEY_LEN    16
#define Q_CHAN_IV_LEN         12
#define Q_CHAN_TAG_LEN        16
#define Q_CHAN_NONCE_LEN      12
/* 协商 KDF 固定 seq（双方一致即可，仅用于派生会话密钥） */
#define Q_CHAN_HANDSHAKE_SEQ   1
/* 接收 seq 滑动窗口跨度 */
#define Q_CHAN_SEQ_SPAN       64
/* 时间戳粗筛窗口（秒） */
#define Q_CHAN_TS_WINDOW      300

struct q_chan {
    int                mode;
    uint8_t           *local_priv;       /* 服务端静态私钥 DER（客户端可为空） */
    size_t             local_priv_len;
    uint8_t           *local_pub;        /* 服务端静态公钥 DER（客户端可为空） */
    size_t             local_pub_len;
    uint8_t           *peer_pub_pinned;  /* 客户端：预期服务端静态公钥，用于身份认证 */
    size_t             peer_pub_pinned_len;
    uint8_t            session_key[Q_CHAN_KEY_LEN];
    uint8_t            nonce_base[Q_CHAN_NONCE_LEN];  /* 本端发送方向 nonce 基 */
    uint16_t           key_id;
    uint64_t           seq0;
    uint64_t           seq_send;         /* 下一个发送 seq */
    q_sec_seq_window_t *win;            /* 接收 seq 滑动窗口 */
    int                established;
};

/* IV = nonce_base XOR (seq 零扩展至 12 字节，低 8 字节承载 seq 大端) */
void q_chan_iv_derive(const uint8_t *nonce_base, uint64_t seq, uint8_t iv[Q_CHAN_IV_LEN]);

#endif /* L_3_Q_CHAN_INT_H */
