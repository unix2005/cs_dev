/**
 * @file q_chan_token_validate.c
 * @brief 校验会话 Token（绑定字段 + 有效期）
 */
#include "headers.h"
#include "q_chan.h"

int q_chan_token_validate(const q_chan_token_t *t, const uint8_t channel_id[Q_CHAN_CHANID_LEN],
                          const uint8_t user_id[Q_CHAN_USERID_LEN], const uint8_t device_id[Q_CHAN_DEVID_LEN],
                          uint32_t src_ip, uint64_t now_sec)
{
    if (!t || !channel_id || !user_id || !device_id)
        return -1;
    if (now_sec >= t->expire_time)
        return -1;
    if (t->src_ip != src_ip)
        return -1;
    if (memcmp(t->channel_id, channel_id, Q_CHAN_CHANID_LEN) != 0)
        return -1;
    if (memcmp(t->user_id, user_id, Q_CHAN_USERID_LEN) != 0)
        return -1;
    if (memcmp(t->device_id, device_id, Q_CHAN_DEVID_LEN) != 0)
        return -1;
    return 0;
}
