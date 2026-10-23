/**
 * @file qc_push_ut.c
 * @brief M5 推送层单元测试：wire 字节序 + TLV 回环 + 订阅集合（纯本地，不连网不起线程）
 *
 * 跑法：make -C lib_src/l_4/q_clientcomm ut_push
 */
#include "qc_push.h"
#include "qc_push_wire.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond, ...)                                          \
    do {                                                          \
        if (cond) { g_pass++; }                                   \
        else { g_fail++; printf("  FAIL %s:%d ", __FILE__, __LINE__); \
               printf(__VA_ARGS__); printf("\n"); }               \
    } while (0)

/* ---------- 1. double 大端往返（与主机序无关） ---------- */
static void test_f64_roundtrip(void)
{
    static const double vals[] = {
        0.0, -0.0, 1.0, -1.0, 116.397128, 39.916527,
        180.0, -180.0, 1e-7, 123456.7890123, 3.141592653589793
    };
    size_t i;
    printf("[1] double 大端往返\n");
    for (i = 0; i < sizeof(vals) / sizeof(vals[0]); ++i)
    {
        uint64_t be = qc_push_f64_be(vals[i]);
        double back = qc_push_be_f64(be);
        /* 位型比较：-0.0 与 0.0 值相等但位型不同，这里按值比较即可 */
        CHECK(back == vals[i] || (back != back && vals[i] != vals[i]),
              "f64 roundtrip %g -> %g", vals[i], back);
    }
    /* 大端序断言：最高位字节必须在线格式第 0 字节 */
    {
        uint64_t be = qc_push_f64_be(1.0);   /* 1.0 = 0x3FF0000000000000 */
        uint8_t wire[8];
        size_t k;
        for (k = 0; k < 8u; ++k)
            wire[k] = (uint8_t)(be >> (56 - 8 * (int)k));
        CHECK(wire[0] == 0x3F && wire[1] == 0xF0 && wire[7] == 0x00,
              "1.0 大端线格式首字节=%02X %02X（应 3F F0）", wire[0], wire[1]);
    }
}

/* ---------- 2. SUB_DELTA 组帧（分片边界） ---------- */
static void test_build_delta(void)
{
    uint8_t buf[Q_PUSH_SHARD_MAX * 48u + 64u];
    const char *ids[3];
    size_t blen = 0;
    uint16_t tag;
    const uint8_t *v;
    uint16_t vlen;
    size_t used, off;
    int nids = 0, nop = 0;

    printf("[2] SUB_DELTA 组帧\n");
    ids[0] = "京A12345-1";
    ids[1] = "京B00001-2";
    ids[2] = "";                     /* 空键应被跳过 */
    CHECK(qc_push_build_delta(buf, sizeof(buf), Q_PUSH_OP_ADD, ids, 3, &blen) == 0,
          "build_delta ADD 失败");
    off = 0;
    while (off + 4u <= blen)
    {
        CHECK(q_codec_tlv_decode(buf + off, blen - off, &tag, &v, &vlen, &used) == 0,
              "delta TLV 解析失败 off=%zu", off);
        if (q_codec_tlv_decode(buf + off, blen - off, &tag, &v, &vlen, &used) != 0)
            break;
        off += used;
        if (tag == Q_IPC_T_PUSH_OP)
        {
            nop++;
            CHECK(vlen == 1u && v[0] == Q_PUSH_OP_ADD, "OP 载荷错");
        }
        else if (tag == Q_IPC_T_PUSH_VID)
        {
            nids++;
        }
    }
    CHECK(nop == 1, "应有 1 个 OP，实际 %d", nop);
    CHECK(nids == 2, "应有 2 个 VID（空键被跳过），实际 %d", nids);

    /* op 非法必须拒绝，不发出去 */
    CHECK(qc_push_build_delta(buf, sizeof(buf), 99, ids, 3, &blen) != 0,
          "非法 op 未拒绝");

    /* 满额分片：Q_PUSH_SHARD_MAX 个 ID 必须放得下一帧 */
    {
        static const char *big[Q_PUSH_SHARD_MAX];
        static char storage[Q_PUSH_SHARD_MAX][8];
        uint32_t i;
        size_t blen2 = 0;
        for (i = 0; i < Q_PUSH_SHARD_MAX; ++i)
        {
            snprintf(storage[i], sizeof(storage[i]), "V%05u", (unsigned)i);
            big[i] = storage[i];
        }
        CHECK(qc_push_build_delta(buf, sizeof(buf), Q_PUSH_OP_ADD, big,
                                  Q_PUSH_SHARD_MAX, &blen2) == 0,
              "满额分帧失败");
        CHECK(blen2 < Q_IPC_TLV_MAX, "满额帧超 TLV 上限（%zu）", blen2);
    }
}

/* ---------- 3. TRAJ 载荷构造 + 解析 ---------- */
static size_t build_traj(uint8_t *buf, size_t cap)
{
    size_t off = 0;

    CHECK(qc_push_tlv_put(buf, cap, &off, Q_IPC_T_PUSH_VID, "京A12345-1", (uint16_t)strlen("京A12345-1")) == 0, "put vid");
    CHECK(qc_push_put_f64(buf, cap, &off, Q_IPC_T_PUSH_LON, 116.397128) == 0, "put lon");
    CHECK(qc_push_put_f64(buf, cap, &off, Q_IPC_T_PUSH_LAT, 39.916527) == 0, "put lat");
    CHECK(qc_push_put_u32(buf, cap, &off, Q_IPC_T_PUSH_SPEED, 87u) == 0, "put speed");
    CHECK(qc_push_put_u32(buf, cap, &off, Q_IPC_T_PUSH_ONLINE, 1u) == 0, "put online");
    CHECK(qc_push_put_u32(buf, cap, &off, Q_IPC_T_PUSH_AFLAGS, Q_PUSH_ALARM_OVERSPEED) == 0, "put aflags");
    CHECK(qc_push_tlv_put(buf, cap, &off, Q_IPC_T_PUSH_TV, "2026-10-23 10:30:00", (uint16_t)strlen("2026-10-23 10:30:00")) == 0, "put tv");
    CHECK(qc_push_tlv_put(buf, cap, &off, Q_IPC_T_PUSH_ADDR, "北京市朝阳区", (uint16_t)strlen("北京市朝阳区")) == 0, "put addr");
    return off;
}

static void test_traj(void)
{
    uint8_t buf[1024];
    size_t blen = build_traj(buf, sizeof(buf));
    qc_traj_t t;

    printf("[3] TRAJ 解析\n");
    CHECK(qc_push_parse_traj(buf, blen, &t) == 0, "parse_traj 失败");
    CHECK(strcmp(t.vehicle_id, "京A12345-1") == 0, "vehicle_id=%s", t.vehicle_id);
    CHECK(t.lon > 116.397127 && t.lon < 116.397129, "lon=%.9f", t.lon);
    CHECK(t.lat > 39.916526 && t.lat < 39.916528, "lat=%.9f", t.lat);
    CHECK(t.speed_kph == 87u, "speed=%u", t.speed_kph);
    CHECK(t.online == 1u, "online=%u", t.online);
    CHECK(t.alarm_flags == Q_PUSH_ALARM_OVERSPEED, "aflags=%u", t.alarm_flags);
    CHECK(strcmp(t.time, "2026-10-23 10:30:00") == 0, "time=%s", t.time);
    CHECK(strcmp(t.address, "北京市朝阳区") == 0, "address=%s", t.address);

    /* 缺 vehicle_id 必须判非法（不能推出"不知道是谁"的轨迹） */
    {
        uint8_t bad[64];
        size_t off = 0;
        qc_push_put_u32(bad, sizeof(bad), &off, Q_IPC_T_PUSH_SPEED, 5u);
        CHECK(qc_push_parse_traj(bad, off, &t) != 0, "缺 vid 未判非法");
    }
}

/* ---------- 4. ALARM 载荷构造 + 解析 ---------- */
static void test_alarm(void)
{
    uint8_t buf[1024];
    size_t off = 0, blen;
    qc_alarm_t a;

    printf("[4] ALARM 解析\n");
    CHECK(qc_push_tlv_put(buf, sizeof(buf), &off, Q_IPC_T_PUSH_VID, "京B00001-2", (uint16_t)strlen("京B00001-2")) == 0, "put vid");
    CHECK(qc_push_tlv_put(buf, sizeof(buf), &off, Q_IPC_T_PUSH_ORG, "ORG_ROOT/ORG_A", (uint16_t)strlen("ORG_ROOT/ORG_A")) == 0, "put org");
    CHECK(qc_push_tlv_put(buf, sizeof(buf), &off, Q_IPC_T_PUSH_PLATE, "京B00001", (uint16_t)strlen("京B00001")) == 0, "put plate");
    CHECK(qc_push_put_u32(buf, sizeof(buf), &off, Q_IPC_T_PUSH_ATYPE, Q_PUSH_ALARM_FATIGUE) == 0, "put atype");
    CHECK(qc_push_put_f64(buf, sizeof(buf), &off, Q_IPC_T_PUSH_LON, 116.5) == 0, "put lon");
    CHECK(qc_push_put_f64(buf, sizeof(buf), &off, Q_IPC_T_PUSH_LAT, 39.5) == 0, "put lat");
    CHECK(qc_push_tlv_put(buf, sizeof(buf), &off, Q_IPC_T_PUSH_TV, "2026-10-23 10:31:00", (uint16_t)strlen("2026-10-23 10:31:00")) == 0, "put tv");
    CHECK(qc_push_tlv_put(buf, sizeof(buf), &off, Q_IPC_T_PUSH_ATEXT, "疲劳驾驶报警", (uint16_t)strlen("疲劳驾驶报警")) == 0, "put atext");
    CHECK(qc_push_tlv_put(buf, sizeof(buf), &off, Q_IPC_T_PUSH_ADDR, "海淀区", (uint16_t)strlen("海淀区")) == 0, "put addr");
    blen = off;

    CHECK(qc_push_parse_alarm(buf, blen, &a) == 0, "parse_alarm 失败");
    CHECK(strcmp(a.vehicle_id, "京B00001-2") == 0, "vid=%s", a.vehicle_id);
    CHECK(strcmp(a.org_id, "ORG_ROOT/ORG_A") == 0, "org=%s", a.org_id);
    CHECK(strcmp(a.plate, "京B00001") == 0, "plate=%s", a.plate);
    CHECK(a.alarm_type == Q_PUSH_ALARM_FATIGUE, "atype=%u", a.alarm_type);
    CHECK(a.lon > 116.49 && a.lon < 116.51, "lon=%f", a.lon);
    CHECK(strcmp(a.text, "疲劳驾驶报警") == 0, "text=%s", a.text);
    CHECK(strcmp(a.address, "海淀区") == 0, "address=%s", a.address);
}

/* ---------- 5. 订阅集合：增删去重幂等（不起线程不连网） ---------- */
static void test_sub_set(void)
{
    qc_push_cfg_t cfg;
    qc_push_t *p;
    const char *add1[] = { "京A12345-1", "京B00001-2" };
    const char *add2[] = { "京A12345-1", "京C99999-3" };  /* 重复 + 新 */
    const char *del1[] = { "京B00001-2", "不存在的车-9" };  /* 删 + 幂等删 */

    printf("[5] 订阅集合\n");
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.host, "127.0.0.1");
    cfg.port = 18098;
    strcpy(cfg.user, "tester");
    p = qc_push_create(&cfg);
    CHECK(p != NULL, "qc_push_create 失败");
    if (!p)
        return;

    /* 未连接时 sub_delta 只改集合不发帧（send 路径进不去） */
    CHECK(qc_push_sub_delta(p, Q_PUSH_OP_ADD, add1, 2) == 0, "add1 失败");
    CHECK(qc_push_sub_count(p) == 2u, "count=%u（应 2）", qc_push_sub_count(p));
    CHECK(qc_push_sub_delta(p, Q_PUSH_OP_ADD, add2, 2) == 0, "add2 失败");
    CHECK(qc_push_sub_count(p) == 3u, "count=%u（应 3，去重后）", qc_push_sub_count(p));
    CHECK(qc_push_sub_delta(p, Q_PUSH_OP_REMOVE, del1, 2) == 0, "del1 失败");
    CHECK(qc_push_sub_count(p) == 2u, "count=%u（应 2）", qc_push_sub_count(p));

    /* 非法 op / 空参数 */
    CHECK(qc_push_sub_delta(p, 42, add1, 2) != 0, "非法 op 未拒绝");
    CHECK(qc_push_sub_delta(NULL, Q_PUSH_OP_ADD, add1, 2) != 0, "NULL p 未拒绝");

    /* 超长键本地也不收（防静默截断成半个主键） */
    {
        static char longkey[128];
        const char *bad[] = { longkey };
        memset(longkey, 'A', sizeof(longkey) - 1u);
        longkey[sizeof(longkey) - 1u] = '\0';
        CHECK(qc_push_sub_delta(p, Q_PUSH_OP_ADD, bad, 1) == 0, "超长键调用返回非 0");
        CHECK(qc_push_sub_count(p) == 2u, "超长键被收进集合（count=%u）",
              qc_push_sub_count(p));
    }

    qc_push_destroy(p);
}

int main(void)
{
    printf("=== qc_push UT ===\n");
    test_f64_roundtrip();
    test_build_delta();
    test_traj();
    test_alarm();
    test_sub_set();
    printf("=== 结果：%d 通过，%d 失败 ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
