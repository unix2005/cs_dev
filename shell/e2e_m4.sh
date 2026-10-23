#!/usr/bin/env bash
# ==============================================================================
#  M4 端到端联调脚本：会话治理（心跳 / 退避重连 / Token 失效重登 / 并发 gate）
#
#  链路：
#    qc_login_ut ─(TCP + SM2 握手 + SM4-GCM)─> netd:18099
#                ─(Unix Socket TLV)─> bizd ─(TCP JSON)─> login:9002 / biz:9003
#
#  验证内容（全部在 ut/qc_login_ut.c 的 --keeper 段）：
#    用例1  正常心跳：心跳计数增长、state 保持 ONLINE
#    用例2  注入 QC_FAULT_HB_RC3（心跳回 3）→ 自动重新登录，relogins 增长，
#           pwd_hash 由 UI 回调现派生（内核不存口令）
#    用例3  注入 QC_FAULT_HB_FAIL（心跳断连）→ 退避重连；故障清除后恢复 ONLINE
#    用例4  N 个业务线程并发 BIZ_QUERY + gate 串行化，验证 lockstep 不被并发破坏
#    用例5  UI 无法提供口令 → 降级 OFFLINE；恢复凭据 + wake → 回到 ONLINE
#
#  用法（WSL 里）：
#     bash shell/e2e_m4.sh
#     HB=1 SEC=3 CONCURRENT=2 bash shell/e2e_m4.sh      # 缩短观察窗口
#
#  环境变量（都有默认值）：
#     SOFT_HOME / CONF_DIR / LIB_DIR / DEP_HOME / USER_NAME / USER_PASS
#     HB          心跳间隔（秒，默认 2）
#     SEC         每个观察窗口时长（秒，默认 5）
#     CONCURRENT  并发业务线程数（默认 4）
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

HB="${HB:-2}"
SEC="${SEC:-5}"
CONCURRENT="${CONCURRENT:-4}"
# CASE_SEL=1..5 只跑指定用例（0 = 全跑）；排查单个用例时用它缩短单次运行时长
CASE_SEL="${CASE_SEL:-0}"
CASE_ARGS=""
[ "$CASE_SEL" != "0" ] && CASE_ARGS="--case $CASE_SEL"

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

# 就绪判定必须看**内核监听态**而不是"能连上"：nftables 对 netd 端口有 drop 规则，
# 未敲门时 /dev/tcp 探测会被静默丢包，每次 connect 阻塞 ~127s（SYN 重传），
# 60 次重试能把脚本挂住几小时（M5 首次联调真实踩中：服务全就绪，脚本卡在探测）。
wait_port() {
    local port="$1" name="$2" i
    for i in $(seq 1 60); do
        if command -v ss >/dev/null 2>&1; then
            if ss -tln 2>/dev/null | awk '{print $4}' | grep -q ":${port}\$"; then
                return 0
            fi
        elif (exec 3<>"/dev/tcp/127.0.0.1/${port}") 2>/dev/null; then
            exec 3<&-
            return 0
        fi
        sleep 0.1
    done
    die "${name} 未能在 ${port} 监听（见 ${CONF_DIR}/${name}.log）"
}

# ---------------------------------------------------------------- 1. 编译 UT
# SKIP_BUILD=1 可跳过（UT 已编译好时，省掉每次 ~15s 的重编）
UT="$SOFT_HOME/lib_src/l_4/q_clientcomm/ut/bin/qc_login_ut"

# 跨端构建残留自检：Windows 构建会在源码树留下 COFF .o，make 只看时间戳不会重编，
# Linux 链接时表现为 undefined reference 甚至 ld 段错误（反之亦然）。
# 这里先扫一遍，发现非本机架构的 .o 就清掉 —— 比让人记"跑之前先 find -delete"可靠。
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
# lib_src 必须最先编：UT/服务都静态链接 lib/ 下的 q_*.a，库里落后于源码时
# 表现为"服务全就绪却莫名拒绝合法指令"（M5 曾因 libqchan.a 旧了一小时，
# netd_B 拒收 PUSH_BEGIN）。build.sh 同时编排 soft_src 服务与 login/biz 微服务。
log "构建 lib_src（l_1~l_3）与 soft_src 服务"
if ! ( cd "$SOFT_HOME" &&
       LIB="$LIB_DIR" DEP_HOME="$DEP_HOME" TONGSUO_HOME="$TONGSUO_HOME" \
       bash shell/build.sh >/tmp/e2e_m4_lib.log 2>&1 ); then
    tail -25 /tmp/e2e_m4_lib.log
    die "lib_src 构建失败（详见 /tmp/e2e_m4_lib.log）"
fi

log "编译 l_4 与 qc_login_ut"
if ! ( cd "$SOFT_HOME/lib_src/l_4/q_clientcomm" &&
       make ut_login LIB="$LIB_DIR" DEP_HOME="$DEP_HOME" TONGSUO_HOME="$TONGSUO_HOME" \
            >/tmp/e2e_m4_build.log 2>&1 ); then
    tail -25 /tmp/e2e_m4_build.log
    die "UT 编译失败（详见 /tmp/e2e_m4_build.log）"
fi
fi
[ -x "$UT" ] || die "未生成 $UT"

# ---------------------------------------------------------------- 2. 起服务
log "启动 bizd / netd / login / biz"
rm -f "$CONF_DIR/bizd.sock"

"$SOFT_HOME/bin/q_service_bizd" -c "$CONF_DIR/bizd_conf.xml" >"$CONF_DIR/bizd.log" 2>&1 &
PIDS="$PIDS $!"
sleep 0.4

"$SOFT_HOME/bin/q_service_netd" -c "$CONF_DIR/netd_conf.xml" >"$CONF_DIR/netd.log" 2>&1 &
PIDS="$PIDS $!"

"$SOFT_HOME/bin/q_service_login" -p 9002 -u "$USER_NAME" -k "$USER_PASS" \
        -S "$SALT_HEX" -i "$ITER" >"$CONF_DIR/login.log" 2>&1 &
PIDS="$PIDS $!"

"$SOFT_HOME/bin/q_service_biz" -p 9003 >"$CONF_DIR/biz.log" 2>&1 &
PIDS="$PIDS $!"

wait_port 9002 login
wait_port 9003 biz
wait_port "$NETD_PORT" netd
printf '服务就绪：netd=%s login=9002 biz=9003\n' "$NETD_PORT"

# ------------------------------------------------- 2.1 可选：起 knockd 验证"重建时重新敲门"
# 为什么值得单独验：nftables 放行条目有 TTL（默认 300s），长连接断开期间条目可能已过期，
# 不重新敲门就会出现"连不上但原因不明"。这条路径只有带上 --knock 才会走到。
# WSL 里 knockd 的 seccomp/nftables 会失败（无 CAP_NET_ADMIN），但**UDP 收包照常**，
# 足以证明"重建时确实发了格式正确的 SPA 包"。
KNOCK="${KNOCK:-0}"
# KNOCK_NFT=0 给 knockd 设 KNOCKD_NO_NFT=1：走「只验签记审计、不开门」的降级路径，
# 用于 WSL 等**内核无 NETLINK_NETFILTER** 的环境 —— 那里照样能证明
# "重建时确实发了格式正确的 SPA 包"（UDP 收包与校验不受影响）。
# 真机默认 1（真开门）：验证完整链路 敲门 -> nft 集合出现带 TTL 的元素 -> 端口由不可达变可达。
KNOCK_NFT="${KNOCK_NFT:-1}"
KNOCK_ARGS=""
if [ "$KNOCK" = "1" ]; then
    SPA_KEY="${SPA_KEY:-$CONF_DIR/spa.key}"
    KNOCK_PORT="${KNOCK_PORT:-5001}"
    [ -f "$SPA_KEY" ] || die "缺少 SPA 密钥 $SPA_KEY（KNOCK=1 需要，可用 shell/prep_e2e_conf.sh 生成）"
    # 重编 knockd 会丢 file capability（上面的 build.sh 刚编过它）—— 不补上就是
    # "SPA 验签成功但 nft 开门失败"：端口依旧不可时才会暴露，且日志里只有一行 ERROR。
    if ! getcap "$SOFT_HOME/bin/q_service_knockd" 2>/dev/null | grep -q cap_net_admin; then
        printf '\033[33m[!]\033[0m knockd 缺 cap_net_admin（重编丢失），自动补上...\n'
        sudo -n setcap cap_net_admin+ep "$SOFT_HOME/bin/q_service_knockd" 2>/dev/null ||
            die "无法自动补 capability，请手动执行: sudo setcap cap_net_admin+ep $SOFT_HOME/bin/q_service_knockd"
        printf '  已补上\n'
    fi
    if [ "$KNOCK_NFT" = "1" ]; then
        "$SOFT_HOME/bin/q_service_knockd" -c "$CONF_DIR/knockd_conf.xml" \
                >"$CONF_DIR/knockd.log" 2>&1 &
    else
        KNOCKD_NO_NFT=1 "$SOFT_HOME/bin/q_service_knockd" -c "$CONF_DIR/knockd_conf.xml" \
                >"$CONF_DIR/knockd.log" 2>&1 &
    fi
    PIDS="$PIDS $!"
    sleep 0.5
    KNOCK_ARGS="--knock 127.0.0.1:${KNOCK_PORT} --spakey ${SPA_KEY}"
    printf 'knockd 已起：127.0.0.1:%s（日志 %s/knockd.log，%s）\n' \
        "$KNOCK_PORT" "$CONF_DIR" \
        "$([ "$KNOCK_NFT" = '1' ] && echo '真开门' || echo '降级：只记审计不开门')"
fi

# ---------------------------------------------------------------- 3. 跑会话治理用例
log "会话治理自检（心跳 %ss，窗口 %ss，并发 %s 线程）" "$HB" "$SEC" "$CONCURRENT"
# stdbuf：UT 输出重定向到文件/管道时是全缓冲的，挂死一行都看不到 —— 强制行缓冲。
# timeout：任何一环节挂死都不至于把整个脚本拖住（UT_TIMEOUT=0 表示不限时）。
UT_TIMEOUT="${UT_TIMEOUT:-90}"
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

$STDBUF $RUNNER "$UT" --host 127.0.0.1 --port "$NETD_PORT" --user "$USER_NAME" --pass "$USER_PASS" \
      --pub "$CONF_DIR/netd_sm2.pub" \
      --keeper --hb-interval "$HB" --keeper-sec "$SEC" --concurrent "$CONCURRENT" \
      --no-biz $CASE_ARGS $KNOCK_ARGS 2>&1 |
    grep -vE '^\s*$' | tail -${TAIL_N:-45}
rc_ut=${PIPESTATUS[0]}
[ "$rc_ut" = "124" ] && printf '\033[31mUT 超时（%ss）：存在挂死，见上方最后一行输出\033[0m\n' "$UT_TIMEOUT"

# 敲门验证：重建连接时 keeper 必须重发 SPA 包（nftables 放行条目有 TTL，不重敲门就会"连不上但原因不明"）。
# 判定要严格：只看**收包**相关日志（放行/校验/cid），启动行的 "knockd start" 不算证据 ——
# 否则 knockd 一崩、grep 命中启动行就会给出假 [OK]。
if [ "$KNOCK" = "1" ]; then
    log "敲门验证（重建连接时应重新发 SPA 包）"
    # 存活判定不能用 pgrep -x：Linux /proc/pid/comm 截断到 15 字符，
    # "q_service_knockd"(16) 会被截成 "q_service_knock"，-x 永远匹配不上 → 假阴性，
    # 曾据此误判「knockd 一进事件循环就崩」。用 ps 全命令行匹配。
    if ! ps -ef | grep "[q]_service_knockd" >/dev/null 2>&1; then
        printf '\033[31m[FAIL]\033[0m knockd 进程已不在 —— SPA 敲门路径未通过验证\n'
        tail -5 "$CONF_DIR/knockd.log" 2>/dev/null
    else
        # 只看**收包**相关日志：授权成功(AUDIT)或校验失败都算"包到了且格式正确"，
        # 启动行的 "knockd start" 不算证据。
        n=$(grep -cE 'SPA 授权成功|SPA 校验失败' "$CONF_DIR/knockd.log" 2>/dev/null || echo 0)
        if [ "${n:-0}" -gt 0 ]; then
            printf '\033[32m[OK]\033[0m knockd 收到 %s 条 SPA 记录：重建连接时确已重新敲门\n' "$n"
            grep -E 'SPA 授权成功|SPA 校验失败' "$CONF_DIR/knockd.log" | tail -4
        else
            printf '\033[33m[--]\033[0m knockd 存活但无收包记录：确认用例是否真的触发了重建（需跑用例3）\n'
        fi

        # 真开门模式下的**硬证据**：nft 放行集合里必须出现带 TTL 的元素。
        # 只查日志不够 —— 曾出现「日志记 open_ok、nft 却返回失败」的相反情况
        # （seccomp 拦了 libmnl 的 pselect6），日志与事实背离，必须落到集合本身。
        if [ "$KNOCK_NFT" = "1" ] && command -v nft >/dev/null 2>&1; then
            tbl=$(sed -n 's:.*<nft_table>\(.*\)</nft_table>.*:\1:p' "$CONF_DIR/knockd_conf.xml" 2>/dev/null | head -1)
            st4=$(sed -n 's:.*<nft_set>\(.*\)</nft_set>.*:\1:p' "$CONF_DIR/knockd_conf.xml" 2>/dev/null | head -1)
            if [ -n "$tbl" ] && [ -n "$st4" ]; then
                # 读 nft 集合需 CAP_NET_ADMIN，普通用户会 "Operation not permitted"，
                # 故先直连、失败再退回 sudo -n。
                # 注意：grep -c 无匹配时退出码为 1，若写成 `|| echo 0` 会追加第二个 0，
                # 变量变成 "0\n0" → [: integer expression expected。用 `|| true` 兜底。
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

log "完成（服务端日志：${CONF_DIR}/*.log）"
