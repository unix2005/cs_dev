#ifndef Q_SEC_HEADERS_H
#define Q_SEC_HEADERS_H
/* 本库聚合公共基础头（仅 include_*.h），由 api/ 下 .c 经此引入；
   外部 q_*.h（q_crypto.h / q_mem.h 等）由各 .c 在 headers.h 之后单独 include */
#include "include_stdio.h"
#include "include_time.h"
#endif /* Q_SEC_HEADERS_H */
