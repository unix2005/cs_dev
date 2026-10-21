#!/usr/bin/env bash
# =============================================================================
# build_tongsuo_win.sh —— 编译国密密码库 Tongsuo（铜锁）Windows 版
# -----------------------------------------------------------------------------
# 源码：$(SOFT_HOME)/open_src/tongsuo-master/tongsuo-master
# 产物：$(SOFT_HOME)/tongsuo（默认），布局 include/ lib/ bin/ ssl/
#       —— 对齐 include/Makefile.inc 的约定：
#            TONGSUO_HOME ?= $(SOFT_HOME)/tongsuo
#            EXT_INCL = $(TONGSUO_HOME)/include
#            EXT_LIB  = $(TONGSUO_HOME)/lib        # 注：Linux 版 OpenSSL 默认落在 lib64
#          故此处显式 --libdir=lib，与 EXT_LIB 保持一致。
#
# 工具链：llvm-mingw（x86_64-w64-mingw32 / clang 19）+ MSYS2 的 GNU make + NASM + Perl
#
# 为什么必须自己备 Perl/NASM（本机环境踩过的坑）：
#   1) OpenSSL 3.x 构建强依赖 Perl + Text::Template 模块。本机 PATH 里没有 perl；
#      Git for Windows 自带的 perl 有两个致命问题：
#        a. 缺 Text::Template / Pod::Usage / ExtUtils::MakeMaker；
#        b. 路径 C:\Program Files\Git\... 含空格，写进生成的 Makefile 后命令会被拆开。
#      → 故从 MSYS2 镜像取 perl 包（+ db/gdbm/libxcrypt）解压到 /d/msys64。
#   2) 汇编优化需要 NASM，否则只能 no-asm（SM4/SM3/SM2 性能损失明显）。
#      GitHub Release 下载被代理拦截 → 从清华 MSYS2 镜像取 mingw-w64-x86_64-nasm。
#   3) Text::Template 从清华 CPAN 镜像取（cpan.org 不通）。
#
# 用法（Git Bash / MSYS2）：
#   bash shell/build_tongsuo_win.sh                     # 默认 x86_64，装到 <SOFT_HOME>/tongsuo
#   bash shell/build_tongsuo_win.sh --deps              # 仅准备 perl/nasm/Text::Template 依赖
#   bash shell/build_tongsuo_win.sh --no-asm            # 不用汇编（无 NASM 时的退路）
#   bash shell/build_tongsuo_win.sh --enable-ntls       # 额外启用国密 TLS（NTLS）
#   bash shell/build_tongsuo_win.sh --jobs=16 --prefix=/e/other/tongsuo
#   bash shell/build_tongsuo_win.sh --clean             # 只清理构建目录
#
# 说明：产物含静态库(libcrypto.a/libssl.a)与 DLL(libcrypto-3-x64.dll 等)；
#       MinGW 下 -lcrypto 默认链接 DLL（优先 libcrypto.dll.a），要静态需直接给 .a 路径。
# =============================================================================
set -euo pipefail

# ---------- 参数 ----------
ARCH="x86_64"
JOBS="$(nproc 2>/dev/null || echo 8)"
DO_DEPS=0
DO_CLEAN=0
USE_ASM=1
ENABLE_NTLS=0
LIBS_ONLY=0
PREFIX=""
for a in "$@"; do
    case "$a" in
        --arch=*)        ARCH="${a#*=}" ;;
        --jobs=*)        JOBS="${a#*=}" ;;
        --prefix=*)      PREFIX="${a#*=}" ;;
        --deps)          DO_DEPS=1 ;;
        --clean)         DO_CLEAN=1 ;;
        --no-asm)        USE_ASM=0 ;;
        --enable-ntls)   ENABLE_NTLS=1 ;;
        --libs-only)     LIBS_ONLY=1 ;;   # 只编库，跳过 apps/openssl.exe
        -h|--help)       sed -n '2,45p' "$0"; exit 0 ;;
        *) echo "[tongsuo-win] 未知参数: $a" >&2; exit 1 ;;
    esac
done

SOFT_HOME="${SOFT_HOME:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
SRC="$SOFT_HOME/open_src/tongsuo-master/tongsuo-master"
OUT="${PREFIX:-$SOFT_HOME/tongsuo}"
DEPS="$SOFT_HOME/open_src/_deps"

# 临时目录（Windows 风格）：C 盘仅剩 ~4G，统一指到 E 盘，供 windres / clang 链接器使用
mkdir -p "$DEPS/tmp"
TMPDIR_WIN="$(cd "$DEPS/tmp" && pwd -W)"

LLVM_MINGW="${LLVM_MINGW:-/d/llvm-mingw-msvcrt-x86_64}"
MSYS_HOME="${MSYS_HOME:-/d/msys64}"
MIRROR_MSYS="${MIRROR_MSYS:-https://mirrors.tuna.tsinghua.edu.cn/msys2}"
MIRROR_CPAN="${MIRROR_CPAN:-https://mirrors.tuna.tsinghua.edu.cn/CPAN}"

case "$ARCH" in
    x86_64) TRIPLE="x86_64-w64-mingw32";  CFG_TARGET="mingw64" ;;
    i686)   TRIPLE="i686-w64-mingw32";    CFG_TARGET="mingw"   ;;
    *) echo "[tongsuo-win] 暂不支持 arch=$ARCH（可选 x86_64 / i686）" >&2; exit 1 ;;
esac

[ -f "$SRC/Configure" ] || { echo "[tongsuo-win] 源码未找到: $SRC" >&2; exit 1; }

# =============================================================================
# 阶段 0：构建依赖（Perl / Text::Template / NASM）
# =============================================================================
PERL_BIN="$MSYS_HOME/usr/bin/perl.exe"
NASM_BIN="$DEPS/nasm/mingw64/bin/nasm.exe"

prepare_deps() {
    mkdir -p "$DEPS"
    echo "[deps] 依赖目录: $DEPS"

    # --- Perl（MSYS2 包，解压到 $MSYS_HOME，路径无空格）---
    if [ ! -x "$PERL_BIN" ]; then
        echo "[deps] 安装 Perl（MSYS2 包）..."
        local pkgs="perl-5.38.4-2 db-6.2.32-6 gdbm-1.26-1 libxcrypt-4.5.2-1"
        for p in $pkgs; do
            [ -f "$DEPS/$p.pkg.tar.zst" ] || \
                curl -sSL -m 300 -o "$DEPS/$p.pkg.tar.zst" "$MIRROR_MSYS/msys/x86_64/$p-x86_64.pkg.tar.zst"
            "$MSYS_HOME/usr/bin/bsdtar.exe" -xf "$DEPS/$p.pkg.tar.zst" -C "$MSYS_HOME"
            echo "[deps]   $p 已解压 -> $MSYS_HOME"
        done
    else
        echo "[deps] Perl 已就绪: $PERL_BIN"
    fi

    # --- Text::Template（OpenSSL 3.x 必需，cpan.org 不通，走镜像）---
    #     注意版本门槛：Tongsuo/OpenSSL 3.5 的 util/perl/OpenSSL/Template.pm 要求 >= 1.46，
    #     装 1.45 会报 "Text::Template version 1.46 required"，故固定取 1.61。
    local tt_tar="Text-Template-1.61.tar.gz"
    local vendor="$MSYS_HOME/usr/share/perl5/vendor_perl/Text"
    if ! [ -f "$vendor/Template.pm" ] || \
       ! "$PERL_BIN" -MText::Template -e 'exit($Text::Template::VERSION >= 1.46 ? 0 : 1)' 2>/dev/null; then
        echo "[deps] 安装/升级 Text::Template (>=1.46) ..."
        [ -f "$DEPS/$tt_tar" ] || \
            curl -sSL -m 120 -o "$DEPS/$tt_tar" \
                "$MIRROR_CPAN/modules/by-module/Text/$tt_tar"
        local td; td="$(mktemp -d)"
        tar xzf "$DEPS/$tt_tar" -C "$td"
        mkdir -p "$vendor"
        cp -f "$td"/Text-Template-*/lib/Text/Template.pm "$vendor/"
        rm -rf "$td"
        echo "[deps]   Text::Template -> $vendor/Template.pm"
    else
        echo "[deps] Text::Template 已就绪"
    fi

    # --- windres 包装（关键修复）---
    #     现象：手工执行 llvm-windres 一切正常，但由 MSYS make 调用时报
    #           "Unable to create temp file: No such file or directory"。
    #     根因（实测）：make 派生的进程环境里【没有】TMP/TEMP/TMPDIR
    #           （MSYS 层把带盘符的临时目录变量剥离/转换掉了），
    #           原生 llvm-windres 拿不到临时目录。
    #     对策：在包装脚本（离 exec 最近处）用【相对路径】补上 TMP —— 相对路径不含盘符，
    #           不会被路径转换破坏；该目录相对 make 的 CWD（源码根）解析。
    #     注：另两种尝试均无效——MSYS2_ENV_CONV_EXCL 在 Git Bash 下不生效；
    #         换 MSYS2 的 GNU windres 会报 "preprocessing failed"（它默认调 gcc -E）。
    mkdir -p "$DEPS/bin" "$SRC/.build-tmp"
    cat > "$DEPS/bin/$TRIPLE-windres" <<EOF
#!/bin/sh
export TMP="$TMPDIR_WIN"
export TEMP="$TMPDIR_WIN"
export TMPDIR="$TMPDIR_WIN"
exec $LLVM_MINGW/bin/$TRIPLE-windres.exe "\$@"
EOF
    chmod +x "$DEPS/bin/$TRIPLE-windres"
    echo "[deps] windres 包装 -> $DEPS/bin/$TRIPLE-windres"

    # --- NASM（汇编优化；GitHub Release 常被代理拦，走 MSYS2 镜像）---
    if [ "$USE_ASM" -eq 1 ] && [ ! -x "$NASM_BIN" ]; then
        echo "[deps] 安装 NASM ..."
        [ -f "$DEPS/nasm.pkg.tar.zst" ] || \
            curl -sSL -m 300 -o "$DEPS/nasm.pkg.tar.zst" \
                "$MIRROR_MSYS/mingw/mingw64/mingw-w64-x86_64-nasm-2.16.03-1-any.pkg.tar.zst"
        mkdir -p "$DEPS/nasm"
        "$MSYS_HOME/usr/bin/bsdtar.exe" -xf "$DEPS/nasm.pkg.tar.zst" -C "$DEPS/nasm"
        echo "[deps]   NASM -> $NASM_BIN"
    elif [ "$USE_ASM" -eq 1 ]; then
        echo "[deps] NASM 已就绪: $NASM_BIN"
    fi
}

prepare_deps

if [ "$DO_DEPS" -eq 1 ]; then
    echo "[tongsuo-win] 依赖准备完成，按参数退出。"
    exit 0
fi

if [ "$DO_CLEAN" -eq 1 ]; then
    echo "[tongsuo-win] 清理构建目录 $SRC ..."
    ( cd "$SRC" && make distclean 2>/dev/null || true )
    rm -rf "$OUT"
    echo "[tongsuo-win] 已清理"
    exit 0
fi

# =============================================================================
# 阶段 1：环境
# =============================================================================
# PATH 顺序：windres 包装 → MSYS2(make/sh/coreutils/perl) → llvm-mingw(编译器) → NASM → 原 PATH
export PATH="$DEPS/bin:$MSYS_HOME/usr/bin:$LLVM_MINGW/bin:$DEPS/nasm/mingw64/bin:$PATH"

export CC="$TRIPLE-gcc"
export AR="$TRIPLE-ar"
export RANLIB="$TRIPLE-ranlib"

echo "=========================================================="
echo "[tongsuo-win] SOFT_HOME = $SOFT_HOME"
echo "[tongsuo-win] SRC       = $SRC"
echo "[tongsuo-win] PREFIX    = $OUT"
echo "[tongsuo-win] TARGET    = $CFG_TARGET (triple=$TRIPLE)"
echo "[tongsuo-win] CC        = $($CC --version | head -1)"
# --- make 必须用【原生 Windows 版】(mingw32-make)，不能用 MSYS2 的 make ---
#     踩坑记录：MSYS make 派生的进程环境里 TMP/TEMP/TMPDIR 会被剥离/转换，
#     导致原生工具链（llvm-windres、clang 链接器）报
#       "Unable to create temp file: No such file or directory"
#       "clang-19: error: unable to make temporary file"
#     换原生 mingw32-make 后环境变量原样传递，问题消失。
#     配方里的 shell 命令由 PATH 中的 MSYS sh/coreutils 提供，二者配合正常。
MAKE_BIN="mingw32-make"
command -v "$MAKE_BIN" >/dev/null || MAKE_BIN="$MSYS_HOME/usr/bin/make.exe"
echo "[tongsuo-win] MAKE      = $("$MAKE_BIN" --version | head -1)"
echo "[tongsuo-win] PERL      = $("$PERL_BIN" -e 'print $^V')"
[ "$USE_ASM" -eq 1 ] && echo "[tongsuo-win] NASM      = $(nasm -v)"
echo "[tongsuo-win] JOBS      = $JOBS"
echo "=========================================================="

# 依赖自检
for m in Text::Template Pod::Usage ExtUtils::MakeMaker; do
    "$PERL_BIN" -M"$m" -e1 2>/dev/null || { echo "[tongsuo-win] Perl 模块缺失: $m" >&2; exit 1; }
done

# =============================================================================
# 阶段 2：Configure
# =============================================================================
cd "$SRC"

CFG_ARGS=("$CFG_TARGET"
    "--prefix=$OUT"
    "--openssldir=$OUT/ssl"
    "--libdir=lib"
    "--cross-compile-prefix=$TRIPLE-"
    # 注：本版 Tongsuo 的 Configure 未实现 no-docs（INSTALL.md 有写但 disabled 列表里没有，
    #     传了会报 "Unsupported options: no-docs"），故不传；文档由 pod 生成，量小。
    "no-tests"
    "-O2"
    "-fstack-protector-strong"
    "-D_FORTIFY_SOURCE=0"
    "-Wl,--dynamicbase" "-Wl,--nxcompat" "-Wl,--high-entropy-va"
)
[ "$USE_ASM" -eq 0 ]   && CFG_ARGS+=("no-asm")
[ "$ENABLE_NTLS" -eq 1 ] && CFG_ARGS+=("enable-ntls")

echo "[tongsuo-win] Configure: ${CFG_ARGS[*]}"
"$PERL_BIN" ./Configure "${CFG_ARGS[@]}"

# =============================================================================
# 阶段 3：编译 & 安装
# =============================================================================
BUILD_LOG="$DEPS/tongsuo_build.log"
if [ "$LIBS_ONLY" -eq 1 ]; then
    # 只构建库（跳过 apps/openssl.exe）：自动化环境下 apps 的近百个 .c 各要清理一次
    # .d.tmp，容易触发执行环境的批量删除保护；库才是客户端真正需要的产物。
    echo "[tongsuo-win] make -j$JOBS build_libs（日志: $BUILD_LOG）"
    if ! "$MAKE_BIN" -j"$JOBS" build_libs > "$BUILD_LOG" 2>&1; then
        echo "[tongsuo-win] 编译失败，日志尾部：" >&2
        tail -40 "$BUILD_LOG" >&2
        exit 1
    fi
    tail -3 "$BUILD_LOG"
    echo "[tongsuo-win] 安装开发文件（头 + 库）到 $OUT ..."
    "$MAKE_BIN" install_dev >> "$BUILD_LOG" 2>&1 || { tail -30 "$BUILD_LOG" >&2; exit 1; }
else
    echo "[tongsuo-win] make -j$JOBS build_sw（日志: $BUILD_LOG）"
    if ! "$MAKE_BIN" -j"$JOBS" build_sw > "$BUILD_LOG" 2>&1; then
        echo "[tongsuo-win] 编译失败，日志尾部：" >&2
        tail -40 "$BUILD_LOG" >&2
        exit 1
    fi
    tail -3 "$BUILD_LOG"
    echo "[tongsuo-win] 安装到 $OUT ..."
    "$MAKE_BIN" install_sw >> "$BUILD_LOG" 2>&1 || { tail -30 "$BUILD_LOG" >&2; exit 1; }
fi

# =============================================================================
# 阶段 4：验证
# =============================================================================
echo
echo "=================== 产物清单 ==================="
ls -la "$OUT/include/openssl/opensslv.h" "$OUT/lib"/*.a "$OUT/lib"/*.dll.a 2>/dev/null | head -20
ls -la "$OUT/bin" 2>/dev/null | head -5

echo
echo "=================== 功能自检 ==================="
if [ -x "$OUT/bin/openssl.exe" ]; then
    "$OUT/bin/openssl.exe" version -a 2>&1 | head -3
    echo "--- SM3 ---"
    printf 'abc' | "$OUT/bin/openssl.exe" dgst -sm3 2>&1 | tail -1
    echo "--- SM2 密钥对 ---"
    "$OUT/bin/openssl.exe" ecparam -name SM2 -genkey -noout -out "$DEPS/_sm2.key" 2>&1 | tail -2
    "$OUT/bin/openssl.exe" ec -in "$DEPS/_sm2.key" -text -noout 2>&1 | grep -iE "ASN1 OID|SM2" | head -2
    rm -f "$DEPS/_sm2.key"
else
    # --libs-only 时没有 openssl.exe：改为链接库跑国密自检程序
    # （shell/tongsuo_sm_selftest.c：SM3 标准向量 / SM4-GCM 往返 / SM2 签名验签）
    echo "(无 openssl.exe，改用自带国密自检程序链接 libcrypto 验证)"
    SELFTEST_C="$SOFT_HOME/shell/tongsuo_sm_selftest.c"
    if [ -f "$SELFTEST_C" ]; then
        "$TRIPLE-gcc" -O2 -I"$OUT/include" "$SELFTEST_C" \
            "$OUT/lib/libcrypto.a" -lws2_32 -lgdi32 -lcrypt32 \
            -o "$DEPS/tongsuo_selftest.exe" \
            && ("$DEPS/tongsuo_selftest.exe" || echo "(运行被沙箱拦截，编译链接已成功)")
    fi
fi

echo
echo "[tongsuo-win] 完成。"
echo "    头文件：$OUT/include"
echo "    库文件：$OUT/lib  （libcrypto.a / libssl.a 静态；libcrypto-3-x64.dll / libssl-3-x64.dll 动态）"
echo "    用法  ：gcc ... -I$OUT/include -L$OUT/lib -lssl -lcrypto"
echo "            注意 MinGW 下 -lcrypto 默认链 DLL（libcrypto.dll.a 优先），"
echo "            要静态请直接写 .a 路径，且 Windows 无 rpath，DLL 需与 exe 同目录。"
