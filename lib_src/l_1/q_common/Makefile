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
         api/q_common_harden.o \
         api/q_common_seccomp.o

include ../../../include/Makefile.inc

# seccomp 由 q_common_seccomp.c 实现（daemon_seccomp_apply）。
# 注：静态库 .a 仅 ar 归档、不参与链接，故此处无需 -lseccomp；真正链接发生在
# 最终可执行程序（knockd/netd/bizd 的 Makefile 须在其 LIBS 保留 -lseccomp）。
# 下行仅作依赖声明占位（lib 构建不消费，仅供阅读）：
LIBS += $(LIB_SECCOMP)

# 对外头文件经 Makefile.inc 内置 install_hdr 归集（HEADER 默认即 q_common.h），无需重写

all: lib_static lib_so install_lib install_hdr
