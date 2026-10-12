# ==============================================
#  lib_src/l_1/q_common  ——  l_1 基础层 守护进程公共件库（libqcommon）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：信号安装 / 停止标志 / 降权沙箱（chroot + setuid；seccomp 待实现）。
#  依赖：l_1 基础（libc）；seccomp 待实现后链接 $(LIB_SECCOMP)。
# ==============================================

# —— 模块变量（先于 include，供 Makefile.inc 通用规则使用）——
TARGET    = libqcommon.a
TARGET_SO = libqcommon.so
OBJS   = api/q_common_signal.o \
         api/q_common_harden.o

include ../../../include/Makefile.inc

# 注：当前骨架未调用 libseccomp，暂不 -lseccomp；实现 seccomp 后在此追加：
# LIBS += $(LIB_SECCOMP)

# 对外头文件经 Makefile.inc 内置 install_hdr 归集（HEADER 默认即 q_common.h），无需重写

all: lib_static lib_so install_lib install_hdr
