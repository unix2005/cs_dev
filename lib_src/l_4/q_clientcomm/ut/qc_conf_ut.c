/**
 * @file qc_conf_ut.c
 * @brief l_4 客户端配置层（qc_conf_*）单元测试
 *
 * 用法：make -C lib_src/l_4/q_clientcomm ut_conf
 *
 * 覆盖点（按"哪错了后果最严重"排序）：
 *   1. 默认值正确性 —— 默认值错了不报错，只表现为"这端心跳 60s 那端 5s"
 *   2. XML 取值与类型转换（含 max_retry=0 / auto_start=0 这两个**合法零值**）
 *   3. 明文口令标志（合规红线：配置里出现口令必须留痕）
 *   4. dump 绝不能打印明文口令 —— 诊断信息会被贴进工单，这是典型泄露路径
 *   5. 校验各错误分支
 *   6. 默认路径的环境变量覆盖（三端同名 VMS_CLIENT_CONF）
 */
#include "qc_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef Q_SYS_WINDOWS
#include <process.h>
#define UT_SETENV(k, v) _putenv_s(k, v)
#else
#define UT_SETENV(k, v) setenv(k, v, 1)
#endif

static int g_fail = 0;
static int g_pass = 0;

#define STEP(cond, fmt, ...)                                                       \
    do {                                                                           \
        if (cond) { g_pass++; printf("  [OK  ] " fmt "\n", ##__VA_ARGS__); }       \
        else      { g_fail++; printf("  [FAIL] " fmt "\n", ##__VA_ARGS__); }       \
    } while (0)

#define XML_FULL "qc_conf_ut_full.xml"
#define XML_MIN  "qc_conf_ut_min.xml"
#define XML_BAD  "qc_conf_ut_bad.xml"
#define DUMP_OUT "qc_conf_ut_dump.txt"

static void write_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("  [FAIL] 无法写 %s\n", path); exit(1); }
    fwrite(content, 1, strlen(content), f);
    fclose(f);
}

static const char *kFullXml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<vms_client>\n"
    "  <server>\n"
    "    <host>192.168.19.22</host>\n"
    "    <port>18099</port>\n"
    "    <pubkey_path>/tmp/vmsconf/netd_sm2.pub</pubkey_path>\n"
    "    <seq0>1</seq0>\n"
    "    <key_id>1</key_id>\n"
    "  </server>\n"
    "  <auth>\n"
    "    <user>qiaoshui</user>\n"
    "    <password>123456</password>\n"
    "    <otp>654321</otp>\n"
    "    <devid_hex>0011223344556677</devid_hex>\n"
    "    <pbkdf2_iter>120000</pbkdf2_iter>\n"
    "  </auth>\n"
    "  <knock>\n"
    "    <endpoint>192.168.19.22:5001</endpoint>\n"
    "    <spa_key_path>/tmp/vmsconf/spa.key</spa_key_path>\n"
    "    <req_port>18099</req_port>\n"
    "    <req_ttl>300</req_ttl>\n"
    "    <key_id>1</key_id>\n"
    "  </knock>\n"
    "  <keeper>\n"
    "    <heartbeat_ms>3000</heartbeat_ms>\n"
    "    <backoff_base_ms>500</backoff_base_ms>\n"
    "    <backoff_max_ms>10000</backoff_max_ms>\n"
    "    <max_retry>0</max_retry>\n"
    "    <auto_start>0</auto_start>\n"
    "  </keeper>\n"
    "  <app>\n"
    "    <client_version>1.2.0</client_version>\n"
    "    <log_level>debug</log_level>\n"
    "    <connect_timeout_ms>8000</connect_timeout_ms>\n"
    "  </app>\n"
    "</vms_client>\n";

static const char *kMinXml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<vms_client>\n"
    "  <server><host>127.0.0.1</host><pubkey_path>/tmp/netd_sm2.pub</pubkey_path></server>\n"
    "  <auth><user>qiaoshui</user></auth>\n"
    "</vms_client>\n";

int main(void)
{
    qc_client_conf_t c;
    char err[256];

    printf("== qc_conf 单元测试 ==\n\n");

    /* ---------- 1. 默认值 ---------- */
    printf("[1] 默认值\n");
    memset(&c, 0xAA, sizeof(c));
    STEP(qc_conf_default(&c) == 0, "qc_conf_default 返回 0");
    STEP(c.server.port == 18099u, "默认 port=18099（实际 %u）", (unsigned)c.server.port);
    STEP(c.server.seq0 == 1u, "默认 seq0=1");
    STEP(c.server.key_id == 1u, "默认 key_id=1");
    STEP(c.keeper.heartbeat_ms == 60000u, "默认心跳 60000ms（实际 %u）",
         (unsigned)c.keeper.heartbeat_ms);
    STEP(c.keeper.backoff_base_ms == 1000u, "默认退避基数 1000ms");
    STEP(c.keeper.backoff_max_ms == 30000u, "默认退避上限 30000ms");
    STEP(c.keeper.max_retry == 0u, "默认 max_retry=0（无限重试）");
    STEP(c.keeper.auto_start == 1u, "默认自动起 keeper");
    STEP(c.auth.pbkdf2_iter == 0u, "默认 pbkdf2_iter=0（取协议默认）");
    STEP(strcmp(c.app.client_version, "1.1.0") == 0, "默认客户端版本 1.1.0");
    STEP(c.auth.password[0] == '\0', "默认口令为空");

    /* ---------- 2. 全量配置加载 ---------- */
    printf("\n[2] 全量 XML 加载\n");
    write_file(XML_FULL, kFullXml);
    memset(&c, 0, sizeof(c));
    STEP(qc_conf_load(XML_FULL, &c) == 0, "加载成功");
    STEP(strcmp(c.server.host, "192.168.19.22") == 0, "host=%s", c.server.host);
    STEP(c.server.port == 18099u, "port=%u", (unsigned)c.server.port);
    STEP(strcmp(c.server.pubkey_path, "/tmp/vmsconf/netd_sm2.pub") == 0, "pubkey 路径正确");
    STEP(strcmp(c.auth.user, "qiaoshui") == 0, "user=%s", c.auth.user);
    STEP(strcmp(c.auth.password, "123456") == 0, "读到口令（联调通道）");
    STEP(strcmp(c.auth.otp, "654321") == 0, "otp 正确");
    STEP(strcmp(c.auth.devid_hex, "0011223344556677") == 0, "devid_hex 正确");
    STEP(c.auth.pbkdf2_iter == 120000u, "pbkdf2_iter=%u", (unsigned)c.auth.pbkdf2_iter);
    STEP(strcmp(c.knock.endpoint, "192.168.19.22:5001") == 0, "knock=%s", c.knock.endpoint);
    STEP(strcmp(c.knock.spa_key_path, "/tmp/vmsconf/spa.key") == 0, "spa_key 路径正确");
    STEP(c.knock.req_ttl == 300u, "req_ttl=%u", (unsigned)c.knock.req_ttl);
    STEP(c.keeper.heartbeat_ms == 3000u, "心跳 3000ms（实际 %u）",
         (unsigned)c.keeper.heartbeat_ms);
    STEP(c.keeper.backoff_base_ms == 500u, "退避基数 500ms");
    STEP(c.keeper.backoff_max_ms == 10000u, "退避上限 10000ms");

    /* 合法零值：max_retry=0 与 auto_start=0 必须被尊重，不能"兜"成默认 1 */
    STEP(c.keeper.max_retry == 0u, "max_retry=0 被尊重（无限重试，不是兜默认）");
    STEP(c.keeper.auto_start == 0u, "auto_start=0 被尊重（显式关闭，不是兜默认 1）");

    STEP(strcmp(c.app.client_version, "1.2.0") == 0, "cver=%s", c.app.client_version);
    STEP(strcmp(c.app.log_level, "debug") == 0, "log_level=%s", c.app.log_level);
    STEP(c.app.connect_timeout_ms == 8000u, "连接超时 8000ms");
    STEP((c.flags & QC_CONF_F_PLAIN_PASSWORD) != 0u, "明文口令已置标志（合规留痕）");
    STEP((c.flags & QC_CONF_F_DEFAULTED) == 0u, "关键项齐全，未走默认");
    STEP(strcmp(c.source_path, XML_FULL) == 0, "source_path 回填");

    /* ---------- 3. 最小化配置（缺失项走默认） ---------- */
    printf("\n[3] 最小化 XML（缺失即默认）\n");
    write_file(XML_MIN, kMinXml);
    memset(&c, 0, sizeof(c));
    STEP(qc_conf_load(XML_MIN, &c) == 0, "加载成功");
    STEP(strcmp(c.server.host, "127.0.0.1") == 0, "host 读到");
    STEP(c.server.port == 18099u, "port 缺失 -> 默认 18099");
    STEP(c.keeper.heartbeat_ms == 60000u, "心跳缺失 -> 默认 60000ms");
    STEP(c.auth.password[0] == '\0', "口令缺失 -> 空（不置标志）");
    STEP((c.flags & QC_CONF_F_PLAIN_PASSWORD) == 0u, "无明文口令标志");
    STEP((c.flags & QC_CONF_F_DEFAULTED) != 0u, "port 走了默认 -> 置 DEFAULTED 提示");

    /* ---------- 4. 异常路径 ---------- */
    printf("\n[4] 异常路径\n");
    memset(&c, 0, sizeof(c));
    STEP(qc_conf_load("__not_exist__.xml", &c) == -2, "文件不存在 -> -2");
    write_file(XML_BAD, "<vms_client><server><host>");
    STEP(qc_conf_load(XML_BAD, &c) == -3, "XML 语法错 -> -3");
    STEP(qc_conf_load(XML_FULL, NULL) == -1, "out 为 NULL -> -1");

    /* ---------- 5. 校验 ---------- */
    printf("\n[5] 校验分支\n");
    qc_conf_load(XML_FULL, &c);
    STEP(qc_conf_validate(&c, err, sizeof(err)) == 0, "全量配置合法（%s）", err);

    qc_conf_load(XML_FULL, &c);
    c.server.host[0] = '\0';
    STEP(qc_conf_validate(&c, err, sizeof(err)) == 1, "缺 host -> 1（%s）", err);

    qc_conf_load(XML_FULL, &c);
    c.server.pubkey_path[0] = '\0';
    STEP(qc_conf_validate(&c, err, sizeof(err)) == 3, "缺 pubkey -> 3（%s）", err);

    qc_conf_load(XML_FULL, &c);
    c.auth.pbkdf2_iter = 1000u;
    STEP(qc_conf_validate(&c, err, sizeof(err)) == 4, "pbkdf2_iter 过低 -> 4（%s）", err);

    qc_conf_load(XML_FULL, &c);
    c.knock.spa_key_path[0] = '\0';
    STEP(qc_conf_validate(&c, err, sizeof(err)) == 5, "knock 缺密钥 -> 5（%s）", err);

    STEP(qc_conf_validate(NULL, err, sizeof(err)) == -1, "NULL -> -1");
    /* 错误信息里绝不能带口令 */
    STEP(strstr(err, "123456") == NULL, "错误信息不含口令");

    /* ---------- 6. dump 脱敏（安全断言） ---------- */
    printf("\n[6] dump 脱敏\n");
    qc_conf_load(XML_FULL, &c);
    {
        FILE *f = fopen(DUMP_OUT, "wb");
        STEP(f != NULL, "打开 dump 输出文件");
        if (f)
        {
            qc_conf_dump(&c, f);
            fclose(f);
        }
        f = fopen(DUMP_OUT, "rb");
        if (f)
        {
            char buf[4096];
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            buf[n] = '\0';
            fclose(f);
            STEP(strstr(buf, "123456") == NULL, "dump 输出不含明文口令");
            STEP(strstr(buf, "654321") == NULL, "dump 输出不含 OTP");
            STEP(strstr(buf, "****") != NULL, "dump 用 **** 遮蔽敏感项");
            STEP(strstr(buf, "192.168.19.22") != NULL, "dump 含 host（排障需要）");
        }
    }

    /* ---------- 7. 默认路径 ---------- */
    printf("\n[7] 默认路径\n");
    {
        char p[QC_PATH_MAX];
        UT_SETENV("VMS_CLIENT_CONF", "/custom/path/vms.xml");
        STEP(qc_conf_default_path(p, sizeof(p)) == 0, "取默认路径成功");
        STEP(strcmp(p, "/custom/path/vms.xml") == 0, "环境变量覆盖生效：%s", p);
        UT_SETENV("VMS_CLIENT_CONF", "");
        STEP(qc_conf_default_path(p, sizeof(p)) == 0, "清空环境变量后仍返回 0");
        STEP(strlen(p) > 0, "回退平台默认路径：%s", p);
        STEP(qc_conf_default_path(NULL, 0) != 0, "out 为 NULL 且无环境变量 -> 非 0");
    }

    /* ---------- 8. 敏感清零 ---------- */
    printf("\n[8] 敏感清零\n");
    qc_conf_load(XML_FULL, &c);
    qc_conf_free(&c);
    STEP(c.auth.password[0] == '\0', "free 后口令已清零");
    STEP(c.server.host[0] == '\0', "free 后整体归零");

    remove(XML_FULL);
    remove(XML_MIN);
    remove(XML_BAD);
    remove(DUMP_OUT);

    printf("\n== 结果：%d 通过 / %d 失败 ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
