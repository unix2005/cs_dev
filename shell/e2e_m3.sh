#!/usr/bin/env bash
# ==============================================================================
#  M3 端到端联调脚本（Linux / WSL）
#
#  链路：
#    qc_login_ut ─(TCP + SM2 握手 + SM4-GCM)─> netd:18099
#                ─(Unix Socket TLV)─> bizd
#                ─(TCP JSON)─> login:9002（登录）/ biz:9003（业务）
#
#  验证内容：登录取 Token 后，用同一个会话连续发 BIZ_QUERY：
#    stats / companies / vehicles（含组织子树过滤 + 分页），
#    并把下游 JSON 原文打出来对账（data 域字段 = 库表列名的 camelCase）。
#
#  用法（WSL 里）：
#     bash shell/e2e_m3.sh
#     ONLY=vehicles BIZ_PARAM='{"org_id":"14","recursive":1}' LIMIT=5 bash shell/e2e_m3.sh
#
#  环境变量（都有默认值，按需覆盖）：
#     SOFT_HOME  项目根（WSL 下默认 /mnt/e/win_e/cs_dev）
#     CONF_DIR   配置与密钥目录（默认 /tmp/vmsconf，含 netd_conf.xml / bizd_conf.xml / *.pub）
#     LIB_DIR    Linux 版 libq*.a 目录（默认 /tmp/cdlib_lx）
#     DEP_HOME   依赖根（默认 /tmp/lxdeps_root）
#     USER_NAME / USER_PASS / SALT_HEX / ITER   登录凭据
#     ONLY       只跑某一个 biz_type；BIZ_PARAM / OFFSET / LIMIT 透传给 UT
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

ONLY="${ONLY:-}"
BIZ_PARAM="${BIZ_PARAM:-}"
OFFSET="${OFFSET:-0}"
LIMIT="${LIMIT:-5}"

# libxml2 / libseccomp 等无 sudo 时用 apt-get download + dpkg-deb -x 解出来的目录
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

# ---------------------------------------------------------------- 1. 编译 UT
log "编译 l_4 与 qc_login_ut"
UT="$SOFT_HOME/lib_src/l_4/q_clientcomm/ut/bin/qc_login_ut"
if ! ( cd "$SOFT_HOME/lib_src/l_4/q_clientcomm" &&
       make ut_login LIB="$LIB_DIR" DEP_HOME="$DEP_HOME" TONGSUO_HOME="$TONGSUO_HOME" \
            >/tmp/e2e_ut_build.log 2>&1 ); then
    tail -25 /tmp/e2e_ut_build.log
    die "UT 编译失败（详见 /tmp/e2e_ut_build.log）"
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

# ---------------------------------------------------------------- 3. 跑用例
run_case() {
    local btype="$1" param="$2" extra="$3"
    local -a args
    args=(--host 127.0.0.1 --port "$NETD_PORT" --user "$USER_NAME" --pass "$USER_PASS"
          --pub "$CONF_DIR/netd_sm2.pub" --biz-type "$btype")
    [ -n "$param" ] && args+=(--biz-param "$param")
    [ -n "$extra" ] && args+=($extra)

    log "BIZ_QUERY ${btype}  param=${param:-(空)} ${extra}"
    "$UT" "${args[@]}" 2>&1 | grep -vE '^\s*$' | tail -14

    # netd 有"每源 IP 新建连接 5/s"的限流（NETD_PERIP_NEW），用例之间必须留出间隔，
    # 否则后续连接的 SM2 握手会被直接拒绝（表现为 qc_connect 失败，很容易误判成协议问题）。
    sleep 1.2
}

PAGING="--offset ${OFFSET} --limit ${LIMIT}"

if [ -z "$ONLY" ] || [ "$ONLY" = "stats" ]; then
    run_case stats "" ""
fi
if [ -z "$ONLY" ] || [ "$ONLY" = "companies" ]; then
    if [ -n "$BIZ_PARAM" ]; then
        run_case companies "$BIZ_PARAM" "$PAGING"
    else
        run_case companies "" "$PAGING"
        run_case companies '{"parent_org_id":"14","recursive":1}' "$PAGING"
    fi
fi
if [ -z "$ONLY" ] || [ "$ONLY" = "vehicles" ]; then
    if [ -n "$BIZ_PARAM" ]; then
        run_case vehicles "$BIZ_PARAM" "$PAGING"
    else
        run_case vehicles "" "$PAGING"
        run_case vehicles '{"org_id":"14","recursive":1}' "$PAGING"
        run_case vehicles '{"org_id":"14","recursive":0}' "$PAGING"
        # ids 精确过滤：主键是"车牌-颜色"这类非数字串，不能写死 —— 先向 biz 探一页取真实主键，
        # 再用它过滤。这条用例同时证明"列表返回的主键"与"按主键过滤认的主键"是同一套。
        IDS_PARAM="$(python3 - "$SOFT_HOME" <<'PY'
import json, sys
sys.path.insert(0, sys.argv[1] + "/shell")
import biz_probe
rsp = biz_probe.call("127.0.0.1", 9003, biz_probe.build_req("vehicles", "", 0, 3, "11" * 32))
print(json.dumps({"ids": [it["vehicleId"] for it in rsp["data"]["items"]]}, ensure_ascii=False))
PY
)" || IDS_PARAM='{"ids":["京Z38406-1","津N80851-1","冀P32707-1"]}'
        run_case vehicles "$IDS_PARAM" "$PAGING"
    fi
fi

log "完成（服务端日志：${CONF_DIR}/*.log）"
