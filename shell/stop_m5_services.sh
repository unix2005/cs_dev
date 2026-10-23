#!/usr/bin/env bash
# =============================================================================
# stop_m5_services.sh —— 停止 run_m5_services.sh 启动的全部常驻服务
# -----------------------------------------------------------------------------
# 注意进程名截断：comm 只保留 15 字符，q_service_knockd(16) → q_service_knock，
# 所以 knockd 必须用 pkill -x q_service_knock，不能用 -f（会误杀 ssh 命令行）。
# 用法：bash shell/stop_m5_services.sh
# =============================================================================
set -uo pipefail
SOFT_HOME="${SOFT_HOME:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
CONF_DIR="${CONF_DIR:-/tmp/vmsconf}"

echo "停止 M5 服务..."
pkill -x q_service_login 2>/dev/null
pkill -x q_service_bizd  2>/dev/null
pkill -x q_service_netd  2>/dev/null
pkill -x q_service_push  2>/dev/null
pkill -x q_service_knock 2>/dev/null   # 截断后的名字

# 普通用户读不了 nft 集合，"you must be root" 时交给 sudo
sleep 0.3
echo "残留进程（应为空）："
ps -eo comm,args 2>/dev/null | grep -E 'q_service_' | grep -v grep || echo "  无"

rm -f "$CONF_DIR/bizd.sock" "$CONF_DIR/pushd.sock"
echo "已清理 UDS 套接字。如需放行条目也清掉：sudo nft delete element inet filter allow4 '{ <ip> }' （视 knockd 配置）"
