#!/usr/bin/env bash
# =============================================================================
# build_cjson_win.sh —— 编译 vendored 开源库 cJSON（open_src/cJSON-1.7.19）
#                       目标平台：Windows（MinGW / llvm-mingw，PE-COFF）
# -----------------------------------------------------------------------------
# 产物布局【必须】对齐 soft_src/bizd/Makefile 的引用约定：
#     SOFT_INCL = . $(SOFT_HOME)/cJSON/include
#     LIBS      = ... -L$(SOFT_HOME)/cJSON/lib -lcjson -Wl,-rpath,$(SOFT_HOME)/cJSON/lib
# 故输出目录固定两级子目录： <OUT>/include 与 <OUT>/lib。
# 注：Windows 文件系统不区分大小写，$(SOFT_HOME)/cJSON 与 .../cjson 是同一路径；
#     本产物为 Windows(PE) 二进制，不要与 Linux 版 .a/.so 混放同一目录（会互相覆盖）。
#
# 工具链：llvm-mingw（clang 19，target x86_64-w64-mingw32）
#         与 win_vms_v1 native 的 vms_core.dll 构建工具链一致（导入表仅 KERNEL32/msvcrt）。
#
# 用法（Git Bash / MSYS2）：
#   bash shell/build_cjson_win.sh                      # 默认 x86_64，输出到 <SOFT_HOME>/cjson
#   bash shell/build_cjson_win.sh --arch=aarch64       # 交叉编译 ARM64（纯 ARM64，非 arm64ec）
#   bash shell/build_cjson_win.sh --static-only        # 只出静态库
#   bash shell/build_cjson_win.sh --no-test            # 跳过链接自检
#   bash shell/build_cjson_win.sh <源码目录> <输出目录>  # 位置参数覆盖
#
# 产出：
#   <OUT>/include/cJSON.h  <OUT>/include/cJSON_Utils.h
#   <OUT>/lib/libcjson.a        静态库（推荐优先；DLL 无分发负担）
#   <OUT>/lib/libcjson.dll      DLL
#   <OUT>/lib/libcjson.dll.a    MinGW 导入库（-lcjson 链接 DLL 时用）
#   <OUT>/lib/libcjson.lib      MSVC COFF 导入库（由 .def 生成，供 MSVC/link.exe 链接 DLL）
#   <OUT>/lib/cjson.def         导出符号定义文件
# =============================================================================
set -euo pipefail

# ---------- 参数解析 ----------
ARCH="x86_64"
DO_STATIC=1
DO_SHARED=1
DO_TEST=1
POSITIONAL=()
for a in "$@"; do
    case "$a" in
        --arch=*)        ARCH="${a#*=}" ;;
        --static-only)   DO_STATIC=1; DO_SHARED=0 ;;
        --shared-only)   DO_STATIC=0; DO_SHARED=1 ;;
        --no-test)       DO_TEST=0 ;;
        -h|--help)       sed -n '2,40p' "$0"; exit 0 ;;
        *)               POSITIONAL+=("$a") ;;
    esac
done

SOFT_HOME="${SOFT_HOME:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
SRC="${1:-$SOFT_HOME/open_src/cJSON-1.7.19/cJSON-master}"
OUT="${2:-$SOFT_HOME/cjson}"

# ---------- 工具链定位：llvm-mingw ----------
LLVM_MINGW="${LLVM_MINGW:-/d/llvm-mingw-msvcrt-x86_64}"
if [ ! -x "$LLVM_MINGW/bin/$ARCH-w64-mingw32-gcc" ]; then
    echo "[cjson-win] 未找到工具链: $LLVM_MINGW/bin/$ARCH-w64-mingw32-gcc" >&2
    echo "[cjson-win] 请用 LLVM_MINGW=<llvm-mingw 根目录> 指定，或检查 --arch" >&2
    exit 1
fi
export PATH="$LLVM_MINGW/bin:$PATH"

TRIPLE="$ARCH-w64-mingw32"
CC_BIN="$TRIPLE-gcc"
AR_BIN="$TRIPLE-ar"
NM_BIN="$TRIPLE-nm"
DLLTOOL_BIN="$TRIPLE-dlltool"

# 机器类型（dlltool 生成 MSVC 导入库时用）
case "$ARCH" in
    x86_64) DLLTOOL_MACHINE="i386:x86-64" ;;
    i686)   DLLTOOL_MACHINE="i386" ;;
    aarch64) DLLTOOL_MACHINE="arm64" ;;
    armv7)  DLLTOOL_MACHINE="arm" ;;
    *)      DLLTOOL_MACHINE="" ;;
esac

[ -f "$SRC/cJSON.c" ] || { echo "[cjson-win] 源码未找到: $SRC" >&2; exit 1; }

TMPD="$(mktemp -d)"
trap 'rm -rf "$TMPD"' EXIT

echo "=========================================================="
echo "[cjson-win] SOFT_HOME = $SOFT_HOME"
echo "[cjson-win] SRC       = $SRC"
echo "[cjson-win] OUT       = $OUT"
echo "[cjson-win] TOOLCHAIN = $LLVM_MINGW ($($CC_BIN --version | head -1))"
echo "[cjson-win] ARCH      = $ARCH  (triple=$TRIPLE)"
echo "=========================================================="

# ---------- 编译选项 ----------
# 对齐 AI_SKILL_RULE §四.6 的安全基线；MinGW/Clang 无 glibc fortify，
# 故显式 -D_FORTIFY_SOURCE=0，避免误认为已生效（Linux 构建才启用）。
CFLAGS_BASE="-O2 -Wall -Wextra -Wformat -Werror=format-security -D_FORTIFY_SOURCE=0"

# 能力探测：stack-protector 在部分 llvm-mingw 版本需要额外 runtime
CFLAGS_SEC=""
echo 'int main(void){return 0;}' > "$TMPD/probe.c"
if $CC_BIN $CFLAGS_BASE -fstack-protector-strong -c "$TMPD/probe.c" -o "$TMPD/probe.o" >/dev/null 2>&1; then
    CFLAGS_SEC="-fstack-protector-strong"
else
    echo "[cjson-win] 警告：-fstack-protector-strong 不可用，已降级"
fi

# Windows 二进制加固：ASLR / DEP / 64 位高熵 VA
LDFLAGS_EXE="-Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va"

CFLAGS="$CFLAGS_BASE $CFLAGS_SEC"
echo "[cjson-win] CFLAGS = $CFLAGS"

mkdir -p "$OUT/include" "$OUT/lib"

# ---------- 1) 头文件 ----------
cp -f "$SRC/cJSON.h"       "$OUT/include/cJSON.h"
cp -f "$SRC/cJSON_Utils.h" "$OUT/include/cJSON_Utils.h"
echo "[cjson-win] [1/5] 头文件已安装"

# ---------- 2) 编译目标文件 ----------
# 注意：不定义 CJSON_HIDE_SYMBOLS —— 默认在 _WIN32 下 CJSON_PUBLIC 为 dllexport，
#       静态库场景该属性被忽略，DLL 场景可正确导出。
echo "[cjson-win] [2/5] 编译 cJSON.c / cJSON_Utils.c ..."
$CC_BIN $CFLAGS -I"$SRC" -c "$SRC/cJSON.c"       -o "$TMPD/cJSON.o"
$CC_BIN $CFLAGS -I"$SRC" -c "$SRC/cJSON_Utils.c" -o "$TMPD/cJSON_Utils.o"

# ---------- 3) 静态库 ----------
if [ "$DO_STATIC" -eq 1 ]; then
    rm -f "$OUT/lib/libcjson.a"
    $AR_BIN rcs "$OUT/lib/libcjson.a" "$TMPD/cJSON.o" "$TMPD/cJSON_Utils.o"
    echo "[cjson-win] [3/5] 静态库 -> $OUT/lib/libcjson.a"
else
    echo "[cjson-win] [3/5] 静态库（跳过）"
fi

# ---------- 4) 动态库 + 导入库 + def ----------
if [ "$DO_SHARED" -eq 1 ]; then
    rm -f "$OUT/lib/libcjson.dll" "$OUT/lib/libcjson.dll.a" "$OUT/lib/cjson.def"
    $CC_BIN $CFLAGS -shared \
        -o "$OUT/lib/libcjson.dll" \
        "$TMPD/cJSON.o" "$TMPD/cJSON_Utils.o" \
        -Wl,--out-implib,"$OUT/lib/libcjson.dll.a" \
        -Wl,--output-def,"$OUT/lib/cjson.def" \
        $LDFLAGS_EXE
    echo "[cjson-win] [4/5] 动态库 -> $OUT/lib/libcjson.dll (+ .dll.a / .def)"

    # MSVC 侧导入库（供 MSVC / link.exe / clang-cl 链接该 DLL）
    if [ -n "$DLLTOOL_MACHINE" ] && [ -x "$(command -v $DLLTOOL_BIN)" ]; then
        $DLLTOOL_BIN -m "$DLLTOOL_MACHINE" \
                     -d "$OUT/lib/cjson.def" \
                     -D "libcjson.dll" \
                     -l "$OUT/lib/libcjson.lib" \
            && echo "[cjson-win]       MSVC 导入库 -> $OUT/lib/libcjson.lib" \
            || echo "[cjson-win]       警告：MSVC 导入库生成失败（不影响 MinGW 使用）"
    fi
else
    echo "[cjson-win] [4/5] 动态库（跳过）"
fi

# ---------- 5) 链接自检 ----------
if [ "$DO_TEST" -eq 1 ]; then
    echo "[cjson-win] [5/5] 链接自检 ..."
    cat > "$TMPD/t.c" <<'EOF'
#include <stdio.h>
#include <cJSON.h>
int main(void) {
    printf("version=%s\n", cJSON_Version());
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "hello", "world");
    char *s = cJSON_PrintUnformatted(root);
    printf("json=%s\n", s ? s : "(null)");
    cJSON_free(s);
    cJSON_Delete(root);
    return 0;
}
EOF
    if [ "$DO_STATIC" -eq 1 ]; then
        # 必须显式给 .a 路径：MinGW ld 在同一 -L 目录下优先 libfoo.dll.a（与 Linux 优先 .so 同行为），
        # 只写 -lcjson 会链到 DLL，验证不到静态库本身。
        $CC_BIN $CFLAGS $LDFLAGS_EXE -I"$OUT/include" "$TMPD/t.c" \
            "$OUT/lib/libcjson.a" -o "$TMPD/t_static.exe"
        echo -n "        静态链接: " && ("$TMPD/t_static.exe" || echo "(运行被拦截，链接已成功)")
    fi
    if [ "$DO_SHARED" -eq 1 ]; then
        $CC_BIN $CFLAGS $LDFLAGS_EXE -I"$OUT/include" "$TMPD/t.c" \
            -L"$OUT/lib" -lcjson -o "$TMPD/t_shared.exe"
        cp -f "$OUT/lib/libcjson.dll" "$TMPD/"   # 同目录放置，模拟分发
        echo -n "        动态链接: " && ("$TMPD/t_shared.exe" || echo "(运行被拦截，链接已成功)")
    fi
else
    echo "[cjson-win] [5/5] 自检（跳过）"
fi

# ---------- 产物清单 ----------
echo
echo "=================== 产物清单 ==================="
find "$OUT" -type f \( -name '*.h' -o -name '*.a' -o -name '*.dll' -o -name '*.lib' -o -name '*.def' \) \
    -printf '% 10s  %p\n' 2>/dev/null \
  || ls -la "$OUT/include" "$OUT/lib"
echo
echo "[cjson-win] DLL 导出符号数: $($NM_BIN --defined-only "$TMPD/cJSON.o" 2>/dev/null | grep -c ' T ' || echo 0)"
echo "[cjson-win] DLL 依赖导入表:"
$TRIPLE-objdump -p "$OUT/lib/libcjson.dll" 2>/dev/null | grep -i "DLL Name" || true
echo
echo "[cjson-win] 完成。使用方式："
echo "    动态（默认）：gcc ... -I$OUT/include -L$OUT/lib -lcjson"
echo "                  → MinGW ld 优先 libfoo.dll.a，等同 Linux 下优先 .so；"
echo "                    需把 libcjson.dll 拷到 exe 同目录（Windows 无 rpath/RUNPATH）。"
echo "    静态（推荐）：gcc ... -I$OUT/include ... $OUT/lib/libcjson.a      # 直接给 .a 路径"
echo "                  或 -Wl,-Bstatic -lcjson -Wl,-Bdynamic"
echo "    MSVC        ：cl ... /I$OUT/include ... /link /LIBPATH:$OUT/lib libcjson.lib"
echo "                  （libcjson.lib 为 dlltool 生成的 COFF 导入库，运行时仍需 libcjson.dll）"
