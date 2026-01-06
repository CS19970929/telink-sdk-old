#pragma once
#include "tl_common.h"

typedef enum {
    ST_KV_OK = 0,
    ST_KV_NOT_FOUND = 1,
    ST_KV_ERR = -1,
    ST_KV_NO_SPACE = -2,
} st_kv_ret_t;

// 注册固定 key 表（用于迁移拷贝最新值）
void st_kv_register_keys(const u16 *keys, u16 key_num);

// 初始化（扫描、选 active sector、定位写指针）
int  st_kv_init(void);

// 设置/获取
st_kv_ret_t st_kv_set(u16 key, const void *value, u16 len);
st_kv_ret_t st_kv_get(u16 key, void *out, u16 *inout_len);

// 轮询：处理迁移/延迟擦除等
void st_kv_poll(void);

// 触发迁移（可由上层在“安全窗口”调用）
int  st_kv_force_migrate(void);
u16 st_crc16_modbus(const u8 *buf, u16 len);