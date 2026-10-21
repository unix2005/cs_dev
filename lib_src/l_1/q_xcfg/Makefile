# ==============================================
#  lib_src/l_1/q_xcfg  ——  l_1 基础层 XML 配置库（libqxcfg）
#  首行必须引入公共配置
#  目录结构（见 AI_SKILL_RULE §3.1）：
#    headers.h      聚合公共基础头（include_*.h），仅作 api/ 源文件聚合入口，不安装
#    q_xcfg.h       对外头文件（自包含，外部可直接 #include <q_xcfg.h>）
#    Makefile
#    api/           C 源代码（q_xcfg_*.c，每函数一文件）
#    ut/            单元测试 / 小程序（q_xcfg_ut.c）
#  源文件引用铁律：api/ 下每个 .c 仅 #include "headers.h" 与 "q_xcfg.h"
#
#  说明：TARGET / TARGET_SO / OBJS 在 include 之前定义，使 Makefile.inc 的
#        lib_static / lib_so 能正确捕获先决条件。
#        静态库 / 动态库 / 安装均由 Makefile.inc 通用目标完成，本模块仅做：
#          - 对外头归集 install_hdr
#          - 单元测试 ut
# ==============================================

# —— 模块变量（先于 include，供 Makefile.inc 通用规则使用）——
TARGET    = libqxcfg.a
TARGET_SO = libqxcfg.so
OBJS   = api/q_xcfg_init.o api/q_xcfg_destroy.o \
         api/q_xcfg_get_int.o api/q_xcfg_get_string.o \
         api/q_xcfg_get_bool.o api/q_xcfg_get_float.o \
         api/q_xcfg_has_key.o api/q_xcfg_dump.o

# include_xml.h 会 #include "cJSON.h"（vendored 版）：头文件路径必须走 SOFT_INCL
# （Makefile.inc 展开为 -I 加入 CFLAGS），写在 LIBS 里对编译期无效。
# 用 DEP_HOME（三端依赖根）而非写死路径，交叉编译时 make DEP_HOME=... 覆盖即可。
SOFT_INCL = . $(DEP_HOME)/cjson/include

include ../../../include/Makefile.inc

# 分层依赖：l_1 基础层，零项目内依赖（不追加 -lq*）
# 仅依赖系统/开源库 libxml2；将 -lxml2 注入 .so 链接使其自包含。
# 注：Makefile.inc 的 lib_so 配方把 LIB_LDFLAGS 放在 OBJS 之前，而 ld 默认
# --as-needed 会在库尚未被引用时丢弃它，导致 libxml2 不进入 NEEDED。
# 故用 --no-as-needed 强制记录，链接结束再恢复 --as-needed，保证 .so 自包含。
#LIB_LDFLAGS += -Wl,--no-as-needed -lxml2 -Wl,--as-needed

# 对外头文件归集到 include/

# 扩展默认 all：仅追加先决条件（无配方体，不触发"覆盖配方"告警）
all: lib_static lib_so install_lib install_hdr

# 单元测试：构建并运行 ut/ 小程序（链接 libqxcfg.so，rpath 指向 lib/）
ut: all
	mkdir -p ut/bin
	$(CC) $(CFLAGS) -I. ut/q_xcfg_ut.c -o ut/bin/q_xcfg_ut -L$(LIB) -lqxcfg -lxml2 -lpthread -Wl,-rpath,$(LIB)
	./ut/bin/q_xcfg_ut

.PHONY: install_hdr ut
