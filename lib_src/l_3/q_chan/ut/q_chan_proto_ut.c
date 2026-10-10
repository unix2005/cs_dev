/**
 * @file q_chan_proto_ut.c
 * @brief q_chan 协议语义单元测试：连接状态机 / 指令分发 / 会话Token
 *        纯逻辑，无需 Tongsuo；Linux 下 make ut 编译并运行。
 */
#include "headers.h"
#include "q_chan.h"
#include <string.h>

/* ---- 连接状态机 ---- */
static int test_sm(void)
{
    q_chan_st_t st = Q_CHAN_ST_NEW;

    /* 未认证态白名单：仅 HELLO 允许 */
    if (q_chan_sm_cmd_allowed(st, Q_CHAN_CMD_HELLO) != 1) return -1;
    if (q_chan_sm_cmd_allowed(st, Q_CHAN_CMD_LOGIN) != 0) return -1;
    if (q_chan_sm_cmd_allowed(st, Q_CHAN_CMD_BIZ_QUERY) != 0) return -1;

    /* 握手完成 -> KEYED */
    if (q_chan_sm_transition(&st, Q_CHAN_EV_HANDSHAKE_OK) != 0) return -1;
    if (st != Q_CHAN_ST_KEYED) return -1;

    /* KEYED 允许 LOGIN/HELLO，禁止业务指令 */
    if (q_chan_sm_cmd_allowed(st, Q_CHAN_CMD_LOGIN) != 1) return -1;
    if (q_chan_sm_cmd_allowed(st, Q_CHAN_CMD_HEARTBEAT) != 0) return -1;

    /* 收 LOGIN(成功) -> AUTHED */
    if (q_chan_sm_on_cmd(&st, Q_CHAN_CMD_LOGIN, 1) != 0) return -1;
    if (st != Q_CHAN_ST_AUTHED) return -1;

    /* AUTHED 允许业务指令，禁止 LOGIN */
    if (q_chan_sm_cmd_allowed(st, Q_CHAN_CMD_BIZ_QUERY) != 1) return -1;
    if (q_chan_sm_cmd_allowed(st, Q_CHAN_CMD_LOGIN) != 0) return -1;
    if (q_chan_sm_on_cmd(&st, Q_CHAN_CMD_HEARTBEAT, 0) != 0) return -1;
    if (st != Q_CHAN_ST_AUTHED) return -1;

    /* 非法：NEW 收 LOGIN -> 断连 */
    q_chan_st_t st2 = Q_CHAN_ST_NEW;
    if (q_chan_sm_on_cmd(&st2, Q_CHAN_CMD_LOGIN, 1) != -1) return -1;

    /* LOGIN 失败 -> CLOSED */
    q_chan_st_t st3 = Q_CHAN_ST_KEYED;
    if (q_chan_sm_on_cmd(&st3, Q_CHAN_CMD_LOGIN, 0) != 0) return -1;
    if (st3 != Q_CHAN_ST_CLOSED) return -1;

    return 0;
}

/* ---- 指令分发 ---- */
static int echo_handler(uint16_t cmd, const uint8_t *body, size_t body_len,
                        uint8_t *out, size_t outcap, size_t *out_len, void *ud)
{
    (void)cmd; (void)ud;
    if (body_len > outcap) return -1;
    memcpy(out, body, body_len);
    *out_len = body_len;
    return 0;
}

static int test_disp(void)
{
    q_chan_disp_t *d = q_chan_disp_create();
    if (!d) return -1;
    if (q_chan_disp_register(d, Q_CHAN_CMD_HELLO, echo_handler, NULL) != 0) {
        q_chan_disp_destroy(d); return -1;
    }

    uint8_t in[] = {1, 2, 3, 4};
    uint8_t out[16]; size_t out_len = 0;
    if (q_chan_disp_invoke(d, Q_CHAN_CMD_HELLO, in, sizeof(in), out, sizeof(out), &out_len) != 0) {
        q_chan_disp_destroy(d); return -1;
    }
    if (out_len != 4 || memcmp(out, in, 4) != 0) {
        q_chan_disp_destroy(d); return -1;
    }

    /* 未知命令 -> -1 */
    if (q_chan_disp_invoke(d, Q_CHAN_CMD_LOGOUT, in, sizeof(in), out, sizeof(out), &out_len) != -1) {
        q_chan_disp_destroy(d); return -1;
    }

    q_chan_disp_destroy(d);
    return 0;
}

/* ---- 会话 Token ---- */
static int test_token(void)
{
    uint8_t cid[Q_CHAN_CHANID_LEN] = {0}; cid[0] = 0xAA;
    uint8_t uid[Q_CHAN_USERID_LEN] = {0}; uid[0] = 0xBB;
    uint8_t did[Q_CHAN_DEVID_LEN] = {0}; did[0] = 0xCC;
    uint32_t ip = 0x7F000001u; /* 127.0.0.1 */

    q_chan_token_t t;
    if (q_chan_token_issue(cid, uid, did, ip, 1800, &t) != 0) return -1;

    uint64_t now = (uint64_t)time(NULL);
    if (q_chan_token_validate(&t, cid, uid, did, ip, now) != 0) return -1;

    /* IP 不符 -> 失败 */
    if (q_chan_token_validate(&t, cid, uid, did, 0x01020304u, now) != -1) return -1;
    /* channel_id 不符 -> 失败 */
    uint8_t bad[Q_CHAN_CHANID_LEN] = {0};
    if (q_chan_token_validate(&t, bad, uid, did, ip, now) != -1) return -1;
    /* 过期 -> 失败 */
    if (q_chan_token_validate(&t, cid, uid, did, ip, now + 2000) != -1) return -1;

    return 0;
}

int main(void)
{
    if (test_sm() != 0)    { printf("FAIL: state machine\n"); return 1; }
    if (test_disp() != 0)  { printf("FAIL: dispatch\n"); return 1; }
    if (test_token() != 0) { printf("FAIL: token\n"); return 1; }
    printf("q_chan_proto_ut: PASS\n");
    return 0;
}
