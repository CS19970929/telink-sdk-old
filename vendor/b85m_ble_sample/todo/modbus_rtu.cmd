#include "modbus_rtu.h"
// #include <string.h>
#include "app_config.h"
#include "tl_common.h"
#include "drivers.h"

// 浣犺鎻愪緵杩欎袱涓嚱鏁帮紙鍦� app_modbus_uart.c 閲屼細瀹炵幇锛夛細
extern uint16_t uart_dma_rx_len(void);
extern uint8_t* uart_dma_rx_payload_ptr(void);
extern void uart_dma_rx_reset(void);
extern void uart_dma_send_bytes(const uint8_t *data, uint16_t len);

static uint8_t  s_slave_id = MODBUS_SLAVE_ID;
static uint32_t s_last_rx_ms = 0;
static uint16_t s_last_rx_len = 0;

static uint8_t  s_tx[MODBUS_TX_MAX];

// #include "modbus_rtu.h"

static uint16_t g_holding[256];
static uint16_t g_input[256];

int modbus_reg_read_holding(uint16_t addr, uint16_t qty, uint16_t *out)
{
    if((uint32_t)addr + qty > 256) return -1;
    for(uint16_t i=0;i<qty;i++) out[i] = g_holding[addr + i];
    return 0;
}

int modbus_reg_read_input(uint16_t addr, uint16_t qty, uint16_t *out)
{
    if((uint32_t)addr + qty > 256) return -1;
    for(uint16_t i=0;i<qty;i++) out[i] = g_input[addr + i];
    return 0;
}

int modbus_reg_write_single(uint16_t addr, uint16_t value)
{
    if(addr >= 256) return -1;
    g_holding[addr] = value;
    return 0;
}

int modbus_reg_write_multi(uint16_t addr, uint16_t qty, const uint16_t *in)
{
    if((uint32_t)addr + qty > 256) return -1;
    for(uint16_t i=0;i<qty;i++) g_holding[addr + i] = in[i];
    return 0;
}

static uint16_t mb_crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for(uint16_t i=0;i<len;i++){
        crc ^= buf[i];
        for(int j=0;j<8;j++){
            if(crc & 1) crc = (crc >> 1) ^ 0xA001;
            else        crc = (crc >> 1);
        }
    }
    return crc;
}

static void mb_put_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static uint16_t mb_get_u16_be(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void mb_send_exception(uint8_t func, uint8_t ex_code)
{
    // [id][func|0x80][ex][crc_lo][crc_hi]
    s_tx[0] = s_slave_id;
    s_tx[1] = func | 0x80;
    s_tx[2] = ex_code;
    uint16_t crc = mb_crc16(s_tx, 3);
    s_tx[3] = (uint8_t)(crc & 0xFF);
    s_tx[4] = (uint8_t)(crc >> 8);
    uart_dma_send_bytes(s_tx, 5);
}

static void mb_handle_frame(const uint8_t *rx, uint16_t len)
{
    if(len < 4) return;

    uint8_t id   = rx[0];
    uint8_t func = rx[1];

    if(id != s_slave_id && id != 0x00){
        // 涓嶆槸鏈満锛屼篃涓嶆槸骞挎挱
        return;
    }

    // CRC 鏍￠獙
    uint16_t crc_calc = mb_crc16(rx, (uint16_t)(len - 2));
    uint16_t crc_rx   = (uint16_t)(rx[len-2] | (rx[len-1] << 8));
    if(crc_calc != crc_rx){
        // CRC閿欙細鐩存帴涓�
        return;
    }

    // 骞挎挱甯э細鍙墽琛屼笉搴旂瓟
    uint8_t is_broadcast = (id == 0x00);

    // ====== 鍔熻兘鐮佸鐞� ======
    if(func == 0x03 || func == 0x04){
        // 璇讳繚鎸�/杈撳叆瀵勫瓨鍣�
        if(len != 8){
            if(!is_broadcast) mb_send_exception(func, 0x03); // Illegal data value
            return;
        }
        uint16_t addr = mb_get_u16_be(&rx[2]);
        uint16_t qty  = mb_get_u16_be(&rx[4]);
        if(qty == 0 || qty > 125){
            if(!is_broadcast) mb_send_exception(func, 0x03);
            return;
        }

        uint16_t regs[125];
        int rc = 0;
        if(func == 0x03) rc = modbus_reg_read_holding(addr, qty, regs);
        else             rc = modbus_reg_read_input  (addr, qty, regs);

        if(rc != 0){
            if(!is_broadcast) mb_send_exception(func, 0x02); // Illegal data address(甯哥敤)
            return;
        }

        if(is_broadcast) return;

        // 鞚戨嫷锛歔id][func][bytecnt][data...][crc]
        uint16_t bytecnt = (uint16_t)(qty * 2);
        uint16_t out_len = (uint16_t)(3 + bytecnt);
        if(out_len + 2 > MODBUS_TX_MAX){
            mb_send_exception(func, 0x04); // Slave device failure
            return;
        }

        s_tx[0] = s_slave_id;
        s_tx[1] = func;
        s_tx[2] = (uint8_t)bytecnt;

        for(uint16_t i=0;i<qty;i++){
            mb_put_u16_be(&s_tx[3 + 2*i], regs[i]);
        }

        uint16_t crc = mb_crc16(s_tx, out_len);
        s_tx[out_len + 0] = (uint8_t)(crc & 0xFF);
        s_tx[out_len + 1] = (uint8_t)(crc >> 8);

        uart_dma_send_bytes(s_tx, (uint16_t)(out_len + 2));
        return;
    }
    else if(func == 0x06){
        // 鍐欏崟瀵勫瓨鍣細len=8
        if(len != 8){
            if(!is_broadcast) mb_send_exception(func, 0x03);
            return;
        }
        uint16_t addr = mb_get_u16_be(&rx[2]);
        uint16_t val  = mb_get_u16_be(&rx[4]);

        int rc = modbus_reg_write_single(addr, val);
        if(rc != 0){
            if(!is_broadcast) mb_send_exception(func, 0x02);
            return;
        }
        if(is_broadcast) return;

        // 鍥炴樉璇锋眰锛堟爣鍑嗭級
        memcpy(s_tx, rx, 6);
        uint16_t crc = mb_crc16(s_tx, 6);
        s_tx[6] = (uint8_t)(crc & 0xFF);
        s_tx[7] = (uint8_t)(crc >> 8);
        uart_dma_send_bytes(s_tx, 8);
        return;
    }
    else if(func == 0x10){
        // 鍐欏瀵勫瓨鍣細len >= 9
        // [id][10][addr_hi][addr_lo][qty_hi][qty_lo][bytecnt][data...][crc]
        if(len < 9){
            if(!is_broadcast) mb_send_exception(func, 0x03);
            return;
        }
        uint16_t addr    = mb_get_u16_be(&rx[2]);
        uint16_t qty     = mb_get_u16_be(&rx[4]);
        uint8_t  bytecnt = rx[6];

        if(qty == 0 || qty > 123) { // 123*2=246
            if(!is_broadcast) mb_send_exception(func, 0x03);
            return;
        }
        if(bytecnt != (uint8_t)(qty * 2)){
            if(!is_broadcast) mb_send_exception(func, 0x03);
            return;
        }
        if((uint16_t)(7 + bytecnt + 2) != len){
            if(!is_broadcast) mb_send_exception(func, 0x03);
            return;
        }

        uint16_t regs[123];
        for(uint16_t i=0;i<qty;i++){
            regs[i] = mb_get_u16_be(&rx[7 + 2*i]);
        }

        int rc = modbus_reg_write_multi(addr, qty, regs);
        if(rc != 0){
            if(!is_broadcast) mb_send_exception(func, 0x02);
            return;
        }
        if(is_broadcast) return;

        // 鍝嶅簲锛歔id][10][addr][qty][crc]
        s_tx[0] = s_slave_id;
        s_tx[1] = 0x10;
        mb_put_u16_be(&s_tx[2], addr);
        mb_put_u16_be(&s_tx[4], qty);
        uint16_t crc = mb_crc16(s_tx, 6);
        s_tx[6] = (uint8_t)(crc & 0xFF);
        s_tx[7] = (uint8_t)(crc >> 8);
        uart_dma_send_bytes(s_tx, 8);
        return;
    }

    // 涓嶆敮鎸佸姛鑳界爜
    if(!is_broadcast) mb_send_exception(func, 0x01); // Illegal function
}

void modbus_rtu_init(uint8_t slave_id)
{
    s_slave_id = slave_id;
    s_last_rx_ms = 0;
    s_last_rx_len = 0;
}

void modbus_rtu_on_rx_progress(uint32_t now_ms)
{
    s_last_rx_ms = now_ms;
}

// 鏍稿績锛氱敤鈥滈暱搴﹀彉鍖� + 闈欓粯闂撮殧鈥濇潵鍒ゅ抚缁撴潫
void modbus_rtu_poll(uint32_t now_ms)
{
    mb_send_exception(0x06, 0x55);
	u8 send_buf[4] = {0x01, 0x02, 0x03, 0x55};
	uart_dma_send_packet(send_buf, sizeof(send_buf));
    
    uint16_t cur_len = uart_dma_rx_len();
    if(cur_len != s_last_rx_len){
        s_last_rx_len = cur_len;
        s_last_rx_ms  = now_ms;
        return;
    }

    if(cur_len == 0) return;

    if((now_ms - s_last_rx_ms) >= MODBUS_FRAME_GAP_MS){
        // 鍒ゅ畾涓�甯х粨鏉�
        uint16_t frame_len = cur_len;
        if(frame_len > MODBUS_RX_MAX) frame_len = MODBUS_RX_MAX;

        const uint8_t *payload = uart_dma_rx_payload_ptr();

        // 澶勭悊甯у墠锛氬厛 copy锛岄伩鍏� DMA 鍦ㄤ綘瑙ｆ瀽鏃惰鐩�
        uint8_t tmp[MODBUS_RX_MAX];
        memcpy(tmp, payload, frame_len);

        uart_dma_rx_reset();
        s_last_rx_len = 0;

        mb_handle_frame(tmp, frame_len);
    }
}
