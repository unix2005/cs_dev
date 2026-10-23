#!/usr/bin/env bash
# =============================================================================
# build_vmscore_win.sh —— 编译 WinUI3 客户端的 native 门面 vms_core.dll
# -----------------------------------------------------------------------------
# 源码：$(SOFT_HOME)/soft_src/win_vms_v1/native/{include/vms_api.h, src/vms_core.c}
# 产物：$(SOFT_HOME)/soft_src/win_vms_v1/native/build/{vms_core.dll, libvms_core.a}
#
# 运行平台：Windows（Git Bash / MSYS2 环境）
# 工具链  ：LLVM-MinGW（D:\llvm-mingw-msvcrt-x86_64，与既有 vms_core.dll 同源）
#
# 用法：
#   bash shell/build_vmscore_win.sh               # 编译 x86_64（默认）
#   bash shell/build_vmscore_win.sh --mock-only   # 不链 qc_*（退回纯 Mock 形态）
#
# 与纯 Mock 形态的差异（M1 起）：
#   链接 libqclientcomm 及其闭包 + Tongsuo libcrypto，使 vms_login/logout/heartbeat
#   真正走 敲门 -> SM2 握手 -> HELLO -> SALT -> PBKDF2-SM3 -> LOGIN 全链路。
#   DLL 体积由 ~200KB 增至 ~7MB（静态链入国密库），换来的好处是**无外部 DLL 依赖**
#   （Windows 无 rpath，动态链 libq*.dll 时必须把它们拷到同目录）。
#
# 前置依赖：
#   - $(SOFT_HOME)/lib/libq*.a   由 shell/build_libsrc_win.sh 产出
#   - $(SOFT_HOME)/tongsuo/lib   由 shell/build_tongsuo_win.sh 产出
# =============================================================================
set -uo pipefail

MOCK_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --mock-only) MOCK_ONLY=1 ;;
        -h|--help)   sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "未知参数: $1（--help 查看用法）" >&2; exit 1 ;;
    esac
    shift
done

SOFT_HOME="${SOFT_HOME:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
NATIVE="$SOFT_HOME/soft_src/win_vms_v1/native"

LLVM_MINGW="${LLVM_MINGW:-/d/llvm-mingw-msvcrt-x86_64}"
CC_BIN="$LLVM_MINGW/bin/x86_64-w64-mingw32-gcc"
[ -x "$CC_BIN" ] || { echo "[vmscore] 编译器不存在: $CC_BIN" >&2; exit 1; }

# 原生 PE 编译器不认 POSIX 路径：统一转 Windows 风格再传给 -I / 链接项
W_SOFT_HOME="$(cd "$SOFT_HOME" && pwd -W)"
W_LIB="$W_SOFT_HOME/lib"
W_TONGSUO_LIB="$W_SOFT_HOME/tongsuo/lib"
W_CJSON="$W_SOFT_HOME/cjson"
# v7：vms_core 经 l_4 qc_conf_* -> l_1 q_xcfg 读 XML 配置，需 libxml2 及其传递依赖
# **静态链**（-Bstatic）：MSYS2 包的 lib/ 下同时有 .a 与 .dll.a，而 MinGW 的 -lfoo
# 优先选 libfoo.dll.a —— 动态方案会让 vms_core.dll 依赖 libxml2-2.dll，
# 少带一个 DLL 的表现是"vms_core.dll 加载失败"，错误信息完全不提 libxml2，极难排查。
# 客户端是要分发到用户机器上的，宁可 DLL 大 2MB 也不要这种坑。
W_LIBXML_DIR="$W_SOFT_HOME/open_src/_deps/winxml/mingw64"
XML_LIBS="-L$W_LIBXML_DIR/lib -Wl,-Bstatic -lxml2 -lz -llzma -liconv -lcharset -Wl,-Bdynamic"

# cJSON：解析下游业务响应（M2 起 vms_get_stats 走真实查询）
[ -f "$SOFT_HOME/cjson/lib/libcjson.a" ] || { echo "[vmscore] 缺失 cjson/lib/libcjson.a（先跑 build_cjson_win.sh）" >&2; exit 1; }
CJSON_A="$W_CJSON/lib/libcjson.a"
CJSON_INC="-I$W_CJSON/include"

mkdir -p "$NATIVE/build"

# stat64 垫片：MSYS2 预编译 libxml2.a(catalog.o) 引用裸 stat64，llvm-mingw 只有
# _stat64（详见 open_src/_deps/winshim/stat64_shim.c 头部注释）。仅 MOCK_ONLY=0
# 需要（--mock-only 不链 libxml2）。编到临时目录，不进源码树。
STAT64_SHIM_O=""
if [ "$MOCK_ONLY" -eq 0 ]; then
    SHIM_SRC="$SOFT_HOME/open_src/_deps/winshim/stat64_shim.c"
    [ -f "$SHIM_SRC" ] || { echo "[vmscore] 缺失 $SHIM_SRC" >&2; exit 1; }
    STAT64_SHIM_O="$NATIVE/build/stat64_shim.o"
    "$CC_BIN" -O2 -c "$SHIM_SRC" -o "$STAT64_SHIM_O" || {
        echo "[vmscore] stat64 垫片编译失败" >&2; exit 1; }
fi

STATIC_LIBS=""
if [ "$MOCK_ONLY" -eq 0 ]; then
    for m in qclientcomm qchan qspa qsec qcodec qnet qcrypto qrand qreactor qutil qmem qlog qxcfg; do
        [ -f "$SOFT_HOME/lib/lib$m.a" ] || { echo "[vmscore] 缺失 lib$m.a（先跑 build_libsrc_win.sh）" >&2; exit 1; }
        STATIC_LIBS="$STATIC_LIBS $W_LIB/lib$m.a"
    done
    [ -f "$SOFT_HOME/tongsuo/lib/libcrypto.a" ] || { echo "[vmscore] 缺失 Tongsuo libcrypto.a" >&2; exit 1; }
    STATIC_LIBS="$STATIC_LIBS $W_TONGSUO_LIB/libcrypto.a"
fi

echo "[vmscore] 编译 vms_core.dll（MOCK_ONLY=$MOCK_ONLY）"
# shellcheck disable=SC2086
"$CC_BIN" -shared -O2 -Wall -Wextra -DVMS_BUILD_DLL \
    -I"$NATIVE/include" -I"$W_SOFT_HOME/include" -I"$W_SOFT_HOME/tongsuo/include" $CJSON_INC \
    "$NATIVE/src/vms_core.c" \
    -o "$NATIVE/build/vms_core.dll" \
    -Wl,--out-implib,"$NATIVE/build/libvms_core.a" \
    $STATIC_LIBS $CJSON_A $XML_LIBS $STAT64_SHIM_O \
    -lws2_32 -lbcrypt -lcrypt32 -lpthread -static-libgcc

rc=$?
[ $rc -ne 0 ] && { echo "[vmscore] 编译失败（exit=$rc）" >&2; exit $rc; }

echo "[vmscore] 产物:"
ls -la "$NATIVE/build/vms_core.dll" "$NATIVE/build/libvms_core.a"
echo
echo "导出符号自检："
"$LLVM_MINGW/bin/x86_64-w64-mingw32-nm" --defined-only "$NATIVE/build/vms_core.dll" 2>/dev/null \
    | grep -E ' T vms_(login|logout|heartbeat|connect|create)' || echo "  （nm 不可用，跳过）"
exit 0
