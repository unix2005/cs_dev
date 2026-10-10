#!/usr/bin/env bash
#
# =====================================================================
#  CS_DEV 分层依赖静态检查（CI 必过项）
#  对应规则：AI_SKILL_RULE.md §3.3 / §3.3(9) / 第 240 行
#
#  静态扫描 lib_src 下所有 q_* 模块，上报以下违规：
#    1) 反向依赖：l_N 引用 l_M（M > N）
#    2) 同层互引：l_N 内部不同模块互相引用
#    3) 库名含下划线：Makefile 中 -lq_* 或 TARGET=libq_*.a/.so 含下划线
#    4) Makefile 缺失下层依赖：源码 #include 了 q_*.h 却无对应 -lq*
#    附加：循环依赖检测（禁止任意引用环）
#
#  依赖推导：
#    - 模块目录名 q_xxx  ->  派生库名 libq + (去 q_ 前缀、去下划线)，如 q_log -> libqlog -> -lqlog
#    - 源码中 #include <q_xxx.h> / "q_xxx.h" 且 q_xxx 为已知模块 -> 声明对该模块的依赖
#    - Makefile 中 LIBS += -lqxxx  -> 链接期依赖
#    （模块自身的 q_xxx.h、以及本模块内部头 q_xxx_*.h 不计入跨模块依赖）
#
#  用法：
#    shell/check_layer.sh [lib_src目录]
#  环境变量 SOFT_HOME 可覆盖项目根（默认由脚本位置推导）
#
#  退出码：0=通过；1=存在违规
# =====================================================================

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOFT_HOME="${SOFT_HOME:-$(cd "$SCRIPT_DIR/.." && pwd)}"
LIB_SRC="${1:-$SOFT_HOME/lib_src}"

if [ ! -d "$LIB_SRC" ]; then
    echo "ERROR: lib_src 目录不存在: $LIB_SRC" >&2
    exit 1
fi

# ------------------------- 发现所有 q_* 模块 -------------------------
declare -A MOD_LAYER MOD_LIBNAME REV_LIB
MODULES=()
while IFS= read -r mdir; do
    [ -z "$mdir" ] && continue
    mname="$(basename "$mdir")"
    layer_dir="$(basename "$(dirname "$mdir")")"
    layer="${layer_dir#l_}"
    case "$layer" in
        ''|*[!0-9]*) echo "WARN: 无法识别层级目录，已忽略: $layer_dir" >&2; continue;;
    esac
    MOD_LAYER[$mname]="$layer"
    core="${mname#q_}"; core="${core//_/}"      # 去 q_ 前缀与全部下划线
    MOD_LIBNAME[$mname]="q${core}"             # 即 -l 之后的部分（libqxxx -> qxxx）
    REV_LIB["q${core}"]="$mname"
    MODULES+=("$mname")
done < <(find "$LIB_SRC" -mindepth 2 -maxdepth 2 -type d -name 'q_*' | sort)

if [ ${#MODULES[@]} -eq 0 ]; then
    echo "WARN: 在 $LIB_SRC 下未发现任何 l_N/q_* 模块" >&2
fi

is_module() { [ -n "${MOD_LAYER[$1]:-}" ]; }   # $1=candidate 已知模块?

# ------------------------- 违规收集容器 -------------------------
REV_DEP=()      # 反向依赖
SAME_LAYER=()   # 同层互引
UNDERSCORE=()   # 库名含下划线
MISSING_DEP=()  # Makefile 缺失下层依赖
UNKNOWN=()      # 未知 -l / 未知头（仅告警，不计入失败）

# 关系图（基于 include 的硬依赖，用于循环检测）
declare -A GRAPH
for m in "${MODULES[@]}"; do GRAPH[$m]=""; done

# ------------------------- 逐模块扫描 -------------------------
for m in "${MODULES[@]}"; do
    layer="${MOD_LAYER[$m]}"
    mdir="$LIB_SRC/l_${layer}/$m"
    [ -d "$mdir" ] || continue

    # ---- 1) 解析源码 #include 的 q_*.h 依赖（带文件:行号）----
    inc_deps=""
    while IFS= read -r line; do
        f="${line%%:*}"
        rest="${line#*:}"; ln="${rest%%:*}"
        hdr="$(printf '%s' "$line" | sed -nE 's/.*[<"](q_[A-Za-z0-9_]*\.h)[>"].*/\1/p')"
        [ -z "$hdr" ] && continue
        cand="${hdr%.h}"
        [ "$cand" = "$m" ] && continue            # 自身头文件
        if is_module "$cand"; then
            inc_deps="$inc_deps $cand|$f:$ln"
            GRAPH[$m]="${GRAPH[$m]} $cand"
        fi
    done < <(grep -rnE --include='*.c' --include='*.h' \
                -e '#[[:space:]]*include[[:space:]]*[<"](q_[A-Za-z0-9_]*\.h)[>"]' \
                "$mdir" 2>/dev/null)

    # ---- 2) 解析 Makefile 的 -lq* 依赖（带 Makefile:行号）----
    lib_deps=""
    while IFS= read -r line; do
        ln="${line%%:*}"; body="${line#*:}"
        for tok in $(printf '%s' "$body" | grep -oE '\-lq[A-Za-z0-9_]+'); do
            arg="${tok#-l}"
            if printf '%s' "$arg" | grep -q '_'; then
                UNDERSCORE+=("$m : Makefile:$ln  $tok （库名/链接名禁止含下划线，应写为 -l${arg//_/}）")
                continue
            fi
            dep="${REV_LIB[$arg]:-}"
            if [ -z "$dep" ]; then
                UNKNOWN+=("$m : Makefile:$ln  $tok （无法映射到已知 q_* 模块库名）")
            else
                lib_deps="$lib_deps $dep"
            fi
        done
    done < <(grep -nE '\-lq[A-Za-z0-9_]*' "$mdir/Makefile" 2>/dev/null)

    # ---- 3) 解析 TARGET / TARGET_SO 是否含下划线 ----
    while IFS= read -r line; do
        ln="${line%%:*}"; body="${line#*:}"
        if printf '%s' "$body" | grep -qE 'libq_[A-Za-z0-9_]*\.(a|so)'; then
            UNDERSCORE+=("$m : Makefile:$ln  $body （产物库名禁止含下划线）")
        fi
    done < <(grep -nE '^[[:space:]]*(TARGET|TARGET_SO)[[:space:]]*[:?]?=' "$mdir/Makefile" 2>/dev/null)

    # ---- 4) 判定违规：反向 / 同层 / 缺失 ----
    for entry in $inc_deps; do
        dep="${entry%%|*}"; loc="${entry#*|}"
        dl="${MOD_LAYER[$dep]}"
        if [ "$dl" -gt "$layer" ]; then
            REV_DEP+=("$m (l_$layer) -> $dep (l_$dl)  @ $loc")
        elif [ "$dl" -eq "$layer" ] && [ "$dep" != "$m" ]; then
            SAME_LAYER+=("$m (l_$layer) -> $dep (l_$dl)  @ $loc")
        fi
    done

    for entry in $inc_deps; do
        dep="${entry%%|*}"; loc="${entry#*|}"
        if ! printf '%s' " $lib_deps " | grep -q " $dep "; then
            MISSING_DEP+=("$m : 引用 $dep 头(@ $loc) 但 Makefile 未含 -l${MOD_LIBNAME[$dep]}")
        fi
    done
done

# ------------------------- 循环依赖检测 -------------------------
declare -A COLOR
has_cycle=0
cycle_reports=()
dfs() {
    local u="$1" v
    COLOR[$u]=1
    for v in ${GRAPH[$u]}; do
        [ -z "$v" ] && continue
        if [ "${COLOR[$v]:-0}" = "0" ]; then
            dfs "$v"
        elif [ "${COLOR[$v]}" = "1" ]; then
            has_cycle=1
            cycle_reports+=("$u -> $v （形成引用环）")
        fi
    done
    COLOR[$u]=2
}
for m in "${MODULES[@]}"; do
    [ "${COLOR[$m]:-0}" = "0" ] && dfs "$m"
done

# ------------------------- 汇总输出 -------------------------
HR="======================================================================"
echo "$HR"
echo " CS_DEV 分层依赖静态检查 (check_layer.sh)"
echo " 扫描目录 : $LIB_SRC"
echo " 模块数量 : ${#MODULES[@]}  ($(printf '%s ' "${MODULES[@]}"))"
echo "$HR"

print_section() {  # $1=标题  $2=数组名
    local title="$1" name="$2" i
    local -n arr="$name"
    echo ""
    echo "[$title]"
    if [ ${#arr[@]} -eq 0 ]; then
        echo "  (无)"
    else
        for i in "${arr[@]}"; do echo "  - $i"; done
    fi
}

print_section "1/4 反向依赖  l_N -> l_M (M>N)"              REV_DEP
print_section "2/4 同层互引  l_N 内不同模块互相引用"         SAME_LAYER
print_section "3/4 库名含下划线 (-lq_* / TARGET=libq_*)"     UNDERSCORE
print_section "4/4 Makefile 缺失下层依赖 (include 无 -lq*)"  MISSING_DEP
if [ "$has_cycle" = "1" ]; then
    echo ""
    echo "[附加] 循环依赖"
    for i in "${cycle_reports[@]}"; do echo "  - $i"; done
fi
if [ ${#UNKNOWN[@]} -gt 0 ]; then
    echo ""
    echo "[提示] 无法识别的 -l / 头（未计入失败，请人工确认）"
    for i in "${UNKNOWN[@]}"; do echo "  - $i"; done
fi

err_total=$(( ${#REV_DEP[@]} + ${#SAME_LAYER[@]} + ${#UNDERSCORE[@]} + ${#MISSING_DEP[@]} + has_cycle ))
echo ""
echo "$HR"
if [ "$err_total" -gt 0 ]; then
    echo " 结果: 失败 —— 共 $err_total 项分层违规（CI 门禁未通过）"
    echo "$HR"
    exit 1
else
    echo " 结果: 通过 —— 未发现分层依赖违规"
    echo "$HR"
    exit 0
fi
