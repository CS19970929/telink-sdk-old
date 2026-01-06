#pragma once
#include "tl_common.h"

#define RUN_STATE_VER   1

typedef struct __attribute__((packed)) {
    u16 ver;
    u16 soc_x100;      // SOC*100
    s32 ah_x1000;      // Ah*1000（可负）
    u32 cycle_cnt;
    u32 last_save_tick_s; // 你自己定义时间基准（RTC秒/运行秒）
    u16 reserved;
} run_state_t;

void run_kv_init(void);
int  run_kv_load(run_state_t *out);          // 1=ok, 0=no data
int  run_kv_request_save(const run_state_t *st); // 只请求，不直接写
void run_kv_proc(void);                      // 主循环里调用，真正写flash
