#include "uart.h"
#include "gpio.h"
#include "modbus_rtu.h"
// #include <string.h>

#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"

// ====== DMA RX/TX Buffer ======
// Telink UART DMA 常见：buffer 前 4 字节存长度，后面是 payload
static uint8_t s_uart_rx_dma[4 + MODBUS_RX_MAX] __attribute__((aligned(4)));
static uint8_t s_uart_tx_dma[4 + MODBUS_TX_MAX] __attribute__((aligned(4)));

// 你要替换为你工程里真正的毫秒 tick
extern uint32_t clock_time_ms(void);

// ====== 这几个函数给 modbus_rtu.c 调用 ======
uint16_t uart_dma_rx_len(void)
{
    // 约定：前2字节是长度（不同SDK可能是 4字节len，你按实际改）
    return (uint16_t)(s_uart_rx_dma[0] | (s_uart_rx_dma[1] << 8));
}

uint8_t* uart_dma_rx_payload_ptr(void)
{
    return &s_uart_rx_dma[4];
}

void uart_dma_rx_reset(void)
{
    s_uart_rx_dma[0] = s_uart_rx_dma[1] = s_uart_rx_dma[2] = s_uart_rx_dma[3] = 0;

    // 关键：某些 SDK/芯片需要重新 init rx buffer 或清 timeout 标志
    // 最稳：重新调用 uart_recbuff_init
    uart_recbuff_init(s_uart_rx_dma, sizeof(s_uart_rx_dma));
}

void uart_dma_send_bytes(const uint8_t *data, uint16_t len)
{
    if(len > MODBUS_TX_MAX) len = MODBUS_TX_MAX;

    s_uart_tx_dma[0] = (uint8_t)(len & 0xFF);
    s_uart_tx_dma[1] = (uint8_t)(len >> 8);
    s_uart_tx_dma[2] = 0;
    s_uart_tx_dma[3] = 0;
    memcpy(&s_uart_tx_dma[4], data, len);

    // 等待上一次发送完成
    while(uart_tx_is_busy()){}

    uart_send_dma(s_uart_tx_dma);
}

// ====== UART + Modbus 初始化 ======
void app_modbus_uart_init(uint32_t baud)
{
    // 1) UART 初始化
    // 你按项目系统时钟设置 System_clock（比如 16000000/24000000/32000000...）
    uart_init_baudrate(baud, 16000000, PARITY_EVEN, STOP_BIT_ONE); // Modbus RTU 常用：8E1

    // 2) GPIO 选择：PC2=TX, PC3=RX
    uart_gpio_set(UART_TX_PC2, UART_RX_PC3);

    // 3) DMA使能
    uart_dma_enable(1, 1);

    // 4) RX DMA buffer init
    uart_recbuff_init(s_uart_rx_dma, sizeof(s_uart_rx_dma));
    uart_dma_rx_reset();

    // 5) Modbus RTU init
    modbus_rtu_init(MODBUS_SLAVE_ID);
}

// ====== 主循环里调用 ======
void app_modbus_uart_loop(void)
{
    uint32_t now = clock_time();

    // 你可以每次循环都 poll（内部会判断）
    modbus_rtu_poll(now);
}

