#!/usr/bin/env bash
# ==============================================================================
#  M5 端到端联调脚本：实时推送（第二条独立长连接）
#
#  链路（推送栈是**独立服务实例**，与命令栈平行，共用同一套 SM2 身份密钥）：
#    命令栈：qc_login_ut ─TCP+SM2─> netd_A:18099 ─UDS─> bizd ─TCP─> login:9002
#    推送栈：qc_login_ut ─TCP+SM2─> netd_B:18098 ─UDS─> pushd(pushd.sock)
#            （PUSH_BEGIN 时 netd_B 为该客户端连接建一条到 pushd 的持久 UDS + 读线程）
#    隐身  ：knockd_A:5001 放行 18099；knockd_B:5002 放行 18098（同一 allow4 集合）
#
#  验证内容（qc_login_ut --push 段，主流程 9.7）：
#    1. 推送连接自建 knock -> connect -> SM2 握手 -> HELLO -> LOGIN -> PUSH_BEGIN
#       （lockstep 应答；Token 绑定 channel_id，命令连接的 Token 在推送连接上验不过）
#    2. SUB_DELTA ADD 两台车 -> 轨迹按订阅每秒到达（事件驱动，有数即推）
#    3. 过半时 SUB_DELTA REMOVE 一台 -> 在途帧 drain 后该车轨迹停发
#    4. 收听 >=25s 时报警至少 1 条（桩周期 12/17/23s，按用户组织子树过滤）
#    5. 收听期间推送连接保持（keepalive 兜底半开检测）
#
#  用法：
#    bash shell/e2e_m5.sh
#    PUSH_SEC=12 PUSH_RM= bash shell/e2e_m5.sh      # 缩短收听、跳过 REMOVE 用例
#    KNOCK=1 bash shell/e2e_m5.sh                   # 带 SPA 敲门（真机 / 有 nftables 的 VM）
#    SKIP_BUILD=1 bash shell/e2e_m5.sh              # 二进制已是最新，跳过编译
#
#  环境变量（都有默认值）：
#    SOFT_HOME / CONF_DIR / LIB_DIR / DEP_HOME / USER_NAME / USER_PASS
#    NETD_PORT / KNOCK_PORT            A 栈（命令连接，默认 18099 / 5001）
#    PUSH_NETD_PORT / PUSH_KNOCK_PORT  B 栈（推送，默认 18098 / 5002）
#    PUSH_SEC      收听推送秒数（默认 30，>=25 才能看到报警）
#    PUSH_VIDS     订阅车辆主键，逗号分隔（默认 京A10001-1,京A10003-2）
#    PUSH_RM       收听过半时 REMOVE 的车辆（默认 京A10001-1；置空跳过该用例）
#
#  注意：WSL 的后台进程会随 `wsl bash -c` 结束被杀，所以**必须**在本脚本内
#        完成"起服务 -> 跑用例 -> 收尾"，不能分两条命令。
# ==============================================================================
set -u

SOFT_HOME="${SOFT_HOME:-/mnt/e/win_e/cs_dev}"
CONF_DIR="${CONF_DIR:-/tmp/vmsconf}"
LIB_DIR="${LIB_DIR:-/tmp/cdlib_lx}"
DEP_HOME="${DEP_HOME:-/tmp/lxdeps_root}"
TONGSUO_HOME="${TONGSUO_HOME:-$DEP_HOME/tongsuo}"

USER_NAME="${USER_NAME:-qiaoshui}"
USER_PASS="${USER_PASS:-123456}"
SALT_HEX="${SALT_HEX:-00112233445566778899aabbccddeeff}"
ITER="${ITER:-100000}"

NETD_PORT="${NETD_PORT:-18099}"
KNOCK_PORT="${KNOCK_PORT:-5001}"
PUSH_NETD_PORT="${PUSH_NETD_PORT:-18098}"
PUSH_KNOCK_PORT="${PUSH_KNOCK_PORT:-5002}"

PUSH_SEC="${PUSH_SEC:-30}"
PUSH_VIDS="${PUSH_VIDS:-京A10001-1,京A10003-2}"
# 注意：PUSH_RM 默认值里是 REMOVE 用例要删的那台车；置空则跳过"停发"验证
PUSH_RM="${PUSH_RM:-京A10001-1}"

export CPATH="/tmp/lxdeps/usr/include:/tmp/lxdeps/usr/include/libxml2:${CPATH:-}"
export LIBRARY_PATH="/tmp/lxdeps/usr/lib/x86_64-linux-gnu:${LIBRARY_PATH:-}"

PIDS=""
cleanup() {
    for p in $PIDS; do kill "$p" 2>/dev/null; done
    wait 2>/dev/null
}
trap cleanup EXIT

log()  { printf '\n\033[1m== %s ==\033[0m\n' "$*"; }
die()  { printf '\033[31m失败: %s\033[0m\n' "$*" >&2; exit 1; }

wait_port() {
    local port="$1" name="$2" i
    for i in $(seq 1 60); do
        if (exec 3<>"/dev/tcp/127.0.0.1/${port}") 2>/dev/null; then exec 3<&-; return 0; fi
        sleep 0.1
    done
    die "${name} 未能在 ${port} 就绪（见 ${CONF_DIR}/${name}.log）"
}

# ---------------------------------------------------------------- 1. 编译
UT="$SOFT_HOME/lib_src/l_4/q_clientcomm/ut/bin/qc_login_ut"

# 跨端构建残留自检：Windows 构建会在源码树留下 COFF .o，make 只看时间戳不会重编，
# Linux 链接时表现为 undefined reference 甚至 ld 段错误（反之亦然）。
QC_DIR="$SOFT_HOME/lib_src/l_4/q_clientcomm"
if command -v file >/dev/null 2>&1; then
    BAD_O=$(find "$QC_DIR" -name '*.o' -exec file {} \; 2>/dev/null |
            grep -v 'ELF' | head -1)
    if [ -n "$BAD_O" ]; then
        printf '\033[33m[!]\033[0m 发现非 ELF 的 .o 残留（跨端构建遗留），已清理: %s\n' \
               "$(printf '%s' "$BAD_O" | cut -c1-60)"
        find "$QC_DIR" -name '*.o' -delete
    fi
fi

if [ "${SKIP_BUILD:-0}" != "1" ]; then
log "编译 l_4 与 qc_login_ut"
if ! ( cd "$SOFT_HOME/lib_src/l_4/q_clientcomm" &&
       make ut_login LIB="$LIB_DIR" DEP_HOME="$DEP_HOME" TONGSUO_HOME="$TONGSUO_HOME" \
            >/tmp/e2e_m5_build.log 2>&1 ); then
    tail -25 /tmp/e2e_m5_build.log
    die "UT 编译失败（详见 /tmp/e2e_m5_build.log）"
fi

# 服务也必须编：netd 新增了 netd_push.c（反向通路），pushd 是全新二进制。
# e2e_m4 假设服务已预编，这里显式编一遍 —— VM 上留着旧 netd 是 M5 最常见的失败原因。
log "编译 soft_src 服务（login / bizd / netd / pushd）"
for svc in q_s_vmsv1__login bizd netd q_s_vmsv1__push; do
    if ! ( cd "$SOFT_HOME/soft_src/$svc" &&
           make LIB="$LIB_DIR" DEP_HOME="$DEP_HOME" TONGSUO_HOME="$TONGSUO_HOME" \
                >"/tmp/e2e_m5_build_$svc.log" 2>&1 ); then
        tail -25 "/tmp/e2e_m5_build_$svc.log"
        die "$svc 编译失败（详见 /tmp/e2e_m5_build_$svc.log）"
    fi
done
fi
for b in q_service_login q_service_bizd q_service_netd q_service_push; do
    [ -x "$SOFT_HOME/bin/$b" ] || die "未生成 $SOFT_HOME/bin/$b"
done
[ -x "$UT" ] || die "未生成 $UT"

# ---------------------------------------------------------------- 2. 起服务
log "启动 bizd / netd_A / netd_B / pushd / login"
rm -f "$CONF_DIR/bizd.sock" "$CONF_DIR/pushd.sock"

"$SOFT_HOME/bin/q_service_bizd" -c "$CONF_DIR/bizd_conf.xml" >"$CONF_DIR/bizd.log" 2>&1 &
PIDS="$PIDS $!"
sleep 0.4

# A 栈：命令连接（UT 主流程仍要走登录闭环，顺带验证新 netd 二进制没破坏老功能）
"$SOFT_HOME/bin/q_service_netd" -c "$CONF_DIR/netd_conf.xml" >"$CONF_DIR/netd.log" 2>&1 &
PIDS="$PIDS $!"

# B 栈：推送连接。netd_push_conf.xml 的 push_ipc_path 指向 $CONF_DIR/pushd.sock，
# PUSH_BEGIN 时才会真正建 UDS（先起 pushd，否则 BEGIN 时 connect 失败）。
"$SOFT_HOME/bin/q_service_netd" -c "$CONF_DIR/netd_push_conf.xml" >"$CONF_DIR/netd_push.log" 2>&1 &
PIDS="$PIDS $!"

"$SOFT_HOME/bin/q_service_push" -s "$CONF_DIR/pushd.sock" -L 2 \
        >"$CONF_DIR/pushd.log" 2>&1 &
PIDS="$PIDS $!"

"$SOFT_HOME/bin/q_service_login" -p 9002 -u "$USER_NAME" -k "$USER_PASS" \
        -S "$SALT_HEX" -i "$ITER" >"$CONF_DIR/login.log" 2>&1 &
PIDS="$PIDS $!"

wait_port 9002 login
wait_port "$NETD_PORT" netd_A
wait_port "$PUSH_NETD_PORT" netd_B
for i in $(seq 1 50); do [ -S "$CONF_DIR/pushd.sock" ] && break; sleep 0.1; done
[ -S "$CONF_DIR/pushd.sock" ] || die "pushd.sock 未出现（见 ${CONF_DIR}/pushd.log）"
printf '服务就绪：netd_A=%s netd_B=%s pushd=%s login=9002\n' \
    "$NETD_PORT" "$PUSH_NETD_PORT" "$CONF_DIR/pushd.sock"

# ------------------------------------------- 2.1 可选：起 knockd_A + knockd_B
# 为什么值得单独验：nftables 放行条目有 TTL（默认 300s），推送长连接断开期间条目可能
# 已过期，不重新敲门就会出现"连不上但原因不明"。两条连接都必须带自己的敲门参数。
KNOCK="${KNOCK:-0}"
# KNOCK_NFT=0 给 knockd 设 KNOCKD_NO_NFT=1：走「只验签记审计、不开门」的降级路径，
# 用于 WSL 等**内核无 NETLINK_NETFILTER** 的环境；真机默认 1（真开门）。
KNOCK_NFT="${KNOCK_NFT:-1}"
KNOCK_ARGS=""
if [ "$KNOCK" = "1" ]; then
    SPA_KEY="${SPA_KEY:-$CONF_DIR/spa.key}"
    [ -f "$SPA_KEY" ] || die "缺少 SPA 密钥 $SPA_KEY（KNOCK=1 需要，可用 shell/prep_e2e_conf.sh 生成）"
    for conf in knockd_conf.xml knockd_push_conf.xml; do
        [ -f "$CONF_DIR/$conf" ] || die "缺少 $CONF_DIR/$conf（KNOCK=1 需要）"
        if [ "$KNOCK_NFT" = "1" ]; then
            "$SOFT_HOME/bin/q_service_knockd" -c "$CONF_DIR/$conf" \
                    >"$CONF_DIR/${conf%.xml}.log" 2>&1 &
        else
            KNOCKD_NO_NFT=1 "$SOFT_HOME/bin/q_service_knockd" -c "$CONF_DIR/$conf" \
                    >"$CONF_DIR/${conf%.xml}.log" 2>&1 &
        fi
        PIDS="$PIDS $!"
    done
    sleep 0.5
    KNOCK_ARGS="--knock 127.0.0.1:${KNOCK_PORT} --push-knock 127.0.0.1:${PUSH_KNOCK_PORT} --spakey ${SPA_KEY}"
    printf 'knockd 已起：A=127.0.0.1:%s B=127.0.0.1:%s（%s）\n' \
        "$KNOCK_PORT" "$PUSH_KNOCK_PORT" \
        "$([ "$KNOCK_NFT" = '1' ] && echo '真开门' || echo '降级：只记审计不开门')"
else
    printf '  [--] KNOCK!=1：跳过 SPA 敲门（netd/netd_B 直连，需端口未被防火墙挡）\n'
fi

# ---------------------------------------------------------------- 3. 跑推送联调
log "推送联调（收听 %ss，订阅 %s，REMOVE %s）" "$PUSH_SEC" "$PUSH_VIDS" "${PUSH_RM:-（跳过）}"
# stdbuf：UT 输出重定向到文件/管道时是全缓冲的，挂死一行都看不到 —— 强制行缓冲。
# timeout：任何一环节挂死都不至于把整个脚本拖住（UT_TIMEOUT=0 表示不限时）。
UT_TIMEOUT="${UT_TIMEOUT:-180}"
if [ "$UT_TIMEOUT" != "0" ] && command -v timeout >/dev/null 2>&1; then
    RUNNER="timeout ${UT_TIMEOUT}"
else
    RUNNER=""
fi
if command -v stdbuf >/dev/null 2>&1; then
    STDBUF="stdbuf -oL -eL"
else
    STDBUF=""
fi

PUSH_RM_ARGS=""
[ -n "$PUSH_RM" ] && PUSH_RM_ARGS="--push-rm $PUSH_RM"

# --no-biz：M5 只验推送面，业务查询已由 e2e_m3/m4 覆盖；但命令连接的登录闭环照跑
# （pushd 的 BEGIN 鉴权依赖它：Token 由推送连接自己的 LOGIN 产生）。
$STDBUF $RUNNER "$UT" --host 127.0.0.1 --port "$NETD_PORT" --user "$USER_NAME" --pass "$USER_PASS" \
      --pub "$CONF_DIR/netd_sm2.pub" --no-biz \
      --push --push-port "$PUSH_NETD_PORT" --push-sec "$PUSH_SEC" \
      --push-vids "$PUSH_VIDS" $PUSH_RM_ARGS $KNOCK_ARGS 2>&1 |
    grep -vE '^\s*$' | tail -${TAIL_N:-60}
rc_ut=${PIPESTATUS[0]}
[ "$rc_ut" = "124" ] && printf '\033[31mUT 超时（%ss）：存在挂死，见上方最后一行输出\033[0m\n' "$UT_TIMEOUT"

# ---------------------------------------------------------------- 4. 服务端佐证
# UT 的 step 断言是第一证据；这里再从服务端日志交叉印证，防止"客户端以为通了、
# 服务端其实什么都没干"（M3 的 Mock/真库之辨就是这么栽过的）。
log "服务端佐证"
if [ -f "$CONF_DIR/pushd.log" ]; then
    nb=$(grep -c 'push 会话建立' "$CONF_DIR/pushd.log" 2>/dev/null || true)
    ns=$(grep -c 'push 订阅更新' "$CONF_DIR/pushd.log" 2>/dev/null || true)
    printf '  pushd：会话建立 %s 次，订阅更新 %s 次\n' "${nb:-0}" "${ns:-0}"
    grep -E 'push 会话建立|push 订阅更新' "$CONF_DIR/pushd.log" | tail -6
else
    printf '\033[33m[--]\033[0m 无 pushd.log\n'
fi
if [ -f "$CONF_DIR/netd_push.log" ]; then
    printf '  netd_B 推送相关日志（尾 6 行）：\n'
    grep -iE 'push' "$CONF_DIR/netd_push.log" | tail -6
fi

# 敲门验证：pushd 侧看不到敲门，看 knockd_B 的收包记录 + nft 集合元素。
if [ "$KNOCK" = "1" ]; then
    log "敲门验证（两条连接都应重新发 SPA 包）"
    if ! ps -ef | grep "[q]_service_knockd" >/dev/null 2>&1; then
        printf '\033[31m[FAIL]\033[0m knockd 进程已不在 —— SPA 敲门路径未通过验证\n'
    else
        for klog in knockd.log knockd_push.log; do
            [ -f "$CONF_DIR/$klog" ] || continue
            n=$(grep -cE 'SPA 授权成功|SPA 校验失败' "$CONF_DIR/$klog" 2>/dev/null || true)
            printf '  %s：SPA 记录 %s 条\n' "$klog" "${n:-0}"
        done
        if [ "$KNOCK_NFT" = "1" ] && command -v nft >/dev/null 2>&1; then
            tbl=$(sed -n 's:.*<nft_table>\(.*\)</nft_table>.*:\1:p' "$CONF_DIR/knockd_conf.xml" 2>/dev/null | head -1)
            st4=$(sed -n 's:.*<nft_set>\(.*\)</nft_set>.*:\1:p' "$CONF_DIR/knockd_conf.xml" 2>/dev/null | head -1)
            if [ -n "$tbl" ] && [ -n "$st4" ]; then
                # 读 nft 集合需 CAP_NET_ADMIN，普通用户会 "Operation not permitted"，先直连再退回 sudo -n
                out=$(nft list set "$tbl" "$st4" 2>/dev/null ||
                      sudo -n nft list set "$tbl" "$st4" 2>/dev/null)
                el=$(printf '%s' "$out" | grep -c 'expires' || true)
                if [ "${el:-0}" -gt 0 ]; then
                    printf '\033[32m[OK]\033[0m nft 放行集合 %s %s 内有 %s 个带 TTL 的元素（门真开了）\n' "$tbl" "$st4" "$el"
                    printf '%s\n' "$out" | grep 'expires' | head -3
                else
                    printf '\033[31m[FAIL]\033[0m nft 放行集合无元素 —— 敲门成功但门没开（查 CAP_NET_ADMIN 与集合是否预创建）\n'
                fi
            fi
        fi
    fi
fi

log "完成（服务端日志：${CONF_DIR}/*.log；UT rc=${rc_ut}）"
[ "$rc_ut" = "0" ] || exit "$rc_ut"
