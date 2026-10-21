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
OBJS    = main.o bizd_config.o bizd_util.o \
         bizd_route.o bizd_handler.o bizd_srv.o

# —— 分层依赖：本项目库静态链接(.a)，运行期不依赖 libq*.so；
#    外部库(pthread / Tongsuo / libxml2 / cJSON)仅在最终可执行程序链接期引入 ——
QLIBS  = $(call ld_group, \
         $(LIB)/libqcommon.a $(LIB)/libqreactor.a $(LIB)/libqxcfg.a \
         $(LIB)/libqipc.a $(LIB)/libqcodec.a $(LIB)/libqchan.a \
         $(LIB)/libqsec.a $(LIB)/libqcrypto.a \
         $(LIB)/libqrand.a $(LIB)/libqutil.a $(LIB)/libqmem.a $(LIB)/libqlog.a \
         $(LIB)/libqnet.a)

# cJSON 为本地 vendored（$(DEP_HOME)/cjson）。
# 【编译期】头文件路径必须走 SOFT_INCL —— Makefile.inc 会把 SOFT_INCL 展开为 -I 加入 CFLAGS；
# 原先误把 -I$(SOFT_HOME)/cJSON/include 写在 LIBS 里，而 LIBS 只在链接期生效，
# 导致 include_xml.h -> cJSON.h 在编译期找不到（fatal error: 'cJSON.h' file not found）。
# 另：目录名是小写 cjson（不是 cJSON），路径写错同样会编译期失败；
#     且走 DEP_HOME（三端依赖根），避免交叉编译时吃到别的平台的产物。
SOFT_INCL = . $(DEP_HOME)/cjson/include

# 链接期：系统 libxml2（q_xcfg 依赖）+ Tongsuo + vendored cJSON
LIBS   = $(QLIBS) $(LIB_PTHREAD) -lseccomp -lxml2 \
         -L$(DEP_HOME)/tongsuo/lib64 $(LIB_CRYPTO) -Wl,-rpath,$(DEP_HOME)/tongsuo/lib64 \
         -L$(DEP_HOME)/cjson/lib -lcjson -Wl,-rpath,$(DEP_HOME)/cjson/lib
# 注：DB 客户端库待接入后在此追加；seccomp 沙箱已移入共享库 libqcommon，
#     -lseccomp 由该库符号拉取（Linux 专有库；macOS 无 libseccomp，链接期会失败，属平台限制）

include ../../include/Makefile.inc
