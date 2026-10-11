# spa_send —— knockd 测试发送端（仅联调 / 验证开门闭环用）
# 产物：可执行程序 spa_send -> $(BIN)/
# 用法：spa_send [target_host] [target_port] [req_port] [req_ttl] [user_id]
# 依赖：q_spa（SPA 包构造）+ q_crypto（SM4-GCM，基于 Tongsuo）+ q_rand / q_mem / q_log
# 构建：与 knockd 同处 Linux 构建环境执行 `make`（会落到 bin/）
TARGET = spa_send
OBJS   = main.o
BUILD  = bin
SOFT_INCL = .

# q_spa 传递依赖：q_crypto 国密基于 Tongsuo，最终链接须带上 $(LIB_CRYPTO)
LIBS += -lqspa -lqcrypto -lqrand -lqmem -lqlog $(LIB_CRYPTO)

include ../../../include/Makefile.inc
