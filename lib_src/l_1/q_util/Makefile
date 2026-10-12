# ==============================================
#  lib_src/l_1/q_util  ——  l_1 基础层 通用工具库（libqutil）
#  首行必须引入公共配置
#  目录结构（见 AI_SKILL_RULE §3.1）：
#    headers.h      聚合公共基础头（include_*.h），仅作 api/ 源文件聚合入口，不安装
#    q_util.h       对外头文件（自包含，外部可直接 #include <q_util.h>）
#    Makefile
#    api/           C 源代码（q_util_*.c，每函数一文件）
#    ut/            单元测试 / 小程序（q_util_ut.c）
#  源文件引用铁律：api/ 下每个 .c 仅 #include "headers.h" 与 "q_util.h"
#
#  说明：TARGET / TARGET_SO / OBJS 在 include 之前定义，使 Makefile.inc 的
#        lib_static / lib_so 能正确捕获先决条件。
#        静态库 / 动态库 / 安装均由 Makefile.inc 通用目标完成，本模块仅做：
#          - 对外头归集 install_hdr
#          - 单元测试 ut
# ==============================================

# —— 模块变量（先于 include，供 Makefile.inc 通用规则使用）——
TARGET    = libqutil.a
TARGET_SO = libqutil.so
OBJS   = api/q_util_hex_encode.o api/q_util_hex_decode.o \
         api/q_util_b64_encode.o api/q_util_b64_decode.o \
         api/q_util_str_startswith.o api/q_util_str_endswith.o \
         api/q_util_str_trim.o api/q_util_str_rtrim.o \
         api/q_util_time_cmp.o api/q_util_time_format.o

include ../../../include/Makefile.inc

# 分层依赖：l_1 基础层，零项目内依赖（不追加 -lq*），仅依赖 libc

# 对外头文件归集到 include/
install_hdr:
	cp -f q_util.h $(INCLUDE)/q_util.h

# 扩展默认 all：仅追加先决条件（无配方体，不触发"覆盖配方"告警）
all: lib_static lib_so install_lib install_hdr

# 单元测试：构建并运行 ut/ 小程序（链接 libqutil.so，rpath 指向 lib/）
ut: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/q_util_ut.c -o ut/bin/q_util_ut -L$(LIB) -lqutil -lpthread -Wl,-rpath,$(LIB)
	./ut/bin/q_util_ut

.PHONY: install_hdr ut
