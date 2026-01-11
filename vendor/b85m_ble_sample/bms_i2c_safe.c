#include "tl_common.h"
#include "drivers.h"
#include "bms_i2c_safe.h"

typedef struct {
	I2C_GPIO_GroupTypeDef group;
	GPIO_PinTypeDef sda;
	GPIO_PinTypeDef scl;
	unsigned char div;
	u8 inited;
} bms_i2c_hw_t;

static bms_i2c_hw_t g_bms_i2c = {
	.group = I2C_GPIO_GROUP_C0C1,
	.sda = GPIO_PC0,
	.scl = GPIO_PC1,
	.div = (unsigned char)(CLOCK_SYS_CLOCK_HZ / (4 * 100000)),
	.inited = 0,
};

__attribute__((weak)) void bms_i2c_on_error(bms_i2c_error_level_t level, bms_i2c_error_reason_t reason)
{
	(void)level;
	(void)reason;
}

static void bms_i2c_select_pins(I2C_GPIO_GroupTypeDef group, GPIO_PinTypeDef *sda, GPIO_PinTypeDef *scl)
{
	switch (group)
	{
	case I2C_GPIO_GROUP_A3A4:
		*sda = GPIO_PA3;
		*scl = GPIO_PA4;
		break;
	case I2C_GPIO_GROUP_B6D7:
		*sda = GPIO_PB6;
		*scl = GPIO_PD7;
		break;
	case I2C_GPIO_GROUP_C2C3:
		*sda = GPIO_PC2;
		*scl = GPIO_PC3;
		break;
	case I2C_GPIO_GROUP_C0C1:
	default:
		*sda = GPIO_PC0;
		*scl = GPIO_PC1;
		break;
	}
}

void bms_i2c_safe_setup(I2C_GPIO_GroupTypeDef group, unsigned char divClock)
{
	g_bms_i2c.group = group;
	g_bms_i2c.div = divClock;
	bms_i2c_select_pins(group, &g_bms_i2c.sda, &g_bms_i2c.scl);
	i2c_gpio_set(group);
	i2c_master_init(0, divClock);
	g_bms_i2c.inited = 1;
}

static inline u32 bms_i2c_get_timeout(u32 timeout_us)
{
	return timeout_us ? timeout_us : 1000;
}

static inline int bms_i2c_wait(u32 *start, u32 timeout_us)
{
	while (reg_i2c_status & FLD_I2C_CMD_BUSY)
	{
		if (clock_time_exceed(*start, timeout_us))
		{
			return -1;
		}
	}
	*start = clock_time();
	return 0;
}

static inline int bms_i2c_check_context(void)
{
	if (reg_irq_src)
	{
		bms_i2c_on_error(BMS_I2C_ERR_HEAVY, BMS_I2C_REASON_BAD_CONTEXT);
		return -1;
	}
	return 0;
}

static int bms_i2c_start_addr(u8 dev7, u16 reg, u8 reg_len, u32 *ts, u32 timeout_us)
{
	reg_i2c_id = (dev7 << 1) & 0xFE;
	if (reg_len == 0)
	{
		reg_i2c_ctrl = (FLD_I2C_CMD_ID | FLD_I2C_CMD_START);
	}
	else if (reg_len == 1)
	{
		reg_i2c_adr = (unsigned char)reg;
		reg_i2c_ctrl = (FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR | FLD_I2C_CMD_START);
	}
	else if (reg_len == 2)
	{
		reg_i2c_adr = (unsigned char)(reg >> 8);
		reg_i2c_do = (unsigned char)reg;
		reg_i2c_ctrl = (FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR | FLD_I2C_CMD_DO | FLD_I2C_CMD_START);
	}
	else
	{
		reg_i2c_adr = (unsigned char)(reg >> 16);
		reg_i2c_do = (unsigned char)(reg >> 8);
		reg_i2c_di = (unsigned char)reg;
		reg_i2c_ctrl = (FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR | FLD_I2C_CMD_DO | FLD_I2C_CMD_DI | FLD_I2C_CMD_START);
	}
	return bms_i2c_wait(ts, timeout_us);
}

static int bms_i2c_read_once(u8 dev7, u16 reg, u8 reg_len, u8 *buf, u16 len, u32 timeout_us)
{
	if (!len)
	{
		return 0;
	}
	u32 ts = clock_time();
	if (bms_i2c_start_addr(dev7, reg, reg_len, &ts, timeout_us))
	{
		return -1;
	}
	reg_i2c_id = (dev7 << 1) | 0x01;
	reg_i2c_ctrl = (FLD_I2C_CMD_ID | FLD_I2C_CMD_START);
	if (bms_i2c_wait(&ts, timeout_us))
	{
		return -1;
	}
	for (u16 i = 0; i < len; i++)
	{
		u32 cmd = (FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID);
		if (i == (len - 1))
		{
			cmd |= FLD_I2C_CMD_ACK;
		}
		reg_i2c_ctrl = cmd;
		if (bms_i2c_wait(&ts, timeout_us))
		{
			return -1;
		}
		buf[i] = reg_i2c_di;
	}
	reg_i2c_ctrl = FLD_I2C_CMD_STOP;
	return bms_i2c_wait(&ts, timeout_us);
}

static int bms_i2c_write_once(u8 dev7, u16 reg, u8 reg_len, const u8 *buf, u16 len, u32 timeout_us)
{
	u32 ts = clock_time();
	if (bms_i2c_start_addr(dev7, reg, reg_len, &ts, timeout_us))
	{
		return -1;
	}
	for (u16 i = 0; i < len; i++)
	{
		reg_i2c_di = buf[i];
		reg_i2c_ctrl = FLD_I2C_CMD_DI;
		if (bms_i2c_wait(&ts, timeout_us))
		{
			return -1;
		}
	}
	reg_i2c_ctrl = FLD_I2C_CMD_STOP;
	return bms_i2c_wait(&ts, timeout_us);
}

static int bms_i2c_try_read(u8 dev7, u16 reg, u8 reg_len, u8 *buf, u16 len, u32 timeout_us, u8 retry)
{
	u8 attempts = retry + 1;
	for (u8 i = 0; i < attempts; i++)
	{
		if (bms_i2c_read_once(dev7, reg, reg_len, buf, len, timeout_us) == 0)
		{
			return 0;
		}
		bms_i2c_on_error((i == attempts - 1) ? BMS_I2C_ERR_MID : BMS_I2C_ERR_LIGHT, BMS_I2C_REASON_TIMEOUT);
	}
	return -1;
}

static int bms_i2c_try_write(u8 dev7, u16 reg, u8 reg_len, const u8 *buf, u16 len, u32 timeout_us, u8 retry)
{
	u8 attempts = retry + 1;
	for (u8 i = 0; i < attempts; i++)
	{
		if (bms_i2c_write_once(dev7, reg, reg_len, buf, len, timeout_us) == 0)
		{
			return 0;
		}
		bms_i2c_on_error((i == attempts - 1) ? BMS_I2C_ERR_MID : BMS_I2C_ERR_LIGHT, BMS_I2C_REASON_TIMEOUT);
	}
	return -1;
}

int bms_i2c_read(u8 dev7, u16 reg, u8 reg_len, u8 *buf, u16 len, u32 timeout_us, u8 retry)
{
	if (!g_bms_i2c.inited)
	{
		bms_i2c_safe_setup(g_bms_i2c.group, g_bms_i2c.div);
	}
	if (bms_i2c_check_context())
	{
		return -1;
	}
	int ret = bms_i2c_try_read(dev7, reg, reg_len, buf, len, bms_i2c_get_timeout(timeout_us), retry);
	if (ret)
	{
		bms_i2c_on_error(BMS_I2C_ERR_HEAVY, BMS_I2C_REASON_RETRY_FAIL);
		bms_i2c_recover_bus();
		ret = bms_i2c_try_read(dev7, reg, reg_len, buf, len, bms_i2c_get_timeout(timeout_us), retry);
	}
	return ret;
}

int bms_i2c_write(u8 dev7, u16 reg, u8 reg_len, const u8 *buf, u16 len, u32 timeout_us, u8 retry)
{
	if (!g_bms_i2c.inited)
	{
		bms_i2c_safe_setup(g_bms_i2c.group, g_bms_i2c.div);
	}
	if (bms_i2c_check_context())
	{
		return -1;
	}
	int ret = bms_i2c_try_write(dev7, reg, reg_len, buf, len, bms_i2c_get_timeout(timeout_us), retry);
	if (ret)
	{
		bms_i2c_on_error(BMS_I2C_ERR_HEAVY, BMS_I2C_REASON_RETRY_FAIL);
		bms_i2c_recover_bus();
		ret = bms_i2c_try_write(dev7, reg, reg_len, buf, len, bms_i2c_get_timeout(timeout_us), retry);
	}
	return ret;
}

void bms_i2c_recover_bus(void)
{
	GPIO_PinTypeDef sda = g_bms_i2c.sda;
	GPIO_PinTypeDef scl = g_bms_i2c.scl;
	gpio_set_func(sda, AS_GPIO);
	gpio_set_func(scl, AS_GPIO);
	gpio_set_output_en(scl, 1);
	gpio_set_input_en(scl, 0);
	gpio_set_output_en(sda, 0);
	gpio_set_input_en(sda, 1);
	gpio_setup_up_down_resistor(sda, PM_PIN_PULLUP_10K);
	gpio_write(scl, 1);
	sleep_us(5);
	if (!gpio_read(sda))
	{
		for (int i = 0; i < 9; i++)
		{
			gpio_write(scl, 0);
			sleep_us(5);
			gpio_write(scl, 1);
			sleep_us(5);
			if (gpio_read(sda))
			{
				break;
			}
		}
	}
	gpio_set_output_en(sda, 1);
	gpio_write(sda, 0);
	sleep_us(5);
	gpio_write(scl, 1);
	sleep_us(5);
	gpio_write(sda, 1);
	gpio_set_output_en(sda, 0);

	reset_i2c_moudle();
	i2c_gpio_set(g_bms_i2c.group);
	i2c_master_init(0, g_bms_i2c.div);
}
