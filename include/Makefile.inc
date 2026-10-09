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
#  【各模块 Makefile 必须定义】
#         TARGET  = libqxxx.a        # 库产物名：libq + 模块名，不含下划线
#         OBJS    = q_xxx.o ...
#         LIBS   += -lqxxx ...       # 逐级添加下层依赖：l_3 -> l_2 -> l_1
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
SOFT_INCL = .
SOFT_LIB  = .

# ================= 第三方依赖（按需启用）=================
# 国密密码库 Tongsuo（铜锁）：只有 lib_src/l_1/q_crypto 需要链接
TONGSUO_HOME ?= $(SOFT_HOME)/open_src/tongsuo
LIB_CRYPTO    = -lssl -lcrypto

# 视频解码（按需）
# FFMPEG_HOME ?= $(SOFT_HOME)/open_src/ffmpeg
# LIB_FFMPEG  = -lavformat -lavcodec -lavutil -lswscale

EXT_INCL = $(TONGSUO_HOME)/include
EXT_LIB  = $(TONGSUO_HOME)/lib

# ==============================================
#  核心编译参数
# ==============================================
CFLAGS = -I$(INCLUDE) -I$(INCLUDE) \
         -I$(SOFT_INCL) -I$(EXT_INCL) -I. \
         -g -O2 -m64 -std=c99 \
         -fPIE -fPIC -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
         -Wall -Wcast-align -Wwrite-strings -Wpointer-arith \
         -Wstrict-prototypes -Wmissing-prototypes \
         -Wno-uninitialized -Wunreachable-code \
         -Wformat -Werror=format-security \
         -D_THREAD_SAFE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -fcommon

# 严格模式（可选）：make STRICT=1
ifeq ($(STRICT),1)
CFLAGS += -Wconversion -ftrapv
endif

# 链接参数：基础系统库
# 注意：不含 crypto / ffmpeg，需要的模块自行 LIBS += $(LIB_CRYPTO)
LDFLAGS = -L$(LIB) -L$(SOFT_LIB) -L$(EXT_LIB) \
          -fPIE -pie -fstack-protector \
          -lpthread -lm -lxml2 -luuid

# 动态库编译参数（-o 由规则给出，不再塞进 SOFLAGS）
SOFLAGS = -shared

# 静态库参数
ARFLAGS = -rcs

# 目标名（各模块 Makefile 覆盖，如 TARGET = libqlog.a）
TARGET ?= libapp.a

# 分层依赖（各模块 Makefile 逐级追加，如 LIBS += -lqcodec -lqcrypto）
LIBS ?=

# ==============================================
#  通用编译规则
# ==============================================
.SUFFIXES: .c .o

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

# ==============================================
#  通用业务指令（全局统一规范）
# ==============================================
.PHONY: all clean lib_static lib_so install_bin install_lib check_layer

all: lib_static install_lib

clean:
	rm -rf *.o *.a *.so *.dylib *.exe core *.log

# 编译静态库
lib_static: $(OBJS)
	$(AR) $(ARFLAGS) $(TARGET) $(OBJS)
	-$(RANLIB) $(TARGET)

# 编译动态库（TARGET 需为 libqxxx.so）
lib_so: $(OBJS)
	$(CC) $(SOFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS) $(LIBS)

# 安装可执行程序 --> bin/
install_bin:
	mv -f $(TARGET) $(BIN)/

# 安装库 --> lib/（Linux/macOS）；Windows 产物由 dll/ 规则处理
install_lib:
	mv -f $(TARGET) $(LIB)/

# 分层依赖静态检查
check_layer:
	@sh $(SOFT_HOME)/shell/check_layer.sh

# ==============================================
#  跨平台兼容
# ==============================================
# macOS 兼容适配
ifeq ($(shell uname -s),Darwin)
CFLAGS  += -D_Q_SYS_MACOS
LDFLAGS += -undefined dynamic_lookup
endif

# Linux 兼容适配
ifeq ($(shell uname -s),Linux)
CFLAGS  += -D_Q_SYS_LINUX
# Linux 专属链接期加固（macOS ld64 不支持 -z 参数，不可下移）
LDFLAGS += -Wl,-z,relro,-z,now -Wl,-z,noexecstack
endif

# Windows MinGW 兼容适配
ifdef WIN32
CFLAGS  += -D_Q_SYS_WINDOWS
LDFLAGS := $(filter-out -fPIE -pie,$(LDFLAGS))
SOFLAGS := -shared
endif
