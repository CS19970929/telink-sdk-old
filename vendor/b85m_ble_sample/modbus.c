
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"

#define SYSCLK_HZ     16000000   // 你工程真实系统时钟
#define MODBUS_BAUD   9600

__attribute__((aligned(4))) static unsigned char g_uart_rx_buf[256]; // 建议 16倍数
__attribute__((aligned(4))) static unsigned char g_uart_tx_buf[256];

static inline unsigned int uart_dma_rx_len(unsigned char *buf)
{
    // Telink DMA RX buffer：前4字节放长度（常见约定）
    return (unsigned int)buf[0] | ((unsigned int)buf[1]<<8) | ((unsigned int)buf[2]<<16) | ((unsigned int)buf[3]<<24);
}

#if 0
void app_uart_test_init(void)
{
	WaitMs(2000);  //leave enough time for SWS_reset when power on

	//note: dma addr must be set first before any other uart initialization! (confirmed by sihui)
	uart_recbuff_init( (unsigned char *)&rec_buff, sizeof(rec_buff));

	uart_gpio_set(UART_TX_PB1, UART_RX_PB0);// uart tx/rx pin set

	uart_reset();  //will reset uart digital registers from 0x90 ~ 0x9f, so uart setting must set after this reset

	//baud rate: 115200
	#if (CLOCK_SYS_CLOCK_HZ == 16000000)
//		uart_init(118, 13, PARITY_NONE, STOP_BIT_ONE);
		uart_init(9, 13, PARITY_NONE, STOP_BIT_ONE);
	#elif (CLOCK_SYS_CLOCK_HZ == 24000000)
		uart_init(249, 9, PARITY_NONE, STOP_BIT_ONE);
	#endif


#if (UART_MODE==UART_DMA)
	uart_dma_enable(1, 1); 	//uart data in hardware buffer moved by dma, so we need enable them first

	irq_set_mask(FLD_IRQ_DMA_EN);
	dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1);   	//uart Rx/Tx dma irq enable

	uart_irq_enable(0, 0);  	//uart Rx/Tx irq no need, disable them


#elif(UART_MODE==UART_NDMA)
	uart_dma_enable(0, 0);

	#if(MCU_CORE_TYPE == MCU_CORE_827x)
		irq_disable_type(FLD_IRQ_DMA_EN);
	#elif(MCU_CORE_TYPE == MCU_CORE_825x)
		irq_clr_mask(FLD_IRQ_DMA_EN);
	#endif

	dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 0);

	uart_irq_enable(1,0);   //uart RX irq enable

	uart_ndma_irq_triglevel(1,0);   //set the trig level. 1 indicate one byte will occur interrupt
#endif



	irq_enable();

}
#endif

void modbus_uart_init(void)
{
    uart_reset();

    uart_init_baudrate(MODBUS_BAUD, SYSCLK_HZ, PARITY_EVEN, STOP_BIT_ONE); // 常见 8E1
    uart_dma_enable(1, 1);

    uart_gpio_set(UART_TX_PC2, UART_RX_PC3);

    // RX DMA buffer init
    // 注意：你的 uart_recbuff_init 要按上面的补丁加上 reg_dma_chn_en |= FLD_DMA_CHN_UART_RX
    uart_recbuff_init(g_uart_rx_buf, sizeof(g_uart_rx_buf));
}

#define MODBUS_FRAME_GAP_US  4000   // 9600bps下，3.5字符时间约 ~4ms，取整好用

static unsigned int s_last_len = 0;
static unsigned int s_last_change_tick = 0;

int modbus_poll_frame(unsigned char **pdu, unsigned int *pdu_len)
{
    unsigned int now = clock_time(); // 你的SDK计时函数
    unsigned int len = uart_dma_rx_len(g_uart_rx_buf);

    if(len != s_last_len){
        s_last_len = len;
        s_last_change_tick = now;
        return 0; // 还在收
    }

    // 长度没变，看看是否超过空闲间隔 -> 判定帧结束
    if(len >= 4 + 4 && clock_time_exceed(s_last_change_tick, MODBUS_FRAME_GAP_US)){
        // 有效数据从 buf[4] 开始
        *pdu = &g_uart_rx_buf[4];
        *pdu_len = len;

        return 1; // ✅收到一帧
    }

    return 0;
}

static void uart_dma_rx_reset(void)
{
    g_uart_rx_buf[0]=g_uart_rx_buf[1]=g_uart_rx_buf[2]=g_uart_rx_buf[3]=0;
    s_last_len = 0;
    s_last_change_tick = clock_time();
}

// static void uart_dma_send_packet(const unsigned char *data, unsigned int n)
void uart_dma_send_packet(const unsigned char *data, unsigned int n)
{
    // g_uart_tx_buf[0..3] 存长度
    g_uart_tx_buf[0] = (unsigned char)(n & 0xFF);
    g_uart_tx_buf[1] = (unsigned char)((n >> 8) & 0xFF);
    g_uart_tx_buf[2] = 0;
    g_uart_tx_buf[3] = 0;

    for(unsigned int i=0;i<n;i++){
        g_uart_tx_buf[4+i] = data[i];
    }

    uart_send_dma(g_uart_tx_buf);
}

#if 1
void main_loop_modbus(void)
{
    unsigned char *frame;
    unsigned int  frame_len;

    {
        // 其他任务：AFE采样、BLE、低功耗判断...

        if (modbus_poll_frame(&frame, &frame_len))
        {
            // modbus_on_frame(frame, frame_len);  // 这里做CRC/解析/回包
            // uart_dma_rx_reset();                // 处理完记得清RX长度/重置状态
            uart_dma_send_packet(&frame, frame_len);
        }
    }
}

#endif

// void main_loop_modbus(void)
// {
//     unsigned char *frame;
//     unsigned int  frame_len;

//     {
//         // 其他任务：AFE采样、BLE、低功耗判断...

//         if (modbus_poll_frame(&frame, &frame_len))
//         {
//             modbus_on_frame(frame, frame_len);  // 这里做CRC/解析/回包
//             uart_dma_rx_reset();                // 处理完记得清RX长度/重置状态
//         }
//     }
// }