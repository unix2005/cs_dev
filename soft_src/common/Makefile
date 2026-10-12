# soft_src/common —— 守护进程公共件（静态库）
# 产物：libgtkvmscommon.a -> $(LIB)/
# 内容：信号安装 / 进程降权与 seccomp 沙箱占位（netd/bizd/knockd 共用，避免样板重复）
TARGET = libgtkvmscommon.a
OBJS   = daemon_signal.o daemon_harden.o
BUILD  = lib

include ../../include/Makefile.inc
