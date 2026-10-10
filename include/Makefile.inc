# ==============================================
#  CS_DEV 通用全局 Makefile（公共配置）
#  存放位置：cs_dev/include/Makefile.inc
#  适配目录：bin / include / lib / dll / lib_src / soft_src / shell
#  兼容 Linux / macOS / Windows(MinGW)
# ==============================================
#
#  【引入方式】（三选一，任选其一即可正确解析 SOFT_HOME）
#    1) 推荐：shell 总构建脚本中 export SOFT_HOME=<cs_dev根目录>，模块内用绝对路径
#         include ${SOFT_HOME}/include/Makefile.inc
#    2) 相对路径（按目录深度调整 .. 级数）
#         lib_src/l_N/q_xxx/  ->  include ../../../include/Makefile.inc
#         soft_src/<proc>/    ->  include ../../include/Makefile.inc
#    3) 命令行传入：make SOFT_HOME=/Users/mac/work/cs_dev
#
#  【各模块 Makefile 必须定义（须在 include 之前定义，供 lib_static/lib_so 正确捕获先决条件）】
#         TARGET    = libqxxx.a        # 库产物名：libq + 模块名，不含下划线
#         TARGET_SO = libqxxx.so       # 动态库名（可省略，默认由 TARGET 推导：libqxxx.so）
#         OBJS      = q_xxx.o ...      # 必须先于 include 定义
#         LIBS     += -lqxxx ...       # 逐级添加下层依赖：l_3 -> l_2 -> l_1
#  注意：LIBS 仅用于【可执行程序】的最终链接；编译动态库(.so)时【不】传入 -l，
#        内部库依赖以未决符号保留，运行期经 RUNPATH 从 lib/ 解析。
# ==============================================

# 基础编译工具链
CC      = gcc
AR      = ar
LD      = gcc
RANLIB  = ranlib

# ----------------------------------------------
#  项目根目录：环境变量优先，其次由本文件自身位置推导
# ----------------------------------------------
_INC_SELF := $(abspath $(lastword $(MAKEFILE_LIST)))
SOFT_HOME ?= $(abspath $(dir $(_INC_SELF))..)

# 目录解析失败立即报错，避免后续静默失败
ifeq ($(wildcard $(SOFT_HOME)/include/Makefile.inc),)
$(error SOFT_HOME 解析失败: "$(SOFT_HOME)". 请在 shell 构建脚本中 export SOFT_HOME=<cs_dev根目录>，或在模块 Makefile 中使用相对路径 include)
endif

# 全局公共头文件、库文件、可执行文件目录
INCLUDE = $(SOFT_HOME)/include
LIB     = $(SOFT_HOME)/lib
BIN     = $(SOFT_HOME)/bin

# 本地源码目录（当前编译目录）
# 用 ?= 以便模块在 include 前扩展（如守护进程追加 ../../common 引入公共件头）
SOFT_INCL ?= .
SOFT_LIB  ?= .

# ================= 第三方依赖（按需启用）=================
# 国密密码库 Tongsuo（铜锁）：只有 lib_src/l_1/q_crypto 需要链接
TONGSUO_HOME ?= $(SOFT_HOME)/tongsuo
LIB_CRYPTO    = -lssl -lcrypto

# 视频解码（按需）
# FFMPEG_HOME ?= $(SOFT_HOME)/open_src/ffmpeg
# LIB_FFMPEG  = -lavformat -lavcodec -lavutil -lswscale

EXT_INCL = $(TONGSUO_HOME)/include
EXT_LIB  = $(TONGSUO_HOME)/lib

# ==============================================
#  核心编译参数
# ==============================================
# 公共编译参数（库与可执行程序共用）：包含路径、优化、告警、栈保护、宏定义
# 注意：此处不含 -fPIE / -fPIC / -pie，这些按“库 / 可执行程序”分类放到下方专用变量
CFLAGS = -I. -I$(INCLUDE) \
         $(addprefix -I,$(SOFT_INCL)) -I$(EXT_INCL) \
         -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
         -Wall -Wextra -Wcast-align -Wwrite-strings -Wpointer-arith \
         -Wstrict-prototypes -Wmissing-prototypes -Wunreachable-code \
         -Wformat -Werror=format-security \
         -Wshadow -Wundef \
         -D_THREAD_SAFE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 \
         -fno-common

# 严格模式（可选）：make STRICT=1
ifeq ($(STRICT),1)
CFLAGS += -Wconversion -ftrapv
endif

# —— 库构建专用参数 ——
# 库目标文件必须 -fPIC（位置无关，方可链入 .a/.so 及 PIE 可执行程序）
LIB_CFLAGS  = -fPIC
# 动态库链接参数（仅 -shared，不含 LDFLAGS 的 -pie）：
#  -L$(LIB) -Wl,-rpath,$(LIB) 写入 RUNPATH，使本 .so 运行期能从 lib/ 解析
#    其传递依赖的其它 q_* 库（如 libqchan.so 调用 libqcodec.so）；
#  -Wl,--allow-shlib-undefined 允许本 .so 保留未决符号，依赖由最终链接
#    可执行程序（或运行期）统一解析——动态库编译期不 -l 工程内部库。
#  注意：绝不引入 LIBS（-l 工程库）。
LIB_LDFLAGS = -L$(LIB) -Wl,-rpath,$(LIB) -Wl,--allow-shlib-undefined
# 动态库类型标记：-shared。缺失会导致 gcc/clang 退化成“可执行程序”链接而报缺 _main。
# （Windows MinGW 块末尾另有 SOFLAGS := -shared 覆盖，保持同一语义）
SOFLAGS     = -shared
# 静态库参数
ARFLAGS = -rcs

# —— 可执行程序构建专用参数 ——
# 可执行目标文件用 -fPIE（与链接期 -pie 配合，生成位置无关可执行程序）
BIN_CFLAGS  = -fPIE
# 可执行程序链接参数：PIE + 装载期加固 + 基础系统库
# 注意：不含 crypto / ffmpeg，需要的模块自行 LIBS += $(LIB_CRYPTO)
#   LIBS（含 -lq* 工程库）仅在此处参与最终链接，由可执行程序决定全部依赖。
LDFLAGS     = -L$(LIB) -L$(SOFT_LIB) -L$(EXT_LIB) \
              -pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack \
              -lpthread -lm -lxml2 -luuid

# 目标名（各模块 Makefile 覆盖，如 TARGET = libqlog.a）
TARGET ?= libapp.a
# 动态库名（默认由静态库名推导，模块可显式覆盖，如 TARGET_SO = libqlog.so）
TARGET_SO ?= $(TARGET:.a=.so)

# 模块公共头文件名（与 shell/build.sh 头文件归集保持一致：模块目录名 + .h）
# 例如 lib_src/l_3/q_chan -> q_chan.h；模块亦可显式覆盖 HEADER
MODNAME = $(notdir $(CURDIR))
HEADER  ?= $(MODNAME).h

# 分层依赖（各模块 Makefile 逐级追加，如 LIBS += -lqcodec -lqcrypto）
# 仅用于【可执行程序】最终链接；动态库编译不消费此变量。
LIBS ?=

# ==============================================
#  通用编译规则
# ==============================================
# 模块产物类型：lib（默认，产出 .a/.so）或 bin（产出可执行程序）；
# 各模块 Makefile 可显式覆盖，例如 BUILD = bin
BUILD ?= lib

# 依据 BUILD 选择目标文件编译参数（库用 -fPIC，可执行程序用 -fPIE）
ifeq ($(BUILD),bin)
OBJ_CFLAGS = $(BIN_CFLAGS)
else
OBJ_CFLAGS = $(LIB_CFLAGS)
endif

.SUFFIXES: .c .o

.c.o:
	$(CC) -c $(CFLAGS) $(OBJ_CFLAGS) -o $@ $<

# ==============================================
#  通用业务指令（全局统一规范）
# ==============================================
.PHONY: all clean lib_static lib_so install_bin install_lib install_hdr check_layer format format-check

ifeq ($(BUILD),bin)
all: $(TARGET) install_bin
else
# 库构建：编译静态/动态库 -> 搬运到 lib/ -> 归集公共头到 include/
all: lib_static lib_so install_lib install_hdr
endif

clean:
	rm -rf *.o *.a *.so *.dylib *.exe core *.log $(TARGET) api/*.o ut/bin

# 编译静态库（ar 仅归档 .o，不参与链接，故不需要 -l）
lib_static: $(OBJS)
	$(AR) $(ARFLAGS) $(TARGET) $(OBJS)
	-$(RANLIB) $(TARGET)

# 编译动态库（产出 TARGET_SO，库名不含下划线）
# 用 LIB_CFLAGS(-fPIC) + SOFLAGS(-shared)；绝不引入 LDFLAGS 中的 -pie（专用于可执行程序）
# 关键约定：动态库编译【不】传入 $(LIBS)，即不 -l 工程内部库；内部依赖
# 以未决符号形式保留，由最终链接可执行程序时（或运行期经 RUNPATH）统一解析。
lib_so: $(OBJS)
	$(CC) $(CFLAGS) $(LIB_CFLAGS) $(SOFLAGS) $(LIB_LDFLAGS) -o $(TARGET_SO) $(OBJS)

# 可执行程序统一链接配方（仅 BUILD=bin 时启用；模块需将 TARGET 设为可执行文件名，
# 且 OBJS 为其源文件；链接用 BIN_CFLAGS + LDFLAGS(-pie 等)，并消费 $(LIBS) 全部 -l 依赖）
ifeq ($(BUILD),bin)
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(BIN_CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
endif

# 安装可执行程序 --> bin/
install_bin:
	mv -f $(TARGET) $(BIN)/

# 安装库 --> lib/（Linux/macOS）；Windows 产物由 dll/ 规则处理
# 注：仅搬运库文件；对外头文件由各模块自行 install_hdr 归集，不在此处拷贝内部聚合头
install_lib:
	mv -f $(TARGET) $(LIB)/
	mv -f $(TARGET_SO) $(LIB)/

# 归集对外公共头 --> include/（仅搬运 q_<mod>.h；内部聚合头 headers.h 由模块自管）
# 与 shell/build.sh 的头文件归集保持一致，使单模块 `make` 也能独立产出可用的 include/ 与 lib/
install_hdr:
	@if [ -f "$(HEADER)" ]; then \
		cp -f "$(HEADER)" "$(INCLUDE)/" && echo "    $(HEADER) -> $(INCLUDE)/"; \
	else \
		echo "    (无公共头 $(HEADER)，跳过)"; \
	fi

# 分层依赖静态检查
check_layer:
	@bash $(SOFT_HOME)/shell/check_layer.sh

# ==============================================
#  代码格式化（clangd / clang-format 共用根目录 .clang-format 样式）
#  - make format       按样式就地重写当前模块的 *.c / *.h
#  - make format-check 仅检查（不匹配则非零退出，便于 CI）
#  注：clangd 在编辑器中按 .clang-format 自动格式化；本目标提供命令行等价能力
# ==============================================
format:
	@find . \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 clang-format -i

format-check:
	@find . \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 clang-format --dry-run --Werror

# ==============================================
#  跨平台兼容
# ==============================================
# macOS 兼容适配
ifeq ($(shell uname -s),Darwin)
CFLAGS  += -D_Q_SYS_MACOS
LDFLAGS += -undefined dynamic_lookup
# macOS ld64 用 -undefined dynamic_lookup 允许动态库保留未决符号（等价于 GNU 的 --allow-shlib-undefined）
LIB_LDFLAGS = -L$(LIB) -Wl,-rpath,$(LIB) -undefined dynamic_lookup
endif

# Linux 兼容适配
ifeq ($(shell uname -s),Linux)
CFLAGS  += -D_Q_SYS_LINUX
# Linux 专属链接期加固（macOS ld64 不支持 -z 参数，不可下移）
LDFLAGS += -Wl,-z,relro,-z,now -Wl,-z,noexecstack
# 本项目约定动态库编译不互相 -l（见 lib_so 配方），故可执行程序必须列全传递依赖，
# 并用 --no-as-needed 强制把所有 q_* 写入 NEEDED，运行期方可由 loader 统一加载解析。
LDFLAGS += -Wl,--no-as-needed
endif

# Windows MinGW 兼容适配
ifdef WIN32
CFLAGS  += -D_Q_SYS_WINDOWS
LDFLAGS := $(filter-out -fPIE -pie,$(LDFLAGS))
SOFLAGS := -shared
endif
