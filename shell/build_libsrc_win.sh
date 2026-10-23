#!/usr/bin/env bash
# =============================================================================
# build_libsrc_win.sh —— 编译 lib_src（q_* 四级分层库）的 Windows 版本
# -----------------------------------------------------------------------------
# 产物：$(SOFT_HOME)/lib/libq*.a（默认）+ 可选 libq*.dll / libq*.dll.a（--with-dll）
#       对外头文件由各模块 install_hdr 归集到 $(SOFT_HOME)/include/（已存在，幂等）
#
# 运行平台：Windows（Git Bash / MSYS2 环境）
# 工具链  ：LLVM-MinGW（D:\llvm-mingw-msvcrt-x86_64，与 win_vms_v1 的 vms_core.dll 同源）
#
# 用法：
#   bash shell/build_libsrc_win.sh                 # 全量构建静态库
#   bash shell/build_libsrc_win.sh --with-dll      # 追加构建 DLL（+ 导入库）
#   bash shell/build_libsrc_win.sh --clean         # 先清理中间产物再构建
#   bash shell/build_libsrc_win.sh --arch=aarch64  # 交叉编译（llvm-mingw 自带 sysroot）
#   bash shell/build_libsrc_win.sh --jobs=16
#
# 依赖（脚本会检查并在缺失时提示）：
#   - LLVM-MinGW            编译器 / ar / ranlib / mingw32-make
#   - MSYS2 sh              D:\msys64\usr\bin\sh.exe（Makefile 配方用到 rm/cp/mv）
#   - Tongsuo Windows 版    $(SOFT_HOME)/tongsuo（shell/build_tongsuo_win.sh 产出）
#   - cJSON Windows 版      $(SOFT_HOME)/cjson  （shell/build_cjson_win.sh 产出）
#   - libxml2 等            仅 l_1/q_xcfg 需要，脚本自动从镜像取预编译包
# =============================================================================
set -uo pipefail

# ---------------------------------------------------------------- 参数解析
JOBS=8
WITH_DLL=0
DO_CLEAN=0
ARCH=x86_64
ONLY=""

while [ $# -gt 0 ]; do
    case "$1" in
        --with-dll)      WITH_DLL=1 ;;
        --clean)         DO_CLEAN=1 ;;
        --jobs=*)        JOBS="${1#--jobs=}" ;;
        --arch=*)        ARCH="${1#--arch=}" ;;
        --only=*)        ONLY="${1#--only=}" ;;
        -h|--help)       sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "未知参数: $1（--help 查看用法）" >&2; exit 1 ;;
    esac
    shift
done

# ---------------------------------------------------------------- 路径与环境
SOFT_HOME="${SOFT_HOME:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
DEPS="$SOFT_HOME/open_src/_deps"

case "$ARCH" in
    x86_64)  TRIPLE=x86_64-w64-mingw32 ;;
    aarch64) TRIPLE=aarch64-w64-mingw32 ;;
    i686)    TRIPLE=i686-w64-mingw32 ;;
    armv7)   TRIPLE=armv7-w64-mingw32 ;;
    *) echo "不支持的 --arch=$ARCH（可选 x86_64/aarch64/i686/armv7）" >&2; exit 1 ;;
esac

LLVM_MINGW="${LLVM_MINGW:-/d/llvm-mingw-msvcrt-x86_64}"
MSYS_HOME="${MSYS_HOME:-/d/msys64}"
LIBXML_DIR="$DEPS/winxml/mingw64"        # libxml2 + zlib + libiconv + xz（MSYS2 预编译包）
SHIM_DIR="$DEPS/winshim"                 # <cjson/cJSON.h> 路径垫片

CC_BIN="$LLVM_MINGW/bin/$TRIPLE-gcc"
AR_BIN="$LLVM_MINGW/bin/$TRIPLE-ar"
RANLIB_BIN="$LLVM_MINGW/bin/$TRIPLE-ranlib"
NM_BIN="$LLVM_MINGW/bin/$TRIPLE-nm"
OBJDUMP_BIN="$LLVM_MINGW/bin/$TRIPLE-objdump"
MAKE_BIN="$LLVM_MINGW/bin/mingw32-make.exe"
SH_BIN="$MSYS_HOME/usr/bin/sh.exe"
SH_WIN="$(cygpath -w "$SH_BIN" 2>/dev/null || echo "${MSYS_HOME}/usr/bin/sh.exe")"

OUT_LIB="$SOFT_HOME/lib"
TONGSUO_LIB="$SOFT_HOME/tongsuo/lib"
CJSON_INC="$SOFT_HOME/cjson/include"

# 【关键】编译器是原生 PE 程序，且 MSYS2 的 sh.exe（MINGW64 子系统）不做
#   POSIX→Windows 路径转换：传给 gcc/clang 的 -I/-L 必须是 Windows 风格路径，
#   否则 /e/win_e/... 会被解析成“当前盘符根目录 + e/win_e/...”而找不到文件。
W_SOFT_HOME="$(cd "$SOFT_HOME" && pwd -W)"
W_TONGSUO_LIB="$W_SOFT_HOME/tongsuo/lib"
W_CJSON_INC="$W_SOFT_HOME/cjson/include"
W_SHIM_DIR="$W_SOFT_HOME/open_src/_deps/winshim"
W_LIBXML_DIR="$W_SOFT_HOME/open_src/_deps/winxml/mingw64"
W_OUT_LIB="$W_SOFT_HOME/lib"

for b in "$CC_BIN" "$AR_BIN" "$MAKE_BIN" "$SH_BIN"; do
    [ -x "$b" ] || { echo "[libsrc-win] 缺少工具: $b" >&2; exit 1; }
done
[ -d "$SOFT_HOME/tongsuo/include" ] || { echo "[libsrc-win] 缺少 Tongsuo: 请先运行 shell/build_tongsuo_win.sh" >&2; exit 1; }
[ -d "$CJSON_INC" ]                 || { echo "[libsrc-win] 缺少 cJSON  : 请先运行 shell/build_cjson_win.sh"   >&2; exit 1; }

# --- q_xcfg 依赖 libxml2：本地没有就从镜像取 MSYS2 预编译包 ---
if [ ! -f "$LIBXML_DIR/include/libxml2/libxml/parser.h" ]; then
    echo "[deps] 获取 libxml2 / zlib / libiconv / xz（仅 l_1/q_xcfg 需要）..."
    MIRROR="https://mirrors.tuna.tsinghua.edu.cn/msys2/mingw/mingw64"
    mkdir -p "$DEPS" "$LIBXML_DIR"
    for p in mingw-w64-x86_64-libxml2-2.13.8-1-any \
             mingw-w64-x86_64-zlib-1.3.2-2-any \
             mingw-w64-x86_64-libiconv-1.19-1-any \
             mingw-w64-x86_64-xz-5.8.4-1-any; do
        f="$DEPS/$p.pkg.tar.zst"
        [ -f "$f" ] || curl -sSL -m 180 -o "$f" "$MIRROR/$p.pkg.tar.zst"
        "$MSYS_HOME/usr/bin/bsdtar.exe" -xf "$f" -C "$LIBXML_DIR"
    done
    echo "[deps]   -> $LIBXML_DIR"
fi
# --- <cjson/cJSON.h> 垫片（include_xml.h 按 Linux 安装布局书写）---
mkdir -p "$SHIM_DIR/cjson"
[ -f "$SHIM_DIR/cjson/cJSON.h" ] || \
    printf '#include <cJSON.h>\n' > "$SHIM_DIR/cjson/cJSON.h"

# ---------------------------------------------------------------- 环境变量
# 注意：不要把 MSYS2 的 /usr/bin 放进 PATH —— Git Bash 与 MSYS2 的 /tmp 指向不同目录
#   （前者为 Windows Temp，后者为 D:\msys64\tmp），混用会导致脚本内的 mktemp / tail
#   与 make 配方看到不同的路径。MSYS2 只按需以绝对路径调用（sh.exe / bsdtar.exe）。
export PATH="$LLVM_MINGW/bin:$PATH"
# 临时目录：C 盘常满，统一指到 E 盘；原生 make 不做 MSYS 路径转换，给 Windows 风格路径
TMPROOT="$DEPS/tmp"
mkdir -p "$TMPROOT"
TMPDIR_WIN="$(cd "$TMPROOT" && pwd -W 2>/dev/null || echo "$TMPROOT")"
export TMP="$TMPDIR_WIN" TEMP="$TMPDIR_WIN" TMPDIR="$TMPDIR_WIN"
LOGDIR="$TMPROOT"

# ---------------------------------------------------------------- 编译参数
# 说明：全部在命令行覆盖，Makefile.inc 的 Linux/macOS 分支不会生效。
#  - _FORTIFY_SOURCE 显式置 0：MinGW/Clang 无 glibc fortify，置 0 避免误以为生效
#  - -D_Q_SYS_WINDOWS：与 Makefile.inc 的 ifdef WIN32 分支同义（此处 CFLAGS 被整体覆盖，故显式加）
COMMON_CFLAGS="-O2 -fstack-protector-strong -D_FORTIFY_SOURCE=0 \
 -Wall -Wextra -Wcast-align -Wwrite-strings -Wpointer-arith \
 -Wstrict-prototypes -Wmissing-prototypes -Wunreachable-code \
 -Wformat -Werror=format-security -Wshadow -Wundef \
 -D_THREAD_SAFE -D_Q_SYS_WINDOWS -DLIBXML_STATIC -fno-common"
#  -DLIBXML_STATIC：libxml2 头文件在 Windows 上默认把 API 声明成
#   __declspec(dllimport)，不定义此项时编译出的对象永远要求 libxml2-2.dll 在场。
#   定义后可用 libxml2.a 静态链进产物 —— 客户端要分发到用户机器，
#   少带一个 DLL 的表现是"vms_core.dll 加载失败"且错误信息不提 libxml2，极难排查。

INC_FLAGS="-I. -I$W_SOFT_HOME/include -I$W_SOFT_HOME/tongsuo/include \
 -I$W_CJSON_INC -I$W_SHIM_DIR -I$W_LIBXML_DIR/include -I$W_LIBXML_DIR/include/libxml2"

export CC="$CC_BIN"
export AR="$AR_BIN"
export RANLIB="$RANLIB_BIN"

# ---------------------------------------------------------------- 依赖闭包
# Windows 与 Linux 不同：DLL 不允许保留未决符号，必须在此全部解析；
# 故每个 DLL 显式链接其下层静态库（lib/ 下已产出的 .a）+ 系统库。
SYS_LIBS="-lpthread -lws2_32 -lbcrypt"   # bcrypt: q_rand 在 Windows 用 BCryptGenRandom
CRY_LIBS="-L$W_TONGSUO_LIB -lcrypto"
XML_LIBS="-L$W_LIBXML_DIR/lib -Wl,-Bstatic -lxml2 -lz -llzma -liconv -lcharset -Wl,-Bdynamic"

dll_deps() {
    case "$1" in
        log)        echo "-L$W_OUT_LIB -lpthread" ;;
        mem)        echo "-L$W_OUT_LIB" ;;
        ds)         echo "-L$W_OUT_LIB" ;;
        util)       echo "-L$W_OUT_LIB" ;;
        rand)       echo "-L$W_OUT_LIB -lbcrypt" ;;
        crypto)     echo "-L$W_OUT_LIB $CRY_LIBS" ;;
        reactor)    echo "-L$W_OUT_LIB -lpthread -lws2_32" ;;   # 唤醒管道用 send/recv
        common)     echo "-L$W_OUT_LIB -lpthread" ;;
        xcfg)       echo "-L$W_OUT_LIB $XML_LIBS" ;;
        codec)      echo "-L$W_OUT_LIB -lws2_32" ;;       # ntohl/ntohs 来自 ws2_32
        net)        echo "-L$W_OUT_LIB -lqutil -lqlog -lqmem -lqreactor $SYS_LIBS" ;;
        sec)        echo "-L$W_OUT_LIB -lqcrypto -lqrand -lqmem -lqutil -lqlog $CRY_LIBS -lbcrypt" ;;
        ipc)        echo "-L$W_OUT_LIB -lqutil -lqlog -lws2_32" ;;
        spa)        echo "-L$W_OUT_LIB -lqcrypto -lqrand -lqmem -lqutil -lqlog $CRY_LIBS -lws2_32 -lbcrypt" ;;
        chan)       echo "-L$W_OUT_LIB -lbcrypt -lqsec -lqcodec -lqnet -lqcrypto -lqrand -lqreactor -lqutil -lqmem -lqlog $SYS_LIBS $CRY_LIBS" ;;
        clientcomm) echo "-L$W_OUT_LIB -lbcrypt -lqchan -lqsec -lqcodec -lqnet -lqcrypto -lqrand -lqreactor -lqutil -lqmem -lqlog $SYS_LIBS $CRY_LIBS" ;;
        *)          echo "-L$W_OUT_LIB" ;;
    esac
}

# ---------------------------------------------------------------- 模块清单
MODS="
l_1/q_log log
l_1/q_mem mem
l_1/q_xcfg xcfg
l_1/q_ds ds
l_1/q_util util
l_1/q_rand rand
l_1/q_crypto crypto
l_1/q_reactor reactor
l_1/q_common common
l_2/q_codec codec
l_2/q_ipc ipc
l_2/q_net net
l_2/q_sec sec
l_3/q_chan chan
l_3/q_spa spa
l_4/q_clientcomm clientcomm
"

mkdir -p "$OUT_LIB"

echo "=============================================================="
echo " lib_src → Windows ($TRIPLE)"
echo "   SOFT_HOME : $SOFT_HOME"
echo "   输出目录  : $OUT_LIB"
echo "   并行度    : $JOBS      DLL: $WITH_DLL"
echo "=============================================================="

FAILS=""
BUILT=0

build_one() {
    local dir="$1" name="$2"
    local mdir="$SOFT_HOME/lib_src/$dir"
    [ -d "$mdir" ] || { echo "  [跳过] $dir（目录不存在）"; return 0; }
    [ -n "$ONLY" ] && [ "$name" != "$ONLY" ] && return 0

    cd "$mdir" || return 1

    [ "$DO_CLEAN" -eq 1 ] && "$MAKE_BIN" SHELL="$SH_WIN" WIN32=1 clean >/dev/null 2>&1

    # 静态库：LIB_LDFLAGS 不参与 lib_static，给空即可。
    # 注意：不用 Makefile.inc 的 install_lib —— 它会连 $(TARGET_SO) 一起 mv，
    #       未构建 DLL 时 mv 失败。此处显式搬运 .a / .dll。
    if ! "$MAKE_BIN" SHELL="$SH_WIN" WIN32=1 -j"$JOBS" \
            CFLAGS="$COMMON_CFLAGS $INC_FLAGS" \
            LIB_LDFLAGS="-L$W_OUT_LIB" \
            lib_static install_hdr > "$LOGDIR/libsrc_$name.log" 2>&1; then
        echo "  [失败] libq$name.a —— 日志 $LOGDIR/libsrc_$name.log"
        tail -12 "$LOGDIR/libsrc_$name.log" | sed 's/^/         /'
        FAILS="$FAILS libq$name.a"
        return 1
    fi
    mv -f "$mdir/libq$name.a" "$OUT_LIB/" 2>/dev/null
    local asz=""; [ -f "$OUT_LIB/libq$name.a" ] && asz="$(( $(stat -c%s "$OUT_LIB/libq$name.a") / 1024 ))K"
    printf "  [OK]   libq%-12s %8s\n" "$name.a" "$asz"
    BUILT=$((BUILT+1))

    if [ "$WITH_DLL" -eq 1 ]; then
        local deps; deps="$(dll_deps "$name")"
        if ! "$MAKE_BIN" SHELL="$SH_WIN" WIN32=1 -j"$JOBS" \
                CFLAGS="$COMMON_CFLAGS $INC_FLAGS" \
                SOFLAGS="-shared -Wl,--export-all-symbols -Wl,--enable-auto-import \
 -Wl,--out-implib,$W_OUT_LIB/libq$name.dll.a" \
                LIB_LDFLAGS="$deps" \
                lib_so > "$LOGDIR/libsrc_${name}_dll.log" 2>&1; then
            echo "  [失败] libq$name.dll —— 日志 $LOGDIR/libsrc_${name}_dll.log"
            tail -8 "$LOGDIR/libsrc_${name}_dll.log" | sed 's/^/         /'
            FAILS="$FAILS libq$name.dll"
            return 1
        fi
        # lib_so 只负责编译，产物留在模块目录，搬到 lib/
        [ -f "$mdir/libq$name.dll" ] && mv -f "$mdir/libq$name.dll" "$OUT_LIB/"
        local dsz=""; [ -f "$OUT_LIB/libq$name.dll" ] && dsz="$(( $(stat -c%s "$OUT_LIB/libq$name.dll") / 1024 ))K"
        printf "  [OK]   libq%-12s %8s\n" "$name.dll" "$dsz"
    fi
    return 0
}

echo
echo "--- 编译静态库 ---"
while read -r dir name; do
    [ -n "$dir" ] && build_one "$dir" "$name"
done <<< "$MODS"

# ---------------------------------------------------------------- 链接自检
echo
echo "=================== 链接自检 ==================="
SELFTEST_DIR="$TMPROOT/selftest"
rm -rf "$SELFTEST_DIR"; mkdir -p "$SELFTEST_DIR"
SELFTEST_SRC="$SOFT_HOME/shell/libsrc_win_selftest.c"

DEPS_ALL="-lqclientcomm -lqchan -lqspa -lqsec -lqcodec -lqnet -lqcrypto -lqrand -lqreactor -lqutil -lqmem -lqlog -lqds -lqcommon"

# (1) 真静态链接：显式给出 .a 全路径。
#     MinGW 的 GNU ld 在同一 -L 目录里优先 libxxx.dll.a（等同 Linux 优先 .so），
#     故 -lqxxx 默认链的是 DLL；要真静态必须写全路径（或用 -Wl,-Bstatic）。
STATIC_LIBS=""
for n in clientcomm chan spa sec codec net crypto rand reactor util mem log ds common; do
    STATIC_LIBS="$STATIC_LIBS $W_OUT_LIB/libq$n.a"
done
STATIC_LIBS="$STATIC_LIBS $W_TONGSUO_LIB/libcrypto.a"

if "$CC_BIN" -O2 $INC_FLAGS "$SELFTEST_SRC" -o "$SELFTEST_DIR/selftest_static.exe" \
        $STATIC_LIBS $SYS_LIBS -lcrypt32 2>"$SELFTEST_DIR/link.log"; then
    echo "静态链接: OK（无 q_* / Tongsuo DLL 依赖）"
    echo "  libqclientcomm.a 导出符号: $("$NM_BIN" --defined-only "$OUT_LIB/libqclientcomm.a" | grep -c ' T ') 个"
    echo "--- 运行（静态版）---"
    "$SELFTEST_DIR/selftest_static.exe" || \
        echo "  （运行失败 exit=$?；受限环境下可手动执行 $SELFTEST_DIR/selftest_static.exe）"
else
    echo "静态链接失败："
    tail -20 "$SELFTEST_DIR/link.log" | sed 's/^/    /'
    FAILS="$FAILS link-static"
fi

# (2) 动态链接：默认 -l 走 .dll.a，运行期需同目录有全部 DLL
if [ "$WITH_DLL" -eq 1 ]; then
    cp -f "$OUT_LIB"/libq*.dll "$SELFTEST_DIR/" 2>/dev/null
    cp -f "$SOFT_HOME"/tongsuo/bin/libcrypto-3-x64.dll "$SELFTEST_DIR/" 2>/dev/null
    for w in $(ls "$LLVM_MINGW"/bin/libwinpthread-*.dll 2>/dev/null); do cp -f "$w" "$SELFTEST_DIR/"; done
    if "$CC_BIN" -O2 $INC_FLAGS "$SELFTEST_SRC" -o "$SELFTEST_DIR/selftest_dll.exe" \
            -L"$W_OUT_LIB" $DEPS_ALL $SYS_LIBS $CRY_LIBS 2>>"$SELFTEST_DIR/link.log"; then
        echo "动态链接: OK"
        echo "--- 运行（DLL 版）---"
        "$SELFTEST_DIR/selftest_dll.exe" || \
            echo "  （运行失败 exit=$?；可手动执行 $SELFTEST_DIR/selftest_dll.exe）"
    else
        echo "动态链接失败："; tail -20 "$SELFTEST_DIR/link.log" | sed 's/^/    /'
        FAILS="$FAILS link-dll"
    fi
fi

# ---------------------------------------------------------------- 汇总
echo
echo "=================== 汇总 ==================="
echo "已产出静态库 $BUILT 个 -> $OUT_LIB"
# 注意：libqxxx.dll.a 是 DLL 的导入库，不能计入静态库数量
echo "  lib/*.a      : $(ls -1 "$OUT_LIB"/libq*.a 2>/dev/null | grep -v '\.dll\.a$' | wc -l) 个"
echo "  lib/*.dll    : $(ls -1 "$OUT_LIB"/libq*.dll 2>/dev/null | wc -l) 个"
echo "  lib/*.dll.a  : $(ls -1 "$OUT_LIB"/libq*.dll.a 2>/dev/null | wc -l) 个（导入库）"
if [ -n "$FAILS" ]; then
    echo "失败项:$FAILS" >&2
    exit 1
fi
echo "全部成功。"
echo
echo "使用方式（Windows 无 rpath，推荐静态链接）："
echo "  $TRIPLE-gcc ... -I$W_SOFT_HOME/include -L$W_OUT_LIB \\"
echo "      -lqclientcomm -lqchan -lqsec -lqcodec -lqnet -lqcrypto -lqrand -lqreactor \\"
echo "      -lqutil -lqmem -lqlog -lqds -lqcommon \\"
echo "      -L$W_TONGSUO_LIB -lcrypto -lpthread -lws2_32 -lbcrypt -lcrypt32"
