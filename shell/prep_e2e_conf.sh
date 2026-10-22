#!/usr/bin/env bash
# ==============================================================================
#  端到端联调环境准备脚本（shell/prep_e2e_conf.sh）
#
#  用途：在**任意一台新机器**上生成 e2e_m3/m4.sh 所需的全部配置与密钥，
#        并可按需创建 knockd 依赖的 nftables 表 / 集合 / 放行规则。
#
#  背景：这套东西原先是手工在 WSL 的 /tmp/vmsconf 里准备的，换一台机器就要
#        重新手工来一遍（SM2 密钥对、SPA 密钥、三个 xml、nftables 前置），
#        任一处路径写错都表现为"服务起不来"，排查成本很高。
#
#  用法：
#     bash shell/prep_e2e_conf.sh                 # 只生成配置与密钥
#     bash shell/prep_e2e_conf.sh --nft           # 额外创建 nftables 前置（需 sudo）
#     CONF_DIR=/opt/vmsconf bash shell/prep_e2e_conf.sh
#
#  环境变量：
#     SOFT_HOME  项目根（默认由脚本位置推导）
#     CONF_DIR   配置输出目录（默认 /tmp/vmsconf）
#     NETD_PORT  netd 公网端口（默认 18099），同时用于 nftables 放行规则
#     KNOCK_PORT knockd UDP 监听端口（默认 5001）
#     FORCE      非 0 时覆盖已有密钥（默认保留，避免重跑后客户端内置公钥失效）
# ==============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOFT_HOME="${SOFT_HOME:-$(cd "$SCRIPT_DIR/.." && pwd)}"
CONF_DIR="${CONF_DIR:-/tmp/vmsconf}"
NETD_PORT="${NETD_PORT:-18099}"
KNOCK_PORT="${KNOCK_PORT:-5001}"
LOGIN_PORT="${LOGIN_PORT:-9002}"
BIZ_PORT="${BIZ_PORT:-9003}"
HELLO_PORT="${HELLO_PORT:-9001}"
DO_NFT=0
[ "${1:-}" = "--nft" ] && DO_NFT=1

TONGSUO_HOME="${TONGSUO_HOME:-$SOFT_HOME/tongsuo}"
mkdir -p "$CONF_DIR"

log() { printf '\033[1m== %s ==\033[0m\n' "$*"; }

# ---------------------------------------------------------------- 1. SPA 密钥
log "SPA 共享密钥（SM4，16 字节）"
SPA_KEY="$CONF_DIR/spa.key"
if [ -s "$SPA_KEY" ] && [ "${FORCE:-0}" != "1" ]; then
    printf '  已存在，保留: %s\n' "$SPA_KEY"
else
    head -c 16 /dev/urandom > "$SPA_KEY"
    chmod 600 "$SPA_KEY"
    printf '  已生成: %s (%s 字节)\n' "$SPA_KEY" "$(stat -c%s "$SPA_KEY")"
fi

# ------------------------------------------------------------ 2. SM2 密钥对
log "netd 静态 SM2 密钥对"
PRIV="$CONF_DIR/netd_sm2.priv"
PUB="$CONF_DIR/netd_sm2.pub"
if [ -s "$PRIV" ] && [ -s "$PUB" ] && [ "${FORCE:-0}" != "1" ]; then
    printf '  已存在，保留: %s / %s\n' "$PRIV" "$PUB"
else
    # Tongsuo 自带的 openssl 会优先链到系统 libssl.so.3（版本低于其构建基线），
    # 表现为 "version `OPENSSL_3.5.0' not found"。必须显式指向自带的 lib64。
    export LD_LIBRARY_PATH="$TONGSUO_HOME/lib64:${LD_LIBRARY_PATH:-}"
    if [ ! -x "$TONGSUO_HOME/bin/openssl" ]; then
        printf '\033[31m  缺少 %s/bin/openssl（请先构建 Tongsuo）\033[0m\n' "$TONGSUO_HOME" >&2
        exit 1
    fi
    TMP_PEM="$(mktemp /tmp/sm2_XXXXXX.pem)"
    "$TONGSUO_HOME/bin/openssl" ecparam -name SM2 -genkey -noout -out "$TMP_PEM" 2>/dev/null
    "$TONGSUO_HOME/bin/openssl" ec -in "$TMP_PEM" -outform DER -out "$PRIV" 2>/dev/null
    "$TONGSUO_HOME/bin/openssl" ec -in "$TMP_PEM" -pubout -outform DER -out "$PUB" 2>/dev/null
    rm -f "$TMP_PEM"
    chmod 600 "$PRIV"; chmod 644 "$PUB"
    printf '  已生成: %s (%s B) / %s (%s B)\n' \
        "$PRIV" "$(stat -c%s "$PRIV")" "$PUB" "$(stat -c%s "$PUB")"
    # 经验值：SEC1 私钥 DER=121B、SubjectPublicKeyInfo DER=91B；对不上说明格式不对，
    # 客户端内置公钥会解不开，握手必然失败。
    [ "$(stat -c%s "$PRIV")" = "121" ] && [ "$(stat -c%s "$PUB")" = "91" ] ||
        printf '\033[33m  [!] 密钥长度非 121/91，请核对 DER 格式\033[0m\n'
fi

# -------------------------------------------------------------- 3. 配置文件
log "生成配置文件 -> $CONF_DIR"

cat > "$CONF_DIR/netd_conf.xml" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<config>
    <netd>
        <listen_port>${NETD_PORT}</listen_port>
        <backlog>64</backlog>
        <nworkers>8</nworkers>
        <max_queue>1024</max_queue>
        <poll_timeout_ms>200</poll_timeout_ms>
        <key_id>1</key_id>
        <ipc_path>${CONF_DIR}/bizd.sock</ipc_path>
        <sm2_priv_file>${CONF_DIR}/netd_sm2.priv</sm2_priv_file>
        <sm2_pub_file>${CONF_DIR}/netd_sm2.pub</sm2_pub_file>
        <log_file></log_file>
        <log_level>debug</log_level>
        <log_console>1</log_console>
        <log_max_size>10485760</log_max_size>
        <enable_ipv6>0</enable_ipv6>
        <chroot_dir></chroot_dir>
        <drop_user></drop_user>
    </netd>
</config>
EOF

cat > "$CONF_DIR/bizd_conf.xml" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<config>
    <bizd>
        <ipc_path>${CONF_DIR}/bizd.sock</ipc_path>
        <backlog>64</backlog>
        <nworkers>8</nworkers>
        <max_queue>1024</max_queue>
        <poll_timeout_ms>200</poll_timeout_ms>
        <ep_hello><host>127.0.0.1</host><port>${HELLO_PORT}</port></ep_hello>
        <ep_auth><host>127.0.0.1</host><port>${LOGIN_PORT}</port></ep_auth>
        <ep_biz><host>127.0.0.1</host><port>${BIZ_PORT}</port></ep_biz>
        <ep_video><host>127.0.0.1</host><port>9004</port></ep_video>
        <log_file></log_file>
        <log_level>debug</log_level>
        <log_console>1</log_console>
        <log_max_size>10485760</log_max_size>
    </bizd>
</config>
EOF

cat > "$CONF_DIR/knockd_conf.xml" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<config>
    <knockd>
        <listen_port>${KNOCK_PORT}</listen_port>
        <spa_key_file>${CONF_DIR}/spa.key</spa_key_file>
        <nft_table>inet spa</nft_table>
        <nft_set>allow4</nft_set>
        <nft_set6>allow6</nft_set6>
        <default_ttl>300</default_ttl>
        <poll_timeout_ms>200</poll_timeout_ms>
        <log_file></log_file>
        <log_level>info</log_level>
        <log_console>1</log_console>
        <log_max_size>10485760</log_max_size>
    </knockd>
</config>
EOF

printf '  netd_conf.xml  bizd_conf.xml  knockd_conf.xml\n'

# --------------------------------------------------- 4. nftables 前置（可选）
if [ "$DO_NFT" != "1" ]; then
    printf '\n（跳过 nftables 前置；需要真实敲门验证时加 --nft，需 sudo）\n'
    printf '完成。接下来：\n'
    printf '  bash shell/e2e_m4.sh\n'
    exit 0
fi

log "nftables 前置（sudo）"
# 幂等：先删再建，重跑不留残规则
sudo nft delete table inet spa 2>/dev/null || true
sudo nft add table inet spa
sudo nft add set inet spa allow4 '{ type ipv4_addr; flags timeout; }'
sudo nft add set inet spa allow6 '{ type ipv6_addr; flags timeout; }'
sudo nft add chain inet spa input '{ type filter hook input priority filter; policy accept; }'
# 只针对 netd 端口：白名单内 accept，其余 drop。
# 绝不在此处动 SSH(22) —— 远程机器上误封等于把自己关在门外。
sudo nft add rule inet spa input tcp dport "$NETD_PORT" ip saddr @allow4 accept
sudo nft add rule inet spa input tcp dport "$NETD_PORT" drop
printf '  表/集合/规则已建。当前规则：\n'
sudo nft list table inet spa

printf '\n\033[1m现在 knockd 需要 CAP_NET_ADMIN 才能真正开门：\033[0m\n'
printf '  sudo setcap cap_net_admin+ep %s/bin/q_service_knockd\n' "$SOFT_HOME"
printf '  验证：sudo getcap %s/bin/q_service_knockd\n' "$SOFT_HOME"
