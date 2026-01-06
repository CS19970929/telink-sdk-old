#pragma once
#include "tl_common.h"

void st_flash_read(u32 addr, u32 len, u8 *buf);
void st_flash_write(u32 addr, u32 len, const u8 *buf);
void st_flash_erase_sector(u32 addr);

// 可选：安全电压检查钩子（你硬件若有电压检测，建议实现）
int  st_flash_is_voltage_safe(void);
