#!/usr/bin/env bash
# =============================================================================
# run_m5_services.sh —— 常驻启动 M5 全栈服务（不自动清理，供 Windows GUI 手动联调）
# -----------------------------------------------------------------------------
# 与 e2e_m5.sh 的区别：
#   - 不挂 cleanup trap：服务跑起来后脚本退出，进程继续存活（setsid 脱离 ssh 会话）
#   - 默认不编译：先确保 bin/ 下已有二进制；要重编传 --build
#   - KNOCK 默认 0（netd/netd_B 直连，端口不被防火墙挡，手动联调最省事）
#   - 想验完整 SPA 敲门路径：KNOCK=1 bash run_m5_services.sh
#
# 用法（在 Linux 虚拟机 192.168.19.22 上跑）：
#   bash shell/run_m5_services.sh                # 直连模式，起 5 个服务并常驻
#   KNOCK=1 bash shell/run_m5_services.sh        # 带 SPA 敲门（需 nftables + cap_net_admin）
#   bash shell/run_m5_services.sh --build        # 先重编 lib_src + 服务再起
#
# 停止：bash shell/stop_m5_services.sh
# =============================================================================
set -uo pipefail

DO_BUILD=0
while [ $# -gt 0 ]; do
    case "$1" in
        --build) DO_BUILD=1 ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "未知参数: $1（--help 查看用法）" >&2; exit 1 ;;
    esac
    shift
done

SOFT_HOME="${SOFT_HOME:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
CONF_DIR="${CONF_DIR:-/tmp/vmsconf}"
LIB_DIR="${LIB_DIR:-/tmp/cdlib_lx}"
DEP_HOME="${DEP_HOME:-/tmp/lxdeps_root}"
TONGSUO_HOME="${TONGSUO_HOME:-$DEP_HOME/tongsuo}"

NETD_PORT="${NETD_PORT:-18099}"
KNOCK_PORT="${KNOCK_PORT:-5001}"
PUSH_NETD_PORT="${PUSH_NETD_PORT:-18098}"
PUSH_KNOCK_PORT="${PUSH_KNOCK_PORT:-5002}"

# 这些与 e2e_m5.sh 对齐（桩服务的登录账号/盐/迭代）
USER_NAME="${USER_NAME:-qiaoshui}"
USER_PASS="${USER_PASS:-123456}"
SALT_HEX="${SALT_HEX:-00112233445566778899aabbccddeeff}"
ITER="${ITER:-100000}"

export CPATH="/tmp/lxdeps/usr/include:/tmp/lxdeps/usr/include/libxml2:${CPATH:-}"
export LIBRARY_PATH="/tmp/lxdeps/usr/lib/x86_64-linux-gnu:${LIBRARY_PATH:-}"

log()  { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }
die()  { printf '\033[31m失败: %s\033[0m\n' "$*" >&2; exit 1; }

# 就绪判定看内核监听态（ss -tln），不被 nft 过滤
wait_port() {
    local port="$1" name="$2" i
    for i in $(seq 1 60); do
        if ss -tln 2>/dev/null | awk '{print $4}' | grep -q ":${port}\$"; then
            return 0
        fi
        sleep 0.1
    done
    die "${name} 未能在 ${port} 监听（见 ${CONF_DIR}/${name}.log）"
}

# ---------------------------------------------------------------- 0. 配置 & 可选编译
[ -d "$CONF_DIR" ] || bash "$SOFT_HOME/shell/prep_e2e_conf.sh"
[ -f "$CONF_DIR/netd_conf.xml" ] || die "缺配置 $CONF_DIR/netd_conf.xml（先跑 prep_e2e_conf.sh）"

if [ "$DO_BUILD" = "1" ]; then
    log "重编 lib_src + 服务"
    ( cd "$SOFT_HOME" &&
      LIB="$LIB_DIR" DEP_HOME="$DEP_HOME" TONGSUO_HOME="$TONGSUO_HOME" \
      bash shell/build.sh ) >/tmp/m5_build.log 2>&1 || { tail -25 /tmp/m5_build.log; die "构建失败"; }
fi
for b in q_service_login q_service_bizd q_service_netd q_service_push; do
    [ -x "$SOFT_HOME/bin/$b" ] || die "未生成 $SOFT_HOME/bin/$b（传 --build 重编）"
done

# ---------------------------------------------------------------- 1. 起服务（setsid 常驻）
# 用 setsid nohup 让进程脱离本会话：即使 ssh 断开，服务也不被杀。
log "启动 bizd / netd_A / netd_B / pushd / login（常驻）"
rm -f "$CONF_DIR/bizd.sock" "$CONF_DIR/pushd.sock"

setsid nohup "$SOFT_HOME/bin/q_service_bizd"  -c "$CONF_DIR/bizd_conf.xml"        >"$CONF_DIR/bizd.log"        2>&1 &
setsid nohup "$SOFT_HOME/bin/q_service_netd"  -c "$CONF_DIR/netd_conf.xml"        >"$CONF_DIR/netd.log"        2>&1 &
setsid nohup "$SOFT_HOME/bin/q_service_netd"  -c "$CONF_DIR/netd_push_conf.xml"   >"$CONF_DIR/netd_push.log"   2>&1 &
setsid nohup "$SOFT_HOME/bin/q_service_push"  -s "$CONF_DIR/pushd.sock" -L 2      >"$CONF_DIR/pushd.log"       2>&1 &
setsid nohup "$SOFT_HOME/bin/q_service_login" -p 9002 -u "$USER_NAME" -k "$USER_PASS" \
        -S "$SALT_HEX" -i "$ITER"                                              >"$CONF_DIR/login.log"       2>&1 &

sleep 0.5
wait_port 9002 login
wait_port "$NETD_PORT" netd_A
wait_port "$PUSH_NETD_PORT" netd_B
for i in $(seq 1 50); do [ -S "$CONF_DIR/pushd.sock" ] && break; sleep 0.1; done
[ -S "$CONF_DIR/pushd.sock" ] || die "pushd.sock 未出现（见 ${CONF_DIR}/pushd.log）"

# ---------------------------------------------------------------- 2. 可选 SPA 敲门
KNOCK="${KNOCK:-0}"
KNOCK_NFT="${KNOCK_NFT:-1}"
if [ "$KNOCK" = "1" ]; then
    SPA_KEY="${SPA_KEY:-$CONF_DIR/spa.key}"
    [ -f "$SPA_KEY" ] || die "缺少 SPA 密钥 $SPA_KEY"
    if ! getcap "$SOFT_HOME/bin/q_service_knockd" 2>/dev/null | grep -q cap_net_admin; then
        sudo -n setcap cap_net_admin+ep "$SOFT_HOME/bin/q_service_knockd" 2>/dev/null ||
            die "无法自动补 capability，请手动: sudo setcap cap_net_admin+ep $SOFT_HOME/bin/q_service_knockd"
    fi
    for conf in knockd_conf.xml knockd_push_conf.xml; do
        [ -f "$CONF_DIR/$conf" ] || die "缺少 $CONF_DIR/$conf"
        if [ "$KNOCK_NFT" = "1" ]; then
            setsid nohup "$SOFT_HOME/bin/q_service_knockd" -c "$CONF_DIR/$conf" \
                    >"$CONF_DIR/${conf%.xml}.log" 2>&1 &
        else
            KNOCKD_NO_NFT=1 setsid nohup "$SOFT_HOME/bin/q_service_knockd" -c "$CONF_DIR/$conf" \
                    >"$CONF_DIR/${conf%.xml}.log" 2>&1 &
        fi
    done
    sleep 0.5
    printf 'knockd 已起：A=%s B=%s（%s）\n' "$KNOCK_PORT" "$PUSH_KNOCK_PORT" \
        "$([ "$KNOCK_NFT" = '1' ] && echo '真开门' || echo '降级只记审计')"
else
    printf '  [--] KNOCK!=1：跳过 SPA 敲门（netd/netd_B 直连，确保端口未被防火墙挡）\n'
fi

# ---------------------------------------------------------------- 3. 汇总
printf '\n\033[1m服务已常驻启动（脚本退出不影响）。\033[0m\n'
printf '  netd_A  = %s\n'      "$NETD_PORT"
printf '  netd_B  = %s  (M5 推送栈)\n' "$PUSH_NETD_PORT"
printf '  pushd   = %s/pushd.sock\n' "$CONF_DIR"
printf '  login   = 9002\n'
printf '  knock   = %s (KNOCK=%s)\n' "$KNOCK_PORT" "$KNOCK"
printf '\n日志：%s/*.log\n' "$CONF_DIR"
printf '停止：bash shell/stop_m5_services.sh\n'
