#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ======= 用户配置 =======
#define MODBUS_SLAVE_ID              0x01

// RTU：3.5T 静默间隔（用毫秒近似）
// 115200bps: 1char≈0.087ms，3.5T≈0.3ms；但用ms tick没法这么细，所以用 2~5ms 更稳
// 9600bps: 1char≈1.04ms，3.5T≈3.6ms；用 5ms 合理
#define MODBUS_FRAME_GAP_MS          5

#define MODBUS_RX_MAX                256
#define MODBUS_TX_MAX                256

// ======= 回调：寄存器读写（你把这里对接到你的寄存器表）=======
// 返回 0 表示成功；非0表示异常（将映射为 Modbus exception code）
int modbus_reg_read_holding(uint16_t addr, uint16_t qty, uint16_t *out);
int modbus_reg_read_input  (uint16_t addr, uint16_t qty, uint16_t *out);
int modbus_reg_write_single(uint16_t addr, uint16_t value);
int modbus_reg_write_multi (uint16_t addr, uint16_t qty, const uint16_t *in);

// ======= Modbus RTU 核心接口 =======
void modbus_rtu_init(uint8_t slave_id);

// 你在主循环里周期调用（每 1~10ms 调一次都行）
void modbus_rtu_poll(uint32_t now_ms);

// UART RX 数据投喂接口：
// DMA模式下，你不需要逐字节喂；我们从 DMA 缓冲区读取长度
// 这里暴露一个“收到新数据了”通知（用于更新时间戳）
void modbus_rtu_on_rx_progress(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
