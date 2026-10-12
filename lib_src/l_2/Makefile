all: l2_lib

l2_lib:
	@echo
	@echo "    Begin make l_2 (capability layer) ..."
	@echo
	(cd q_codec; make)
	(cd q_sec;   make)
	(cd q_ipc;   make)
	(cd q_net;   make)
	@echo
	@echo "    Make l_2 library success !!!"

clean:
	(cd q_codec; make clean)
	(cd q_sec;   make clean)
	(cd q_ipc;   make clean)
	(cd q_net;   make clean)
	@echo
	@echo "    Clean l_2 compiled file okey ..."
