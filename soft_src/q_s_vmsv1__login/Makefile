# ==============================================
#  soft_src/q_s_vmsv1__login  ——  登录微服务（bizd 下游，TCP + JSON）后台服务（BUILD=bin）
#  首行必须引入公共配置；目录结构见 AI_SKILL_RULE §3.1
#  职责：接收 bizd 转发的 JSON 信封，完成登录鉴权并签发 Token，回写 JSON 响应。
#        用于打通 客户端 -> netd -> bizd -> login 全链路测试。
#  依赖闭包（高级在前、低级在后、传递闭包全列；pthread 按需）：
#    q_common(信号/降权) q_reactor q_net(TCP 服务器/阻塞收发) q_log q_mem q_rand + pthread
#  注：cJSON 走 include_xml.h；-lcjson 由 -L$(LIB)（lib/）或系统路径解析。
# ==============================================

BUILD   = bin
TARGET  = q_service_login
OBJS    = main.o login_util.o login_handler.o login_srv.o

# —— 分层依赖：本项目库静态链接(.a)，运行期不依赖 libq*.so ——
QLIBS  = $(call ld_group, \
         $(LIB)/libqcommon.a $(LIB)/libqreactor.a \
         $(LIB)/libqnet.a $(LIB)/libqlog.a $(LIB)/libqmem.a \
         $(LIB)/libqrand.a)
# cJSON（lib/ 或系统 /usr/lib/x86_64-linux-gnu 均可解析）+ pthread
LIBS   = $(QLIBS) $(LIB_PTHREAD) -lcjson

include ../../include/Makefile.inc
