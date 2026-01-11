#ifndef BMS_I2C_SAFE_H
#define BMS_I2C_SAFE_H

#include "tl_common.h"
#include "drivers.h"

typedef enum {
	BMS_I2C_ERR_LIGHT = 0,
	BMS_I2C_ERR_MID,
	BMS_I2C_ERR_HEAVY,
} bms_i2c_error_level_t;

typedef enum {
	BMS_I2C_REASON_TIMEOUT = 0,
	BMS_I2C_REASON_RETRY_FAIL,
	BMS_I2C_REASON_BAD_CONTEXT,
} bms_i2c_error_reason_t;

void bms_i2c_safe_setup(I2C_GPIO_GroupTypeDef group, unsigned char divClock);
int bms_i2c_read(u8 dev7, u16 reg, u8 reg_len, u8 *buf, u16 len, u32 timeout_us, u8 retry);
int bms_i2c_write(u8 dev7, u16 reg, u8 reg_len, const u8 *buf, u16 len, u32 timeout_us, u8 retry);
void bms_i2c_recover_bus(void);
void bms_i2c_on_error(bms_i2c_error_level_t level, bms_i2c_error_reason_t reason);

#endif
