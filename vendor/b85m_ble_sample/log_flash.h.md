#pragma once
#include "tl_common.h"

typedef enum {
    LOGT_INFO = 1,
    LOGT_WARN = 2,
    LOGT_PROT = 3,   // 保护事件
    LOGT_DBG  = 4,
} log_type_t;

void log_flash_init(void);
int  log_push(log_type_t t, const u8 *payload, u16 len); // 只入队
void log_flash_proc(void); // 主循环执行写入
