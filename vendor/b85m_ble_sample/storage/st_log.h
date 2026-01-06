#pragma once
#include "tl_common.h"

typedef enum {
    ST_LOG_OK = 0,
    ST_LOG_ERR = -1,
    ST_LOG_NO_SPACE = -2,
} st_log_ret_t;

typedef struct {
    u32 seq;
    u32 ts_ms;     // 你没有 RTC 也没关系，用 tick 换算即可
    u16 type;
    u16 len;
} st_log_item_info_t;

int st_log_init(void);

// 追加一条日志（payload 可为结构体/快照）
st_log_ret_t st_log_append(u16 type, u32 ts_ms, const void *payload, u16 len);

// 读取：按 seq 查找（线扫；后续可加索引）
st_log_ret_t st_log_read(u32 seq, st_log_item_info_t *info, void *out, u16 *inout_len);

// 轮询：处理切扇区/延迟擦除
void st_log_poll(void);
