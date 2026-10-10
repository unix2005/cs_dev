#!/usr/bin/env bash
# =============================================
#  cs_dev 总构建脚本（shell/build.sh）
#  职责（AI_SKILL_RULE §3.1.7「三处登记」之第三处）：
#    1) 归集各模块公共头 q_*.h -> include/
#    2) 按 l_1 -> l_2 -> l_3 分层顺序构建并安装库到 lib/
#    3) 构建后运行 check_layer.sh 静态门禁
#  用法：bash shell/build.sh          # 全量构建
#        bash shell/build.sh q_crypto # 仅构建指定模块（其余仍按层序归集/门禁）
# =============================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export SOFT_HOME="$(cd "$SCRIPT_DIR/.." && pwd)"

LIB_SRC="$SOFT_HOME/lib_src"
INCLUDE="$SOFT_HOME/include"
LIB="$SOFT_HOME/lib"

mkdir -p "$LIB" "$INCLUDE"

# 分层编译顺序（同层内顺序无关，下层必须先于上层；新增模块须在此登记）
MODULES=(
  # l_1 基础层（零项目内依赖）
  l_1/q_log
  l_1/q_mem
  l_1/q_ds
  l_1/q_util
  l_1/q_rand
  l_1/q_crypto
  l_1/q_reactor
  l_1/q_xcfg
  # l_2 能力层（只能调用 l_1）
  l_2/q_net
  l_2/q_ipc
  l_2/q_codec
  l_2/q_sec
  # l_3 协议层（可调用 l_2 与 l_1；q_proto 已并入 q_chan，见 AI_SKILL_RULE §3.3.3 例外登记）
  l_3/q_chan
  l_3/q_spa    # SPA 单包授权（给 knockd；独立于 q_chan）
)

# 限定模式：仅构建指定模块（仍保持头文件归集与门禁）
if [ "$#" -gt 0 ]; then
  SELECTED=()
  for s in "$@"; do
    found=0
    for m in "${MODULES[@]}"; do
      if [ "$(basename "$m")" = "$s" ] || [ "$m" = "$s" ]; then SELECTED+=("$m"); found=1; fi
    done
    [ "$found" -eq 0 ] && { echo "未登记的模块: $s"; exit 1; }
  done
  MODULES=("${SELECTED[@]}")
fi

echo "== [1/3] 归集公共头文件 q_*.h -> $INCLUDE =="
for m in "${MODULES[@]}"; do
  name="$(basename "$m")"
  h="$LIB_SRC/$m/$name.h"
  if [ -f "$h" ]; then
    cp -f "$h" "$INCLUDE/" && echo "    $name.h"
  fi
done

echo "== [2/3] 分层构建（l_1 -> l_2 -> l_3）=="
for m in "${MODULES[@]}"; do
  echo ">> 构建 $m"
  ( cd "$LIB_SRC/$m" && make all ) || { echo "构建失败: $m"; exit 1; }
done

echo "== [3/3] 分层静态检查 check_layer =="
if bash --version | head -1 | grep -qE "version [45]"; then
  bash "$SCRIPT_DIR/check_layer.sh" || echo "!! check_layer 发现分层违规，请修复后重试"
else
  echo "（跳过）本地 bash 版本 < 4，check_layer.sh 需 bash>=4.3，请在 Linux/CI 运行"
fi

echo "== [4/4] 构建 soft_src 守护进程（仅 Linux，依赖 Tongsuo 已构建的库）=="
if [ "$(uname -s)" = "Linux" ]; then
  # 新增守护进程须在此登记；common 必须先于 security/* 构建
  SOFT_MODULES=(
    gtk_vms_v1/common
    gtk_vms_v1/security/netd
    gtk_vms_v1/security/bizd
    gtk_vms_v1/security/knockd
  )
  for m in "${SOFT_MODULES[@]}"; do
    echo ">> 构建 soft_src/$m"
    ( cd "$SOFT_HOME/soft_src/$m" && make all ) || { echo "构建失败: $m"; exit 1; }
  done
else
  echo "（跳过）非 Linux 环境不构建守护进程（需 Tongsuo 链接）"
fi

echo "== 构建完成：库产物位于 $LIB，可执行位于 $SOFT_HOME/bin =="
ls -1 "$LIB" 2>/dev/null || true
