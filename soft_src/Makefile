# ==============================================
#  soft_src  ——  交付型软件（后台服务 / 客户端）构建编排
#  说明：本目录同时含 GTK 客户端（gtk_vms_v1/v2，使用 meson，不在此 make 体系内）
#        与后台守护进程（netd/bizd/knockd，使用本项目 Makefile 体系）。
#        此处仅编排守护进程；gtk 项目请各自 cd 后用 meson 构建。
#  前置：lib_src 须先构建（各 q_* 库已装入 lib/）。
# ==============================================

all:
	@echo
	@echo "    Begin make soft_src servers ..."
	@echo
	(cd netd;   make)
	(cd bizd;   make)
	(cd knockd; make)
	(cd q_s_vmsv1__push; make)
	@echo
	@echo "    Make soft_src servers success !!!"

clean:
	(cd netd;   make clean)
	(cd bizd;   make clean)
	(cd knockd; make clean)
	(cd q_s_vmsv1__push; make clean)
	@echo
	@echo "    Clean soft_src servers okey ..."
