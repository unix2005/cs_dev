/**
 * @file q_chan_token_issue.c
 * @brief 签发会话 Token（32 字节随机 + 绑定字段）
 */
#include "headers.h"
#include "q_chan.h"
#include "q_rand.h"

int q_chan_token_issue(const uint8_t channel_id[Q_CHAN_CHANID_LEN], const uint8_t user_id[Q_CHAN_USERID_LEN],
                       const uint8_t device_id[Q_CHAN_DEVID_LEN], uint32_t src_ip, uint64_t ttl_sec,
                       q_chan_token_t *out)
{
    if (!channel_id || !user_id || !device_id || !out)
        return -1;

    if (q_rand_bytes(out->token, Q_CHAN_TOKEN_LEN) != 0)
        return -1;

    memcpy(out->channel_id, channel_id, Q_CHAN_CHANID_LEN);
    memcpy(out->user_id, user_id, Q_CHAN_USERID_LEN);
    memcpy(out->device_id, device_id, Q_CHAN_DEVID_LEN);
    out->src_ip = src_ip;
    out->issue_time = (uint64_t)time(NULL);
    out->expire_time = out->issue_time + ttl_sec;
    return 0;
}
