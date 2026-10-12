# ==============================================
#  soft_src/bizd  ——  业务进程（仅 Unix Socket，不直接暴露公网）后台服务（BUILD=bin）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：业务逻辑、权限校验、审计日志（SM3 链式哈希）、视频流调度；
#        入站数据来自 netd 经 q_ipc 转发的可信 TLV。
#  依赖闭包（高级在前、低级在后、传递闭包全列；pthread 按需）：
#    q_common(信号/降权) q_reactor q_ipc q_codec q_crypto q_rand
#    q_util q_mem q_log + pthread
# ==============================================

BUILD   = bin
TARGET  = q_service_bizd
OBJS    = main.o

# —— 分层依赖：本项目库静态链接(.a)，运行期不依赖 libq*.so；
#    外部库(pthread / Tongsuo)仅在最终可执行程序链接期引入 ——
QLIBS  = $(call ld_group, \
         $(LIB)/libqcommon.a $(LIB)/libqreactor.a \
         $(LIB)/libqipc.a $(LIB)/libqcodec.a $(LIB)/libqcrypto.a \
         $(LIB)/libqrand.a $(LIB)/libqutil.a $(LIB)/libqmem.a $(LIB)/libqlog.a)
LIBS   = $(QLIBS) $(LIB_PTHREAD) -L$(SOFT_HOME)/tongsuo/lib64 $(LIB_CRYPTO) -Wl,-rpath,$(SOFT_HOME)/tongsuo/lib64
# 注：DB 客户端库待接入后在此追加；seccomp 待实现后追加 $(LIB_SECCOMP)

include ../../include/Makefile.inc
