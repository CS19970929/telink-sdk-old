/********************************************************************************************************
 * @file	app.c
 *
 * @brief	This is the source file for BLE SDK
 *
 * @author	BLE GROUP
 * @date	06,2020
 *
 * @par     Copyright (c) 2020, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *          All rights reserved.
 *
 *          Redistribution and use in source and binary forms, with or without
 *          modification, are permitted provided that the following conditions are met:
 *
 *              1. Redistributions of source code must retain the above copyright
 *              notice, this list of conditions and the following disclaimer.
 *
 *              2. Unless for usage inside a TELINK integrated circuit, redistributions
 *              in binary form must reproduce the above copyright notice, this list of
 *              conditions and the following disclaimer in the documentation and/or other
 *              materials provided with the distribution.
 *
 *              3. Neither the name of TELINK, nor the names of its contributors may be
 *              used to endorse or promote products derived from this software without
 *              specific prior written permission.
 *
 *              4. This software, with or without modification, must only be used with a
 *              TELINK integrated circuit. All other usages are subject to written permission
 *              from TELINK and different commercial license may apply.
 *
 *              5. Licensee shall be solely responsible for any claim to the extent arising out of or
 *              relating to such deletion(s), modification(s) or alteration(s).
 *
 *          THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 *          ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *          WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *          DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER BE LIABLE FOR ANY
 *          DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *          (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *          LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *          ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *          (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *          SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "app.h"
#include "app_att.h"

#include "application/keyboard/keyboard.h"
#include "application/usbstd/usbkeycode.h"

#include "app_config.h"
#include "param.h"

#include "storage/st_storage.h"
#include "storage/st_test.h"
#include "sh367309_datadeal.h"
#include "stdint.h"
#include "sci_upper.h"
#include "SocEnhance.h"
#include "sif_send.h"
#include "soc_kv_store.h"
#include "modbus_uart.h"
#include "modbus_rtu.h"

u8 tbl_advData[31];
u8 tbl_advDataLen;

u8 tbl_scanRsp[31];
u8 tbl_scanRspLen;

bool deepsleep_en = false;

static void ble_build_adv_scanrsp(void)
{
	u8 i = 0;

	// --- ADV: 放 Flags + Appearance + UUID list（建议 ADV 不放名字，名字放 scanRsp） ---
	i = 0;
	// Flags: len=2, type=0x01, data=0x05
	tbl_advData[i++] = 0x02;
	tbl_advData[i++] = 0x01;
	tbl_advData[i++] = 0x05;

	// Appearance: len=3, type=0x19, data=0x0180
	tbl_advData[i++] = 0x03;
	tbl_advData[i++] = 0x19;
	tbl_advData[i++] = 0x80;
	tbl_advData[i++] = 0x01;

	// Incomplete 16-bit UUIDs: len=5, type=0x02, 0x1812, 0x180F
	tbl_advData[i++] = 0x05;
	tbl_advData[i++] = 0x02;
	tbl_advData[i++] = 0x12;
	tbl_advData[i++] = 0x18;
	tbl_advData[i++] = 0x0F;
	tbl_advData[i++] = 0x18;

	tbl_advDataLen = i;

	// --- ScanRsp: 放完整名字 ---
	i = 0;
	tbl_scanRsp[i++] = (u8)(DEV_NAME_LEN + 1); // len = type(1)+name
	tbl_scanRsp[i++] = 0x09;				   // Complete Local Name
	memcpy(&tbl_scanRsp[i], DEV_NAME_STR, DEV_NAME_LEN);
	i += DEV_NAME_LEN;

	tbl_scanRspLen = i;
}

volatile struct SYSTEM_ERROR System_ErrFlag;

extern SH367309_REG_STORE SH367309_Reg_Store;

typedef enum
{
	ADC_APP_CH0 = 0,
	ADC_APP_CH1 = 1,
	ADC_APP_CH2 = 2,
	ADC_APP_CH_MAX = 3,
} adc_app_ch_t;

/* 业务层通道配置：引脚 + 是否启用 */
typedef struct
{
	GPIO_PinTypeDef pin; // 必须 PB0~PB7 / PC4 / PC5
	uint8_t enable;		 // 1启用 0禁用
} adc_app_ch_cfg_t;

/* ============ 1) DEMO 同款固定参数 ============ */
#define ADC_DEMO_SAMPLE_CLK_DIV 5 // 24M/(1+5)=4MHz
#define ADC_DEMO_STATE_CAPTURE 240
#define ADC_DEMO_STATE_SET 10
#define ADC_DEMO_RES RES14
#define ADC_DEMO_VREF ADC_VREF_1P2V
#define ADC_DEMO_TSAMPLE SAMPLING_CYCLES_6
#define ADC_DEMO_PRESCALER ADC_PRESCALER_1F8

/* 你 demo 里采样函数一般默认 8 次（你的 sdk 有 ADC_SAMPLE_NUM=8） */
#define ADC_APP_PERIOD_US 200000 // 200ms

/* ============ 2) ADC 支持脚表（与你 sdk 一致） ============ */
static GPIO_PinTypeDef s_adc_gpio_tab[10] = {
	GPIO_PB0, GPIO_PB1, GPIO_PB2, GPIO_PB3,
	GPIO_PB4, GPIO_PB5, GPIO_PB6, GPIO_PB7,
	GPIO_PC4, GPIO_PC5};

typedef struct
{
	int16_t temp; // 0.1℃
	uint32_t ohm; // Ω
} ntc_t;

static const ntc_t ntc_10k_tab[] = {
	{-200, 69434},
	{-100, 66089},
	{0, 27513},
	{100, 18016},
	{200, 12092},
	{250, 10000},
	{300, 8314},
	{400, 5838},
	{500, 4310},
	{600, 3147},
	{700, 2310},
	{800, 1730},
};
#define NTC_TAB_SIZE (sizeof(ntc_10k_tab) / sizeof(ntc_10k_tab[0]))

#define NTC_RPULL_OHM 10000
#define NTC_VREF_MV 3300 // 如果是接 3.3V 上拉

static uint32_t ntc_adc_to_res_ohm(uint32_t adc_mv)
{
	uint32_t num;
	uint32_t den;

	if (adc_mv <= 1)
		return 1000000; // open
	if (adc_mv >= NTC_VREF_MV - 1)
		return 1; // short

	num = NTC_RPULL_OHM * adc_mv; // <= 33,000,000 fits in uint32
	den = (NTC_VREF_MV - adc_mv);

	return num / den;
}

static int16_t ntc_res_to_temp_01c(uint32_t r)
{
	if (r >= ntc_10k_tab[0].ohm)
		return ntc_10k_tab[0].temp;
	if (r <= ntc_10k_tab[NTC_TAB_SIZE - 1].ohm)
		return ntc_10k_tab[NTC_TAB_SIZE - 1].temp;

	for (int i = 0; i < NTC_TAB_SIZE - 1; i++)
	{
		uint32_t r1 = ntc_10k_tab[i].ohm;
		uint32_t r2 = ntc_10k_tab[i + 1].ohm;

		if (r <= r1 && r >= r2)
		{
			int32_t t1 = ntc_10k_tab[i].temp;
			int32_t t2 = ntc_10k_tab[i + 1].temp;

			// 线性插值
			return t1 + (t2 - t1) * (int32_t)(r1 - r) / (int32_t)(r1 - r2);
		}
	}
	return 250; // fallback 25℃
}

int16_t ntc_adc_mv_to_temp_01c(uint32_t adc_mv)
{
	uint32_t r = ntc_adc_to_res_ohm(adc_mv);
	return ntc_res_to_temp_01c(r);
}

/* pin -> B0P(1) ... C5P(10) */
static uint8_t adc_pin_to_inpch(GPIO_PinTypeDef pin)
{
	for (uint8_t i = 0; i < 10; i++)
	{
		if (pin == s_adc_gpio_tab[i])
			return (uint8_t)(i + 1);
	}
	return 0;
}

/* ============ 3) 通道运行态（工程化架构的核心） ============ */
typedef struct
{
	GPIO_PinTypeDef pin;
	uint8_t enable;
	uint16_t mv; // 最新值（mV）
} adc_app_ch_state_t;

static adc_app_ch_state_t s_ch[ADC_APP_CH_MAX];
static uint32_t s_tick;

/* ============ 4) DEMO 同款：GPIO 设为模拟输入态 ============ */
static void adc_pin_analog_init(GPIO_PinTypeDef pin)
{
	gpio_set_func(pin, AS_GPIO);
	gpio_set_input_en(pin, 0);
	gpio_set_output_en(pin, 0);
	gpio_write(pin, 0);
}

/* ============ 5) DEMO 同款：切换 MISC 差分输入 ============ */
static void adc_misc_switch_to_pin(GPIO_PinTypeDef pin)
{
	uint8_t inpch = adc_pin_to_inpch(pin);
	if (!inpch)
		return; // 非 ADC 支持脚

#if (MCU_CORE_TYPE == MCU_CORE_825x)
	adc_set_ain_channel_differential_mode(ADC_MISC_CHN, (ADC_InputPchTypeDef)inpch, GND);
#else
	// 827x 你工程若用另一套 API，这里对应改一下
	adc_set_ain_channel_differential_mode((ADC_InputPchTypeDef)inpch, GND);
#endif
}

/* ============ 6) DEMO 同款：一次性初始化（app_adc_test_init 的工程版） ============ */
static void adc_demo_style_init_common(void)
{
	/* Step1: power off sar adc */
	adc_power_on_sar_adc(0);

	/* Step2: common adc settings */
	adc_enable_clk_24m_to_sar_adc(1);
	adc_set_sample_clk(ADC_DEMO_SAMPLE_CLK_DIV);

#if (MCU_CORE_TYPE == MCU_CORE_8258)
	adc_set_left_gain_bias(GAIN_STAGE_BIAS_PER100);
	adc_set_right_gain_bias(GAIN_STAGE_BIAS_PER100);
#endif

	/* Step3: misc channel settings（跟 demo 一致） */
	adc_set_chn_enable_and_max_state_cnt(ADC_MISC_CHN, 2);

#if (MCU_CORE_TYPE == MCU_CORE_8278)
	adc_set_state_length(ADC_DEMO_STATE_CAPTURE, ADC_DEMO_STATE_SET);
#else
	adc_set_state_length(ADC_DEMO_STATE_CAPTURE, 0, ADC_DEMO_STATE_SET);
#endif

#if (MCU_CORE_TYPE == MCU_CORE_825x)
	adc_set_resolution(ADC_MISC_CHN, ADC_DEMO_RES);
	adc_set_ref_voltage(ADC_MISC_CHN, ADC_DEMO_VREF);
	adc_set_tsample_cycle(ADC_MISC_CHN, ADC_DEMO_TSAMPLE);
#else
	adc_set_resolution(ADC_DEMO_RES);
	adc_set_ref_voltage(ADC_DEMO_VREF);
	adc_set_tsample_cycle(ADC_DEMO_TSAMPLE);
#endif

	adc_set_ain_pre_scaler(ADC_DEMO_PRESCALER);

	/* Step4: power on sar adc */
	adc_power_on_sar_adc(1);
}

// adc_mv  : ADC采样值(mV)
// vref_mv : 上拉电阻所接电源(mV)，例如 3300 或 1200
// r_pull  : 上拉电阻 (Ω)，你的 = 10000
static uint32_t ntc_calc_res_ohm(uint32_t adc_mv, uint32_t vref_mv, uint32_t r_pull)
{
	if (adc_mv == 0)
		return 10000000; // 开路
	if (adc_mv >= vref_mv)
		return 1; // 短路

	// Rntc = Rpull * Vadc / (Vref - Vadc)
	return (r_pull * adc_mv) / (vref_mv - adc_mv);
}

/* ============ 7) 对外 API ============ */
void adc_app_init(const adc_app_ch_cfg_t cfg[ADC_APP_CH_MAX])
{
	memset(s_ch, 0, sizeof(s_ch));

	for (int i = 0; i < ADC_APP_CH_MAX; i++)
	{
		s_ch[i].pin = cfg[i].pin;
		s_ch[i].enable = cfg[i].enable ? 1 : 0;
		s_ch[i].mv = 0;

		/* 只要启用且 pin 合法，就把 GPIO 设成模拟输入态 */
		if (s_ch[i].enable && adc_pin_to_inpch(s_ch[i].pin))
		{
			adc_pin_analog_init(s_ch[i].pin);
		}
		else
		{
			s_ch[i].enable = 0; // pin 不合法直接禁用
		}
	}

	adc_demo_style_init_common();

	/* 默认切到 CH0（避免第一次采样通道未知） */
	if (s_ch[0].enable)
	{
		adc_misc_switch_to_pin(s_ch[0].pin);
	}

	s_tick = clock_time();
}

int adc_app_set_channel(adc_app_ch_t ch, GPIO_PinTypeDef pin, uint8_t enable)
{
	if ((unsigned)ch >= ADC_APP_CH_MAX)
		return -1;

	if (enable)
	{
		if (!adc_pin_to_inpch(pin))
			return -2; // 非 ADC 支持脚
		s_ch[ch].pin = pin;
		s_ch[ch].enable = 1;
		adc_pin_analog_init(pin);
	}
	else
	{
		s_ch[ch].enable = 0;
		s_ch[ch].mv = 0;
	}
	return 0;
}

uint16_t adc_app_get_mv(adc_app_ch_t ch)
{
	if ((unsigned)ch >= ADC_APP_CH_MAX)
		return 0;
	if (!s_ch[ch].enable)
		return 0;
	return s_ch[ch].mv;
}

void adc_app_process_200ms(void)
{
	/* 轮询采 3 路：切通道 -> 直接用你 sdk 的 adc_sample_and_get_result() */
	for (int i = 0; i < ADC_APP_CH_MAX; i++)
	{
		if (!s_ch[i].enable)
			continue;

		adc_misc_switch_to_pin(s_ch[i].pin);

		/* 你 demo 的函数：返回单位 mV */
		s_ch[i].mv = (uint16_t)adc_sample_and_get_result();
	}
}

struct stCell_Info g_stCellInfoReport;

#define ADV_IDLE_ENTER_DEEP_TIME 60	 // 60 s
#define CONN_IDLE_ENTER_DEEP_TIME 60 // 60 s

#define MY_DIRECT_ADV_TMIE 2000000

#define MY_APP_ADV_CHANNEL BLT_ENABLE_ADV_ALL
#define MY_ADV_INTERVAL_MIN ADV_INTERVAL_30MS
#define MY_ADV_INTERVAL_MAX ADV_INTERVAL_35MS

#define MY_RF_POWER_INDEX RF_POWER_P3dBm

#define BLE_DEVICE_ADDRESS_TYPE BLE_DEVICE_ADDRESS_PUBLIC

_attribute_data_retention_ own_addr_type_t app_own_address_type = OWN_ADDRESS_PUBLIC;

/**
 * @brief      LinkLayer RX & TX FIFO configuration
 */
#define RX_FIFO_SIZE 64
#define RX_FIFO_NUM 8

#define TX_FIFO_SIZE 40
#define TX_FIFO_NUM 16

_attribute_data_retention_ u8 blt_rxfifo_b[RX_FIFO_SIZE * RX_FIFO_NUM] = {0};
_attribute_data_retention_ my_fifo_t blt_rxfifo = {
	RX_FIFO_SIZE,
	RX_FIFO_NUM,
	0,
	0,
	blt_rxfifo_b,
};

_attribute_data_retention_ u8 blt_txfifo_b[TX_FIFO_SIZE * TX_FIFO_NUM] = {0};
_attribute_data_retention_ my_fifo_t blt_txfifo = {
	TX_FIFO_SIZE,
	TX_FIFO_NUM,
	0,
	0,
	blt_txfifo_b,
};

/**
 * @brief	Adv Packet data
 */

// const u8	tbl_advData[] = {
// 	 0x05, 0x09, 'h', 'a', 'n', 's', 't', 'a', 'r',
// 	 0x02, 0x01, 0x05, 							// BLE limited discoverable mode and BR/EDR not supported
// 	 0x03, 0x19, 0x80, 0x01, 					// 384, Generic Remote Control, Generic category
// 	 0x05, 0x02, 0x12, 0x18, 0x0F, 0x18,		// incomplete list of service class UUIDs (0x1812, 0x180F)
// };
// const u8	tbl_advData[] = {
// 	 0x05, 0x09, 'V', 'H', 'I', 'D',
// 	 0x02, 0x01, 0x05, 							// BLE limited discoverable mode and BR/EDR not supported
// 	 0x03, 0x19, 0x80, 0x01, 					// 384, Generic Remote Control, Generic category
// 	 0x05, 0x02, 0x12, 0x18, 0x0F, 0x18,		// incomplete list of service class UUIDs (0x1812, 0x180F)
// };
// const u8	tbl_advData[] = {
// 	 0x05, 0x09, 'S', 'T', 'A', 'R',
// 	 0x02, 0x01, 0x05, 							// BLE limited discoverable mode and BR/EDR not supported
// 	 0x03, 0x19, 0x80, 0x01, 					// 384, Generic Remote Control, Generic category
// 	 0x05, 0x02, 0x12, 0x18, 0x0F, 0x18,		// incomplete list of service class UUIDs (0x1812, 0x180F)
// };

// adc_mv: ADC 引脚电压(mV)
int16_t ntc_adc_to_temp_01c(uint32_t adc_mv)
{
	const uint32_t VREF_MV = 3300; // 你的NTC上拉电源
	const uint32_t RPULL = 10000;  // 10k

	uint32_t r_ntc = ntc_calc_res_ohm(adc_mv, VREF_MV, RPULL);
	return ntc_res_to_temp_01c(r_ntc);
}

_attribute_data_retention_ int device_in_connection_state;
_attribute_data_retention_ u32 advertise_begin_tick;
_attribute_data_retention_ u32 interval_update_tick;
_attribute_data_retention_ u8 sendTerminate_before_enterDeep = 0;
_attribute_data_retention_ u32 latest_user_event_tick;

#if (UI_KEYBOARD_ENABLE)

_attribute_data_retention_ int key_not_released;
_attribute_data_retention_ u8 key_type;
_attribute_data_retention_ static u32 keyScanTick = 0;

extern u32 scan_pin_need;

#define CONSUMER_KEY 1
#define KEYBOARD_KEY 2

_attribute_data_retention_ u8 ota_is_working = 0;
void app_enter_ota_mode(void)
{
	ota_is_working = 1;
	bls_pm_setSuspendMask(SUSPEND_DISABLE);
	bls_pm_setManualLatency(0);
	// #if (BLT_APP_LED_ENABLE)
	// 	device_led_setup(led_cfg[LED_SHINE_OTA]);
	// #endif
	// bls_ota_setTimeout(15 * 1000 * 1000); //set OTA timeout  15 seconds
}

void app_timer_test_init(void)
{
	// timer0 10ms interval irq
	reg_irq_mask |= FLD_IRQ_TMR0_EN;
	reg_tmr0_tick = 0; // clear counter
	// reg_tmr0_capt = 1 * CLOCK_SYS_CLOCK_1MS;
	reg_tmr0_capt = 500 * CLOCK_SYS_CLOCK_1US;
	reg_tmr_sta = FLD_TMR_STA_TMR0; // clear irq status
	reg_tmr_ctrl |= FLD_TMR0_EN;	// start timer

#if 0
	//timer1 15ms interval irq
	reg_irq_mask |= FLD_IRQ_TMR1_EN;
	reg_tmr1_tick = 0; //clear counter
	reg_tmr1_capt = 15 * CLOCK_SYS_CLOCK_1MS;
	reg_tmr_sta = FLD_TMR_STA_TMR1; //clear irq status
	reg_tmr_ctrl |= FLD_TMR1_EN;  //start timer


	//timer2 20ms interval irq
	reg_irq_mask |= FLD_IRQ_TMR2_EN;
	reg_tmr2_tick = 0; //clear counter
	reg_tmr2_capt = 20 * CLOCK_SYS_CLOCK_1MS;
	reg_tmr_sta = FLD_TMR_STA_TMR2; //clear irq status
	reg_tmr_ctrl |= FLD_TMR2_EN;  //start timer
#endif

	irq_enable();
}

/**
 * @brief		this function is used to process keyboard matrix status change.
 * @param[in]	none
 * @return      none
 */
void key_change_proc(void)
{
	latest_user_event_tick = clock_time(); // record latest key change time

	u8 key0 = kb_event.keycode[0];
	u8 key_buf[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	key_not_released = 1;
	if (kb_event.cnt == 2) // two key press, do  not process
	{
	}
	else if (kb_event.cnt == 1)
	{
		if (key0 >= CR_VOL_UP) // volume up/down
		{
			key_type = CONSUMER_KEY;
			u16 consumer_key;
			if (key0 == CR_VOL_UP)
			{ // volume up
				consumer_key = MKEY_VOL_UP;
			}
			else if (key0 == CR_VOL_DN)
			{ // volume down
				consumer_key = MKEY_VOL_DN;
			}
			blc_gatt_pushHandleValueNotify(BLS_CONN_HANDLE, HID_CONSUME_REPORT_INPUT_DP_H, (u8 *)&consumer_key, 2);
		}
		else
		{
			key_type = KEYBOARD_KEY;
			key_buf[2] = key0;
			blc_gatt_pushHandleValueNotify(BLS_CONN_HANDLE, HID_NORMAL_KB_REPORT_INPUT_DP_H, key_buf, 8);
		}
	}
	else // kb_event.cnt == 0,  key release
	{
		key_not_released = 0;
		if (key_type == CONSUMER_KEY)
		{
			u16 consumer_key = 0;
			blc_gatt_pushHandleValueNotify(BLS_CONN_HANDLE, HID_CONSUME_REPORT_INPUT_DP_H, (u8 *)&consumer_key, 2);
		}
		else if (key_type == KEYBOARD_KEY)
		{
			key_buf[2] = 0;
			blc_gatt_pushHandleValueNotify(BLS_CONN_HANDLE, HID_NORMAL_KB_REPORT_INPUT_DP_H, key_buf, 8); // release
		}
	}
}

/**
 * @brief      this function is used to detect if key pressed or released.
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
_attribute_ram_code_ void proc_keyboard(u8 e, u8 *p, int n)
{
	if (clock_time_exceed(keyScanTick, 8000))
	{
		keyScanTick = clock_time();
	}
	else
	{
		return;
	}

	kb_event.keycode[0] = 0;
	int det_key = kb_scan_key(0, 1);

	if (det_key)
	{
		key_change_proc();
	}
}

#elif (UI_BUTTON_ENABLE)

_attribute_data_retention_ static int button_detect_en = 0;
_attribute_data_retention_ static u32 button_detect_tick = 0;

#endif

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_SUSPEND_ENTER"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void ble_remote_set_sleep_wakeup(u8 e, u8 *p, int n)
{
	if (blc_ll_getCurrentState() == BLS_LINK_STATE_CONN && ((u32)(bls_pm_getSystemWakeupTick() - clock_time())) > 80 * SYSTEM_TIMER_TICK_1MS)
	{										   // suspend time > 30ms.add gpio wakeup
		bls_pm_setWakeupSource(PM_WAKEUP_PAD); // gpio pad wakeup suspend/deepsleep
	}
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_ADV_DURATION_TIMEOUT"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void app_switch_to_indirect_adv(u8 e, u8 *p, int n)
{

	bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
					   ADV_TYPE_CONNECTABLE_UNDIRECTED, app_own_address_type,
					   0, NULL,
					   MY_APP_ADV_CHANNEL,
					   ADV_FP_NONE);

	bls_ll_setAdvEnable(1); // must: set adv enable
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_TERMINATE"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void task_terminate(u8 e, u8 *p, int n) //*p is terminate reason
{
	device_in_connection_state = 0;

	if (*p == HCI_ERR_CONN_TIMEOUT)
	{
	}
	else if (*p == HCI_ERR_REMOTE_USER_TERM_CONN)
	{ // 0x13
	}
	else if (*p == HCI_ERR_CONN_TERM_MIC_FAILURE)
	{
	}
	else
	{
	}

#if (UI_LED_ENABLE)
	gpio_write(GPIO_LED_RED, !LED_ON_LEVAL);
	gpio_write(GPIO_LED_BLUE, !LED_ON_LEVAL);
#endif

#if (BLE_APP_PM_ENABLE)
	// user has push terminate pkt to ble TX buffer before deepsleep
	if (sendTerminate_before_enterDeep == 1)
	{
		sendTerminate_before_enterDeep = 2;
	}
#endif

	advertise_begin_tick = clock_time();
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_SUSPEND_EXIT"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
_attribute_ram_code_ void user_set_rf_power(u8 e, u8 *p, int n)
{
	rf_set_power_level_index(MY_RF_POWER_INDEX);
}

/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_CONNECT"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void task_connect(u8 e, u8 *p, int n)
{

#if (!UI_BUTTON_ENABLE) // if use button, do not use big latency
	//	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 19, CONN_TIMEOUT_4S);  // 200mS
	// bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 99, CONN_TIMEOUT_4S);  // 1 S
	bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 0, CONN_TIMEOUT_4S); // 1 S
//	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 149, CONN_TIMEOUT_8S);  // 1.5 S
//	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 199, CONN_TIMEOUT_8S);  // 2 S
//	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 249, CONN_TIMEOUT_8S);  // 2.5 S
//	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 299, CONN_TIMEOUT_8S);  // 3 S
#endif

	latest_user_event_tick = clock_time();

	device_in_connection_state = 1; //

	interval_update_tick = clock_time() | 1; // none zero

#if (UI_LED_ENABLE)
	gpio_write(GPIO_LED_RED, LED_ON_LEVAL);
	gpio_write(GPIO_LED_BLUE, !LED_ON_LEVAL);
#endif
}

int timer0_irq_cnt = 0;
_attribute_ram_code_ void app_timer_test_irq_proc(void)
{
	// gpio_toggle(GPIO_PC3);
	if (reg_tmr_sta & FLD_TMR_STA_TMR0)
	{
		sif_send_data_handle();
		reg_tmr_sta = FLD_TMR_STA_TMR0; // clear irq status
		timer0_irq_cnt++;
		// gpio_toggle(GPIO_PC3);
		if (timer0_irq_cnt >= 200)
		{
			timer0_irq_cnt = 0;
			// gpio_toggle(GPIO_PC3);
		}
		// DBG_CHN0_TOGGLE;
	}

	// if(reg_tmr_sta & FLD_TMR_STA_TMR1){
	// 	reg_tmr_sta = FLD_TMR_STA_TMR1; //clear irq status
	// 	timer1_irq_cnt ++;
	// 	DBG_CHN1_TOGGLE;
	// }

	// if(reg_tmr_sta & FLD_TMR_STA_TMR2){
	// 	reg_tmr_sta = FLD_TMR_STA_TMR2; //clear irq status
	// 	timer2_irq_cnt ++;
	// 	DBG_CHN2_TOGGLE;
	// }
}

/**
 * @brief      power management code for application
 * @param	   none
 * @return     none
 */
_attribute_ram_code_ void blt_pm_proc(void)
{
	static u16 sleep_cnt = 0;
	static u16 sleep_vlow_cnt = 0;
	_attribute_data_retention_ static u32 sleep_tick = 0;
	if (clock_time_exceed(sleep_tick, 1000 * 1000))
	{
		sleep_tick = clock_time();
		if (gpio_read(CHG_IN_PIN))
		{
			if (gpio_read(SW_PIN))
			{
				if (++sleep_cnt >= 5)
				{
					sleep_cnt = 0;
					// printf("0x5v %d\n", gpio_read(CHG_IN_PIN));
					// printf("0xkey %d\n", gpio_read(SW_PIN));
					// gpio_write(AFE_CTL_PIN, 0);
					AFE_Sleep();
					cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0); // deepsleep
				}
			}
		}
		if ((g_stCellInfoReport.u16VCellMin <= 3000 && !g_stCellInfoReport.u16Ichg) || deepsleep_en)
		{
			if(deepsleep_en) sleep_vlow_cnt = 60;
			if (++sleep_vlow_cnt >= (60))
			{
				cpu_set_gpio_wakeup(SW_PIN, Level_Low, 0);

				sleep_vlow_cnt = 0;
				AFE_Sleep();
				cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0); // deepsleep
			}
		}
	}
#if (BLE_APP_PM_ENABLE)
	if (!ota_is_working)
	{
#if (PM_DEEPSLEEP_RETENTION_ENABLE)
		bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
#else
		bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
		// bls_pm_setSuspendMask (SUSPEND_ADV);
#endif
	}

	// do not care about keyScan/button_detect power here, if you care about this, please refer to "8258_ble_remote" demo
	//  #if (UI_KEYBOARD_ENABLE)
	//  	if(scan_pin_need || key_not_released){
	//  		bls_pm_setSuspendMask (SUSPEND_DISABLE);
	//  	}
	//  #endif

#if 0
#if (!TEST_CONN_CURRENT_ENABLE) // test connection power, should disable deepSleep
			if(sendTerminate_before_enterDeep == 2){  //Terminate OK
				analog_write(USED_DEEP_ANA_REG, analog_read(USED_DEEP_ANA_REG) | CONN_DEEP_FLG);
				cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);  //deepSleep
			}


			if(  !blc_ll_isControllerEventPending() ){  //no controller event pending
				//adv 60s, deepsleep
				if( blc_ll_getCurrentState() == BLS_LINK_STATE_ADV && !sendTerminate_before_enterDeep && \
					clock_time_exceed(advertise_begin_tick , ADV_IDLE_ENTER_DEEP_TIME * 1000000))
				{
					cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);  //deepsleep
				}
				//conn 60s no event(key/voice/led), enter deepsleep
				else if( device_in_connection_state && \
						clock_time_exceed(latest_user_event_tick, CONN_IDLE_ENTER_DEEP_TIME * 1000000) )
				{

					bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN); //push terminate cmd into ble TX buffer
					bls_ll_setAdvEnable(0);   //disable adv
					sendTerminate_before_enterDeep = 1;
				}
			}
#endif							// end of !TEST_CONN_CURRENT_ENABLE
#endif							// end of BLE_APP_PM_ENABLE
#endif							// end of BLE_APP_PM_ENABLE
}

void i2c_master_test_init(void)
{

	// I2C pin set
#if (MCU_CORE_TYPE == MCU_CORE_827x)
	i2c_gpio_set(I2C_GPIO_SDA_C0, I2C_GPIO_SCL_C1); // SDA/CK : C0/C1
#elif (MCU_CORE_TYPE == MCU_CORE_825x)
	i2c_gpio_set(I2C_GPIO_GROUP_C0C1); // SDA/CK : C0/C1
#endif

	// slave device id 0x5C(write) 0x5D(read)
	// i2c clock 200K, only master need set i2c clock
	//  i2c_master_init(0x34, (unsigned char)(CLOCK_SYS_CLOCK_HZ/(4*200000)) );
	//  i2c_master_init(0x34, (unsigned char)(CLOCK_SYS_CLOCK_HZ/(4*400000)) );
	i2c_master_init(AFE_ID, (unsigned char)(CLOCK_SYS_CLOCK_HZ / (4 * 100000)));
}

volatile unsigned char i2c_master_rx_buff[0x71 - 0x40 + 1 + 1] = {0};

float RSENSE = 0.001;
// float Sh_GetCadcCurrent(u16 *current)
void i2c_master_mainloop(void)
{
#define SLAVE_DMA_MODE_OTHER_DEV_WRITE (0x46)
#define SLAVE_DMA_MODE_OTHER_DEV_READ (0x40)
	u8 addr = SLAVE_DMA_MODE_OTHER_DEV_READ;
	u8 len = (0x71 - 0x40 + 1); // 鎵嬪唽璇达細闀垮害涓嶅寘鍚獵RC
	// i2c_master_tx_buff[0] += 1;
	// 825x slave dma mode, sram address(0x40000~0x4FFFF) length should be 3 byte
	// i2c_write_series(SLAVE_DMA_MODE_OTHER_DEV_WRITE, 1, (unsigned char *)i2c_master_tx_buff, DBG_DATA_LEN);
	// WaitMs(100);   //1 S
	// i2c_read_series(((u16)addr << 8) | len, 2, (unsigned char *)i2c_master_rx_buff, len + 1);
	// i2c_read_series(((u16)addr << 8) | len, 2, (unsigned char *)i2c_master_rx_buff, len);
	i2c_read_series(((u16)addr << 8) | len, 2, (unsigned char *)&ram_reg_309, len);
	// array_printf(i2c_master_rx_buff, len);
	// Sh_GetCadcCurrent();
	App_AFEGet();
	// printf("current %d,%d", g_stCellInfoReport.u16Ichg, g_stCellInfoReport.u16IDischg);

#if 0
		/*********** copy the data read by i2c master from slave for debug  ****************/
		memcpy( (unsigned char *)(master_rx_buff_debug + master_rx_index*DBG_DATA_LEN), (unsigned char *)i2c_master_rx_buff, DBG_DATA_LEN);
		master_rx_index ++;
		if(master_rx_index>=DBG_DATA_NUM){
			master_rx_index = 0;
		}
#endif
}

/**
 * @brief		user initialization when MCU power on or wake_up from deepSleep mode
 * @param[in]	none
 * @return      none
 */
void user_init_normal(void)
{
	// random number generator must be initiated here( in the beginning of user_init_nromal)
	// when deepSleep retention wakeUp, no need initialize again
	random_generator_init(); // this is must

	////////////////// BLE stack initialization ////////////////////////////////////
	u8 mac_public[6];
	u8 mac_random_static[6];
	// for 512K Flash, flash_sector_mac_address equals to 0x76000
	// for 1M  Flash, flash_sector_mac_address equals to 0xFF000
	blc_initMacAddress(flash_sector_mac_address, mac_public, mac_random_static);

#if (BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_PUBLIC)
	app_own_address_type = OWN_ADDRESS_PUBLIC;
#elif (BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_RANDOM_STATIC)
	app_own_address_type = OWN_ADDRESS_RANDOM;
	blc_ll_setRandomAddr(mac_random_static);
#endif

	////// Controller Initialization  //////////
	blc_ll_initBasicMCU();					   // mandatory
	blc_ll_initStandby_module(mac_public);	   // mandatory
	blc_ll_initAdvertising_module(mac_public); // adv module: 		 mandatory for BLE slave,
	blc_ll_initConnection_module();			   // connection module  mandatory for BLE slave/master
	blc_ll_initSlaveRole_module();			   // slave module: 	 mandatory for BLE slave,
	blc_ll_initPowerManagement_module();	   // pm module:      	 optional

	////// Host Initialization  //////////
	blc_gap_peripheral_init();							  // gap initialization
	my_att_init();										  // gatt initialization
	blc_l2cap_register_handler(blc_l2cap_packet_receive); // l2cap initialization

	// Smp Initialization may involve flash write/erase(when one sector stores too much information,
	//    is about to exceed the sector threshold, this sector must be erased, and all useful information
	//    should re_stored) , so it must be done after battery check
#if (BLE_REMOTE_SECURITY_ENABLE)
	blc_smp_peripheral_init();
#else
	blc_smp_setSecurityLevel(No_Security);
#endif

	///////////////////// USER application initialization ///////////////////
	ble_build_adv_scanrsp();
	bls_ll_setAdvData((u8 *)tbl_advData, sizeof(tbl_advData));
	bls_ll_setScanRspData((u8 *)tbl_scanRsp, sizeof(tbl_scanRsp));

	////////////////// config adv packet /////////////////////
#if (BLE_REMOTE_SECURITY_ENABLE)
	u8 bond_number = blc_smp_param_getCurrentBondingDeviceNumber(); // get bonded device number
	smp_param_save_t bondInfo;
	if (bond_number) // at least 1 bonding device exist
	{
		bls_smp_param_loadByIndex(bond_number - 1, &bondInfo); // get the latest bonding device (index: bond_number-1 )
	}

	if (bond_number) // set direct adv
	{
		// set direct adv
		u8 status = bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
									   ADV_TYPE_CONNECTABLE_DIRECTED_LOW_DUTY, app_own_address_type,
									   bondInfo.peer_addr_type, bondInfo.peer_addr,
									   MY_APP_ADV_CHANNEL,
									   ADV_FP_NONE);
		if (status != BLE_SUCCESS)
		{
			while (1)
				;
		} // debug: adv setting err

		// it is recommended that direct adv only last for several seconds, then switch to indirect adv
		bls_ll_setAdvDuration(MY_DIRECT_ADV_TMIE, 1);
		bls_app_registerEventCallback(BLT_EV_FLAG_ADV_DURATION_TIMEOUT, &app_switch_to_indirect_adv);
	}
	else // set indirect adv
#endif
	{
		u8 status = bls_ll_setAdvParam(MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
									   ADV_TYPE_CONNECTABLE_UNDIRECTED, app_own_address_type,
									   0, NULL,
									   MY_APP_ADV_CHANNEL,
									   ADV_FP_NONE);
		if (status != BLE_SUCCESS)
		{
			while (1)
				;
		} // debug: adv setting err
	}

	bls_ll_setAdvEnable(1); // adv enable

	blc_ota_initOtaServer_module();

	// set rf power index, user must set it after every suspend wakeup, cause relative setting will be reset in suspend
	user_set_rf_power(0, 0, 0);
	bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, &user_set_rf_power);

	// ble event call back
	bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, &task_connect);
	bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, &task_terminate);

	///////////////////// Power Management initialization///////////////////
#if (BLE_APP_PM_ENABLE)
	blc_ll_initPowerManagement_module();

#if (PM_DEEPSLEEP_RETENTION_ENABLE)
	blc_pm_setDeepsleepRetentionType(DEEPSLEEP_MODE_RET_SRAM_LOW16K); // default use 16k deep retention
	bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
	blc_pm_setDeepsleepRetentionThreshold(95, 95);

#if (MCU_CORE_TYPE == MCU_CORE_825x)
	blc_pm_setDeepsleepRetentionEarlyWakeupTiming(TEST_CONN_CURRENT_ENABLE ? 240 : 260);
#elif ((MCU_CORE_TYPE == MCU_CORE_827x))
	blc_pm_setDeepsleepRetentionEarlyWakeupTiming(TEST_CONN_CURRENT_ENABLE ? 340 : 350);
#else
#endif
#else
	bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
#endif

	bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_ENTER, &ble_remote_set_sleep_wakeup);
#else
	bls_pm_setSuspendMask(SUSPEND_DISABLE);
#endif

	// #if (UI_KEYBOARD_ENABLE)
	// 	/////////// keyboard gpio wakeup init ////////
	// 	u32 pin[] = KB_DRIVE_PINS;
	// 	for (int i = 0; i < (sizeof(pin) / sizeof(*pin)); i++)
	// 	{
	// 		cpu_set_gpio_wakeup(pin[i], Level_High, 1); // drive pin pad high wakeup deepsleep
	// 	}

	// 	bls_app_registerEventCallback(BLT_EV_FLAG_GPIO_EARLY_WAKEUP, &proc_keyboard);
	// #elif (UI_BUTTON_ENABLE)

	// 	cpu_set_gpio_wakeup(SW1_GPIO, Level_Low, 1); // button pin pad low wakeUp suspend/deepSleep
	// 	cpu_set_gpio_wakeup(SW2_GPIO, Level_Low, 1); // button pin pad low wakeUp suspend/deepSleep

	// 	bls_app_registerEventCallback(BLT_EV_FLAG_GPIO_EARLY_WAKEUP, &proc_button);
	// #endif

	advertise_begin_tick = clock_time();

	{
		i2c_master_test_init();

		LoadParam();

		// storage_init();
		// storage_test_init();
		bls_ota_registerStartCmdCb(app_enter_ota_mode);

		gpio_set_func(GPIO_PC3, AS_GPIO); // PA4 榛樿涓� GPIO 鍔熻兘锛屽彲浠ヤ笉璁剧疆
		gpio_set_input_en(GPIO_PC3, 0);
		gpio_set_output_en(GPIO_PC3, 1);

		app_timer_test_init();

		gpio_set_func(AFE1_PRO_EN_PIN, AS_GPIO); // PA4 榛樿涓� GPIO 鍔熻兘锛屽彲浠ヤ笉璁剧疆
		gpio_set_input_en(AFE1_PRO_EN_PIN, 0);
		gpio_set_output_en(AFE1_PRO_EN_PIN, 1);

		WaitMs(100);
		AFE_Reset();
		AFE_IsReady();
		SH367309_UpdataAfeConfig();
		SH367309_Enable_AFE_Wdt_Cadc_Drivers();
		// ctl
		gpio_set_func(AFE_CTL_PIN, AS_GPIO); // PA4 榛樿涓� GPIO 鍔熻兘锛屽彲浠ヤ笉璁剧疆
		gpio_set_input_en(AFE_CTL_PIN, 0);
		gpio_set_output_en(AFE_CTL_PIN, 1);
		gpio_write(AFE_CTL_PIN, 1);

		{
			// gpio_set_func(RF_EN_PIN, AS_GPIO); // PA4 榛樿涓� GPIO 鍔熻兘锛屽彲浠ヤ笉璁剧疆
			// gpio_set_input_en(RF_EN_PIN, 0);
			// gpio_set_output_en(RF_EN_PIN, 1);
			// gpio_write(RF_EN_PIN, 1);

			gpio_set_func(SW_PIN, AS_GPIO); // PA4 榛樿涓� GPIO 鍔熻兘锛屽彲浠ヤ笉璁剧疆
			gpio_set_input_en(SW_PIN, 1);
			gpio_set_output_en(SW_PIN, 0);
			// gpio_write(GPIO_PA1, 1);

			gpio_set_func(MCC_C_PIN, AS_GPIO); // PA4 榛樿涓� GPIO 鍔熻兘锛屽彲浠ヤ笉璁剧疆
			gpio_set_input_en(MCC_C_PIN, 0);
			gpio_set_output_en(MCC_C_PIN, 1);
			gpio_write(MCC_C_PIN, 0);

			gpio_set_func(MCC_C_PIN, AS_GPIO); // PA4 榛樿涓� GPIO 鍔熻兘锛屽彲浠ヤ笉璁剧疆
			gpio_set_input_en(MCC_C_PIN, 0);
			gpio_set_output_en(MCC_C_PIN, 1);
			gpio_write(MCC_C_PIN, 0);

			gpio_set_func(CHG_IN_PIN, AS_GPIO); // PA4 榛樿涓� GPIO 鍔熻兘锛屽彲浠ヤ笉璁剧疆
			gpio_setup_up_down_resistor(CHG_IN_PIN, PM_PIN_PULLUP_10K);
			gpio_set_input_en(CHG_IN_PIN, 1);
			gpio_set_output_en(CHG_IN_PIN, 0);
			// gpio_write(MCC_C_PIN, 0);
			gpio_set_func(CHG_WK_PIN, AS_GPIO); // PA4 榛樿涓� GPIO 鍔熻兘锛屽彲浠ヤ笉璁剧疆
			gpio_set_input_en(CHG_WK_PIN, 1);
			gpio_set_output_en(CHG_WK_PIN, 0);
			// gpio_write(MCC_C_PIN, 0);
			gpio_set_func(ADC_BUSEN_PIN, AS_GPIO); // PA4 榛樿涓� GPIO 鍔熻兘锛屽彲浠ヤ笉璁剧疆
			gpio_set_input_en(ADC_BUSEN_PIN, 0);
			gpio_set_output_en(ADC_BUSEN_PIN, 1);
			gpio_write(ADC_BUSEN_PIN, 1);

			gpio_set_func(ADC_EN_PIN, AS_GPIO); // PA4 榛樿涓� GPIO 鍔熻兘锛屽彲浠ヤ笉璁剧疆
			gpio_set_input_en(ADC_EN_PIN, 0);
			gpio_set_output_en(ADC_EN_PIN, 1);
			gpio_write(ADC_EN_PIN, 1);

			gpio_set_func(OWC_TX_PIN, AS_GPIO); // PA4 榛樿涓� GPIO 鍔熻兘锛屽彲浠ヤ笉璁剧疆
			gpio_set_input_en(OWC_TX_PIN, 0);
			gpio_set_output_en(OWC_TX_PIN, 1);
			gpio_write(OWC_TX_PIN, 0);
		}

		adc_app_ch_cfg_t cfg[ADC_APP_CH_MAX] = {
			{ADC_NTC_PIN, 1},  // CH0: 电压分压
			{ADC_VBUS_PIN, 1}, // CH1: NTC1
			{ADC_NMOS_PIN, 1}, // CH2: NTC2
		};

		adc_app_init(cfg);

		cpu_set_gpio_wakeup(CHG_IN_PIN, Level_Low, 1);
		cpu_set_gpio_wakeup(SW_PIN, Level_Low, 1);
		printf("init\n");
		soc_kv_store_init();
		soc_kv_data_t d = soc_kv_store_get();
		// d.soc = 100;
		soc_param_lib_init(&d);

		extern void app_modbus_uart_init(uint32_t baud);
		// app_modbus_uart_init(9600);
		modbus_uart_init();
	}
}

void open_chg_close_dsg(void)
{
	SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1; // 寮�鍚疌ADC
	SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = 1; // 鍏呯數MOS鐢盇FE纭欢鎺у埗
	SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = 0; // 鍏呯數MOS鐢盇FE纭欢鎺у埗
	MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all);
	gpio_write(MCC_C_PIN, 1);
}
void open_dsg_close_chg(void)
{
	SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1; // 寮�鍚疌ADC
	SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = 0; // 鍏呯數MOS鐢盇FE纭欢鎺у埗
	SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = 1; // 鍏呯數MOS鐢盇FE纭欢鎺у埗
	MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all);
	gpio_write(MCC_C_PIN, 0);
}
void enter_fac_mode(bool on)
{
	if (on)
	{
		SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1; // 寮�鍚疌ADC
		SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = 1; // 鍏呯數MOS鐢盇FE纭欢鎺у埗
		SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = 1; // 鍏呯數MOS鐢盇FE纭欢鎺у埗
		MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all);
		gpio_write(MCC_C_PIN, 1);
	}
	else
	{
		open_dsg_close_chg();
	}
}
void charger_detect_and_keyLogi_200ms(void)
{
	static u8 state = 0;

	switch (state)
	{
	case 0:
		if (!gpio_read(CHG_IN_PIN))
		{
			putchar(0x55);
			state = 1;
			// gpio_write(AFE_CTL_PIN, 0);
			open_chg_close_dsg();
		}
		else
		{
			putchar(0xaa);
		}
		break;
	case 1:
		if (gpio_read(CHG_IN_PIN))
		{
			putchar(0xaa);
			state = 0;
			open_dsg_close_chg();
			// gpio_write(AFE_CTL_PIN, 1);
		}
		else
		{
			putchar(0x55);
		}
		break;

	default:
		break;
	}
}

/**
 * @brief		user initialization when MCU wake_up from deepSleep_retention mode
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void user_init_deepRetn(void)
{
#if 0
#if (PM_DEEPSLEEP_RETENTION_ENABLE)

	blc_ll_initBasicMCU(); // mandatory
	rf_set_power_level_index(MY_RF_POWER_INDEX);

	blc_ll_recoverDeepRetention();

	DBG_CHN0_HIGH; // debug

	irq_enable();

#if (UI_KEYBOARD_ENABLE)
	/////////// keyboard gpio wakeup init ////////
	u32 pin[] = KB_DRIVE_PINS;
	for (int i = 0; i < (sizeof(pin) / sizeof(*pin)); i++)
	{
		cpu_set_gpio_wakeup(pin[i], Level_High, 1); // drive pin pad high wakeup deepsleep
	}
#elif (UI_BUTTON_ENABLE)

	cpu_set_gpio_wakeup(SW1_GPIO, Level_Low, 1); // button pin pad low wakeUp suspend/deepSleep
	cpu_set_gpio_wakeup(SW2_GPIO, Level_Low, 1); // button pin pad low wakeUp suspend/deepSleep
#endif

#endif
#endif
}

// _attribute_data_retention_ u8 notify_data_test[20] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
// _attribute_data_retention_ u8 notify_data_test[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,0x08,0x09,0x0a,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
bool rev_master = false;

u16 Sci_CRC16RTU(u8 *pszBuf, u8 unLength)
{
	u16 CRCC = 0XFFFF;
	u32 CRC_count;

	for (CRC_count = 0; CRC_count < unLength; CRC_count++)
	{
		int i;

		CRCC = CRCC ^ *(pszBuf + CRC_count);

		for (i = 0; i < 8; i++)
		{
			if (CRCC & 1)
			{
				CRCC >>= 1;
				CRCC ^= 0xA001;
			}
			else
			{
				CRCC >>= 1;
			}
		}
	}

	return CRCC;
}

#define MAX_TEST_DATA_LEN 1024

// u8 test_buf[MAX_TEST_DATA_LEN];
u8 test_buf[MAX_TEST_DATA_LEN] = {
	0x01, 0x03, 0x4C,
	0x0C, 0x6D, 0x0C, 0x6C, 0x0C, 0x6C, 0x0C, 0x5C,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
	0x0C, 0x6D, 0x0C, 0x5C, 0x00, 0x01, 0x00, 0x04,
	0x00, 0x11, 0x04, 0xF6,
	0xAD, 0x2E};

void generate_test_data(int len)
{
	for (int i = 0; i < len; i++)
	{
		test_buf[i] = i & 0xFF; // 鏈夎寰嬬殑鏁版嵁锛屾柟渚� checksum
	}
}

#define TELINK_NOTIFY_PAYLOAD 20 // MTU=23 鏃� payload = 20

ble_sts_t notify_big_packet(u16 conn, u16 handle, u8 *data, u16 len)
{
	u16 offset = 0;

	while (offset < len)
	{
		u8 chunk = (len - offset) > TELINK_NOTIFY_PAYLOAD ? TELINK_NOTIFY_PAYLOAD : (len - offset);

		ble_sts_t ret = blc_gatt_pushHandleValueNotify(
			conn,
			handle,
			data + offset,
			chunk);

		if (ret != BLE_SUCCESS)
		{
			printf("Send FAIL offset=%d ret=%x\r\n", offset, ret);
			return ret;
		}

		offset += chunk;

		// Telink 鐗规�э細闇�瑕佺粰瀵圭涓�鐐规椂闂达紝鍚﹀垯 notify 浼氳鍚�
		// sleep_us(800);   // 0.8ms瓒冲瀹夊叏
	}

	return BLE_SUCCESS;
}

static int soc = 0; // 褰撳墠 SOC
int get_soc(void)
{
	return soc;
}
int simulate_soc(void)
{
	static int dir = 1; // 1: 澧炲姞, -1: 鍑忓皯
#if 0
    {
        soc += dir;

        // 鍒拌揪涓婇檺锛屽弽鍚�
        if (soc >= 100)
        {
            soc = 100;
            dir = -1;
        }
        // 鍒拌揪涓嬮檺锛屽弽鍚�
        else if (soc <= 0)
        {
            soc = 0;
            dir = 1;
        }
    }
#endif
	++soc;
	return soc;
}
const u16 protect_para[65] = {
	3400,
	3500,
	3600,
	3500,
	100,
	3000,
	3000,
	3000,
	3100,
	100,
	12000,
	12000,
	12000,
	11000,
	100,
	9000,
	9000,
	9000,
	10000,
	100,
	800,
	800,
	800,
	100,
	10,
	800,
	800,
	800,
	100,
	10,
	1000,
	1000,
	1000,
	960,
	100,
	400,
	400,
	400,
	450,
	200,
	1000,
	1000,
	1000,
	960,
	100,
	400,
	400,
	400,
	450,
	500,
	1000,
	1000,
	1000,
	960,
	100,
	100,
	200,
	300,
	100,
	100,
	20,
	10,
	5,
	6,
	100,
};

void notify_other_status(void)
{
	// printf("notify_other_status");
	int len = 3 + 12 * 2 + 2;
	test_buf[0] = 0x01;
	test_buf[1] = 0x03;
	test_buf[2] = 12 * 2;
	u16 other_status[12] = {0};
	extern volatile union System_Status SystemStatus;
	other_status[0] = (UINT16)(SystemStatus.all & 0x0000FFFF);
	other_status[1] = (UINT16)(SystemStatus.all >> 16);
	// other_status[2] = (UINT16)(System_OnOFF_Func.all & 0x0000FFFF);
	// other_status[3] = (UINT16)(System_OnOFF_Func.all >> 16);
	size_t i;
	for (i = 2; i < 12; i++)
		other_status[i] = 1;
	for (i = 0; i < 12; i++)
	{
		test_buf[3 + i * 2] = other_status[i] >> 8;
		test_buf[4 + i * 2] = other_status[i] & 0xff;
	}

	i++;
	u16 crc = Sci_CRC16RTU(test_buf, len - 2);
	// u16 crc = 0x3456;
	test_buf[3 + i * 2] = crc & 0xff;
	test_buf[4 + i * 2] = crc >> 8;

	ble_sts_t r = notify_big_packet(
		BLS_CONN_HANDLE,
		SPP_CLIENT_TO_SERVER_DP_H, // 浣犵殑 notify 鍙ユ焺
		test_buf,
		len);
}
extern UINT8 FaultPoint_First2;
extern UINT8 FaultPoint_Second2;
extern UINT8 FaultPoint_Third2;
extern UINT16 Fault_record_First2[Record_len];
extern UINT16 Fault_record_Second2[Record_len];
extern UINT16 Fault_record_Third2[Record_len];
void notify_protect_status(void)
{
	INT8 k;
	UINT8 a[4];
	UINT16 i = 0, j;
	UINT16 u16SciTemp;
	// printf(" notify_protect_status");
	int len = 3 + 21 * 2 + 2;
	test_buf[0] = 0x01;
	test_buf[1] = 0x03;
	test_buf[2] = 21 * 2;

	u16 protect_status[21] = {0};
	protect_status[0] = 0;
	protect_status[1] = 0;
	protect_status[2] = 0;
	for (j = 0; j < 4; j++)
	{
		k = FaultPoint_First2 - 1 - j;
		if (k < 0)
		{
			k = Record_len + k;
		}
		a[j] = k;
	}
	protect_status[3] = (Fault_record_First2[a[0]] << 8) | Fault_record_First2[a[1]];
	protect_status[4] = (Fault_record_First2[a[2]] << 8) | Fault_record_First2[a[3]];
	for (j = 0; j < 4; j++)
	{
		k = FaultPoint_Second2 - 1 - j;
		if (k < 0)
		{
			k = Record_len + k;
		}
		a[j] = k;
	}
	protect_status[5] = (Fault_record_Second2[a[0]] << 8) | Fault_record_Second2[a[1]];
	protect_status[6] = (Fault_record_Second2[a[2]] << 8) | Fault_record_Second2[a[3]];
	for (j = 0; j < 4; j++)
	{
		k = FaultPoint_Third2 - 1 - j;
		if (k < 0)
		{
			k = Record_len + k;
		}
		a[j] = k;
	}
	protect_status[7] = (Fault_record_Third2[a[0]] << 8) | Fault_record_Third2[a[1]];
	protect_status[8] = (Fault_record_Third2[a[2]] << 8) | Fault_record_Third2[a[3]];
	i = 8;
	System_ErrFlag.u8ErrFlag_Com_AFE1 = 2;
	System_ErrFlag.u8ErrFlag_CBC_DSG = 1;
	protect_status[18] = 0xffff;
	protect_status[19] = 0xffff;
	protect_status[20] = 0xffff;
	// for (j = 0; j < 12; j++)
	// { // 0xD002�����
	// 	u16SciTemp = ((*(&System_ErrFlag.u8ErrFlag_Com_AFE1 + 2 * j)) << 8) | (*(&System_ErrFlag.u8ErrFlag_Com_AFE1 + 2 * j + 1));
	// 	protect_status[i++] = (u16SciTemp >> 8) & 0x00FF;
	// 	protect_status[i++] = u16SciTemp & 0x00FF;
	// }

	for (i = 0; i < 21; i++)
	{
		test_buf[3 + i * 2] = protect_status[i] >> 8;
		test_buf[4 + i * 2] = protect_status[i] & 0xff;
	}

	i++;
	u16 crc = Sci_CRC16RTU(test_buf, len - 2);
	// u16 crc = 0x4567;
	test_buf[3 + i * 2] = crc & 0xff;
	test_buf[4 + i * 2] = crc >> 8;

	ble_sts_t r = notify_big_packet(
		BLS_CONN_HANDLE,
		SPP_CLIENT_TO_SERVER_DP_H, // 浣犵殑 notify 鍙ユ焺
		test_buf,
		len);
}

void notify_soc(void)
{
	// printf("notify_soc");
	int len = 3 + 25 * 2 + 2;
	test_buf[0] = 0x01;
	test_buf[1] = 0x03;
	test_buf[2] = 25 * 2;

	size_t i;
	for (i = 0; i < 25; i++)
	{
		uint16_t u16SciTemp = *(&g_stCellInfoReport.u16Temperature[0] + i);
		test_buf[3 + i * 2] = u16SciTemp >> 8;
		test_buf[4 + i * 2] = u16SciTemp & 0xff;
	}

	i++;
	u16 crc = Sci_CRC16RTU(test_buf, len - 2);
	// u16 crc = 0x2345;
	test_buf[3 + i * 2] = crc & 0xff;
	test_buf[4 + i * 2] = crc >> 8;

	ble_sts_t r = notify_big_packet(
		BLS_CONN_HANDLE,
		SPP_CLIENT_TO_SERVER_DP_H, // 浣犵殑 notify 鍙ユ焺
		test_buf,
		len);
}

void notify_protect_prarm(void)
{
	// printf("notify_protect_prarm");
	int len = 3 + 65 * 2 + 2;
	test_buf[0] = 0x01;
	test_buf[1] = 0x03;
	test_buf[2] = 65 * 2;
	// u16 *p = &g_tParam.protect;
	u16 *p = &g_tParam.protect;

	size_t i;
	for (i = 0; i < 65; i++)
	{
		// test_buf[3 + i * 2] =  protect_para[i] >> 8;
		// test_buf[4 + i * 2] =  protect_para[i] & 0xff;
		test_buf[3 + i * 2] = *p >> 8;
		test_buf[4 + i * 2] = *p & 0xff;
		p++;
	}

	i++;
	u16 crc = Sci_CRC16RTU(test_buf, len - 2);
	test_buf[3 + i * 2] = crc & 0xff;
	test_buf[4 + i * 2] = crc >> 8;

	ble_sts_t r = notify_big_packet(
		BLS_CONN_HANDLE,
		SPP_CLIENT_TO_SERVER_DP_H, // 浣犵殑 notify 鍙ユ焺
		test_buf,
		len);
}

void notify_votage(void)
{
	int len = 3 + 38 * 2 + 2; // 浣犳兂娴嬪灏戝氨濉灏�
	// printf("notify voltage");

	static u8 vol_cnt = 0;
	vol_cnt++;
	{
		test_buf[0] = 0x01;
		test_buf[1] = 0x03;
		test_buf[2] = 38 * 2;

		for (size_t i = 0; i < 39; i++)
		{
			test_buf[3 + i * 2] = 61001 >> 8;
			test_buf[4 + i * 2] = 61001 & 0xff;

			if (i <= 13)
			{
				int temp = g_stCellInfoReport.u16VCell[i];
				test_buf[3 + i * 2] = temp >> 8;
				test_buf[4 + i * 2] = temp & 0xff;
			}

			if (i == 32)
			{
				test_buf[3 + i * 2] = (g_stCellInfoReport.u16VCellMax) >> 8;
				test_buf[4 + i * 2] = (g_stCellInfoReport.u16VCellMax) & 0xff;
			}
			else if (i == 33)
			{
				test_buf[3 + i * 2] = (g_stCellInfoReport.u16VCellMin) >> 8;
				test_buf[4 + i * 2] = (g_stCellInfoReport.u16VCellMin) & 0xff;
			}
			else if (i == 34)
			{
				test_buf[3 + i * 2] = g_stCellInfoReport.u16VCellMaxPosition >> 8;
				test_buf[4 + i * 2] = g_stCellInfoReport.u16VCellMaxPosition & 0xff;
			}
			else if (i == 35)
			{
				test_buf[3 + i * 2] = g_stCellInfoReport.u16VCellMinPosition >> 8;
				test_buf[4 + i * 2] = g_stCellInfoReport.u16VCellMinPosition & 0xff;
			}
			else if (i == 36)
			{
				test_buf[3 + i * 2] = (g_stCellInfoReport.u16VCellDelta) >> 8;
				test_buf[4 + i * 2] = (g_stCellInfoReport.u16VCellDelta) & 0xff;
			}
			else if (i == 37)
			{
				test_buf[3 + i * 2] = (g_stCellInfoReport.u16VCellTotle) >> 8;
				test_buf[4 + i * 2] = (g_stCellInfoReport.u16VCellTotle) & 0xff;
			}
			else if (i == 38)
			{
				u16 crc = Sci_CRC16RTU(test_buf, len - 2);
				// u16 crc = 0x1234;
				test_buf[3 + i * 2] = crc & 0xff;
				test_buf[4 + i * 2] = crc >> 8;
			}
		}
	}

	ble_sts_t r = notify_big_packet(
		BLS_CONN_HANDLE,
		SPP_CLIENT_TO_SERVER_DP_H, // 浣犵殑 notify 鍙ユ焺
		test_buf,
		len);
}

// todo
/*
1.休眠
2.flash
*/
/**
 * @brief     BLE main loop
 * @param[in]  none.
 * @return     none.
 */
void main_loop(void)
{
	////////////////////////////////////// BLE entry /////////////////////////////////
	blt_sdk_main_loop();

////////////////////////////////////// UI entry /////////////////////////////////
#if (UI_KEYBOARD_ENABLE)
	proc_keyboard(0, 0, 0);
#elif (UI_BUTTON_ENABLE)
	// process button 1 second later after power on, to avoid power unstable
	if (!button_detect_en && clock_time_exceed(0, 1000000))
	{
		button_detect_en = 1;
	}
	if (button_detect_en && clock_time_exceed(button_detect_tick, 5000))
	{
		button_detect_tick = clock_time();
		proc_button(0, 0, 0); // button triggers pair & unpair  and OTA
	}
#endif
	_attribute_data_retention_ static u32 update_bms_info_tick = 0;
	if (clock_time_exceed(update_bms_info_tick, 1000 * 200))
	{
		update_bms_info_tick = clock_time();
		App_AFEGet();
		// todo 1s鎿﹀啓涓�娆lash锛屽苟notify
		void update_my_batVal(void);
		// update_my_batVal();
		simulate_soc();
		extern u32 rev_cnt;
		APP_SOC_IntEnhance_Ctrl();

		adc_app_process_200ms();
		uint16_t v0 = adc_app_get_mv(ADC_APP_CH0);
		uint16_t v1 = adc_app_get_mv(ADC_APP_CH1);
		uint16_t v2 = adc_app_get_mv(ADC_APP_CH2);
		uint32_t adc_ntc1_mv = v0; // NTC1
		uint32_t adc_ntc2_mv = v2; // NTC2

		// int16_t temp1_01c = ntc_adc_to_temp_01c(adc_ntc1_mv);
		// int16_t temp2_01c = ntc_adc_to_temp_01c(adc_ntc2_mv);

		// uint32_t adc_ntc1_mv = adc_sample_and_get_result();
		// uint32_t adc_ntc2_mv = adc_sample_and_get_result();

		int16_t temp1 = ntc_adc_mv_to_temp_01c(adc_ntc1_mv);
		int16_t temp2 = ntc_adc_mv_to_temp_01c(adc_ntc2_mv);

		printf("adc %d %d %d\n", v0, v1, v2);
		printf("temp1 %d temp2 %d", temp1, temp2);
		charger_detect_and_keyLogi_200ms();

#if 0
		if(sleep_en)
		{
			//todo 确认afe通信正常机制？？？
extern void AFE_Sleep(void);
			AFE_Sleep();
		}
#endif
	}
	// storage_poll();        // 闈為樆濉炶疆璇紙榛樿涓嶅仛闀挎摝闄わ級
	// storage_test_step();   // 娴嬭瘯鍐欏叆锛堥獙璇� KV/LOG 绋冲畾鎬э級
	{
		// if(device_in_connection_state && clock_time_exceed(interval_update_tick, 1000*1000))
		if (device_in_connection_state && rev_master)
		{
			extern u16 addr;
			rev_master = false;
			if (addr == 0xd000)
				notify_votage();
			else if (addr == 0x2100)
				notify_protect_prarm();
			else if (addr == 0xd026)
				notify_soc();
			else if (addr == 0xd115)
				notify_other_status();
			else if (addr == 0xd100)
				notify_protect_status();
#if 0
			u8 remain = sizeof(notify_data_test);
			// u8 remain = 30;
			u8 *p = notify_data_test;
			rev_master = false;
			ble_sts_t ret;
			interval_update_tick = clock_time();
			// ret = blc_gatt_pushHandleValueNotify(BLS_CONN_HANDLE, SPP_SERVER_TO_CLIENT_DP_H, notify_data_test, 8);
			// ret = blc_gatt_pushHandleValueNotify(BLS_CONN_HANDLE, SPP_CLIENT_TO_SERVER_DP_H, notify_data_test, sizeof(notify_data_test));
#endif
		}
	}
	extern void main_loop_modbus(void);
	main_loop_modbus();
	soc_kv_store_update_and_log_if_changed(SOC_Calculate_Element.u8SOC_Now, SOC_Calculate_Element.u8DSG_SOC_Int, SOC_Calculate_Element.u32Cycle_times);

	blt_pm_proc();
////////////////////////////////////// PM Process /////////////////////////////////
#if (UI_KEYBOARD_ENABLE)
	blt_pm_proc();
#elif (UI_BUTTON_ENABLE)
	if (button_not_released)
	{
		bls_pm_setSuspendMask(SUSPEND_DISABLE);
	}
	else
	{
		bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
	}
#endif
}
