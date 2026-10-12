all: fundmental_lib

fundmental_lib:
	@echo
	@echo "    Begin make fundmental library ..."
	@echo
	(cd q_log; make)
	(cd q_mem; make)
	(cd q_xcfg; make)
	(cd q_ds; make)
	(cd q_util; make)
	(cd q_rand; make)
	(cd q_crypto; make)
	(cd q_reactor; make)
	(cd q_common; make)
	@echo
	@echo "    Make fundmental library success !!!"

clean:
	(cd q_log; make clean)
	(cd q_mem; make clean)
	(cd q_xcfg; make clean)
	(cd q_ds; make clean)
	(cd q_util; make clean)
	(cd q_rand; make clean)
	(cd q_crypto; make clean)
	(cd q_reactor; make clean)
	(cd q_common; make clean)
	@echo
	@echo "    Clean fundmental library compiled file okey ..."
