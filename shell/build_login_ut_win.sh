#!/usr/bin/env bash
# =============================================================================
# build_login_ut_win.sh —— 编译「登录闭环联调程序」qc_login_ut 的 Windows 版本
# -----------------------------------------------------------------------------
# 产物：$(SOFT_HOME)/lib_src/l_4/q_clientcomm/ut/bin/qc_login_ut.exe
# 源码：$(SOFT_HOME)/lib_src/l_4/q_clientcomm/ut/qc_login_ut.c（三端共用同一份）
#
# 运行平台：Windows（Git Bash / MSYS2 环境）
# 工具链  ：LLVM-MinGW（D:\llvm-mingw-msvcrt-x86_64，与 win_vms_v1 的 vms_core.dll 同源）
#
# 用法：
#   bash shell/build_login_ut_win.sh              # 编译 x86_64 联调程序
#   bash shell/build_login_ut_win.sh --arch=aarch64
#
# 前置依赖：
#   - $(SOFT_HOME)/lib/libq*.a      由 shell/build_libsrc_win.sh 产出
#   - $(SOFT_HOME)/tongsuo/lib      由 shell/build_tongsuo_win.sh 产出
# 说明：
#   - 链接给的是 .a **全路径**：Windows ld 在同目录会优先吃 libq*.dll.a，
#     用 -lqxxx 会静默链到 DLL 而非静态库（Windows 无 rpath，运行期找不到 DLL）。
#   - 与 Linux/macOS 侧 `make ut_login` 产出的是同一个 C 源文件，行为一致。
# =============================================================================
set -uo pipefail

ARCH=x86_64
while [ $# -gt 0 ]; do
    case "$1" in
        --arch=*)  ARCH="${1#--arch=}" ;;
        -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
        *) echo "未知参数: $1（--help 查看用法）" >&2; exit 1 ;;
    esac
    shift
done

SOFT_HOME="${SOFT_HOME:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
case "$ARCH" in
    x86_64)  TRIPLE=x86_64-w64-mingw32 ;;
    aarch64) TRIPLE=aarch64-w64-mingw32 ;;
    i686)    TRIPLE=i686-w64-mingw32 ;;
    *) echo "不支持的 --arch=$ARCH" >&2; exit 1 ;;
esac

LLVM_MINGW="${LLVM_MINGW:-/d/llvm-mingw-msvcrt-x86_64}"
CC_BIN="$LLVM_MINGW/bin/$TRIPLE-gcc"
[ -x "$CC_BIN" ] || { echo "[login-ut-win] 编译器不存在: $CC_BIN" >&2; exit 1; }

UT_DIR="$SOFT_HOME/lib_src/l_4/q_clientcomm/ut"
SRC="$UT_DIR/qc_login_ut.c"
OUT="$UT_DIR/bin/qc_login_ut.exe"
LIBDIR="$SOFT_HOME/lib"
TONGSUO_LIB="$SOFT_HOME/tongsuo/lib"

for f in "$SRC" "$LIBDIR/libqclientcomm.a" "$TONGSUO_LIB/libcrypto.a"; do
    [ -f "$f" ] || { echo "[login-ut-win] 缺失: $f" >&2; exit 1; }
done

# 原生 PE 编译器不认 POSIX 路径：统一转成 Windows 风格传给 -I/-L/链接项
W_SOFT_HOME="$(cd "$SOFT_HOME" && pwd -W)"
W_LIB="$W_SOFT_HOME/lib"
W_TONGSUO_LIB="$W_SOFT_HOME/tongsuo/lib"

# 依赖闭包：高级在前、低级在后（GNU/ld.lld 单遍解析，顺序反了会报未决符号）
STATIC_LIBS=""
for m in qclientcomm qchan qspa qsec qcodec qnet qcrypto qrand qreactor qutil qmem qlog; do
    STATIC_LIBS="$STATIC_LIBS $W_LIB/lib$m.a"
done

mkdir -p "$UT_DIR/bin"

echo "[login-ut-win] 编译 $SRC"
# shellcheck disable=SC2086
"$CC_BIN" -O2 -Wall -Wextra \
    -I"$W_SOFT_HOME/lib_src/l_4/q_clientcomm" \
    -I"$W_SOFT_HOME/include" \
    -I"$W_SOFT_HOME/tongsuo/include" \
    "$SRC" -o "$OUT" \
    $STATIC_LIBS \
    "$W_TONGSUO_LIB/libcrypto.a" \
    -lws2_32 -lbcrypt -lcrypt32 -lpthread

rc=$?
if [ $rc -ne 0 ]; then
    echo "[login-ut-win] 编译失败（exit=$rc）" >&2
    exit $rc
fi

echo "[login-ut-win] 产物: $OUT"
echo
echo "用法示例（需先启动 netd + bizd + login）："
echo "  $OUT --host 127.0.0.1 --port 18099 --user qiaoshui --pass 123456 \\"
echo "       --pub <netd 的 netd_sm2.pub>"
exit 0
