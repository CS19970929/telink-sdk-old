#include "log_flash.h"
#include "flash_store_cfg.h"
#include "flash.h"
#include <string.h>

#define LOG_MAGIC  0x4C47 // 'LG'

typedef struct __attribute__((packed)) {
    u16 magic;
    u8  type;
    u8  rsv;
    u16 len;
    u32 tick;     // 你可以放 clock_time()/秒计数
    u16 crc;      // header(with crc=0)+payload
} log_hdr_t;

extern u16 crc16_modbus(const u8 *buf, u32 len);

static u32 g_log_wr = 0;

// 简单队列：只缓存1条（够你先跑起来）
static volatile u8 g_log_pending = 0;
static log_type_t g_log_t;
static u8  g_log_buf[128];
static u16 g_log_len;

static u32 log_begin(void){ return FLASH_ADDR_LOG_BASE; }
static u32 log_end(void){ return FLASH_ADDR_LOG_BASE + FLASH_ADDR_LOG_SECTORS * FLASH_SECTOR_SIZE; }

static void log_find_write_ptr(void)
{
    u32 addr = log_begin();
    log_hdr_t hdr;

    while(addr + sizeof(log_hdr_t) < log_end()){
        flash_read_page(addr, sizeof(hdr), (u8*)&hdr);
        if(((u8*)&hdr)[0] == 0xFF) break;

        if(hdr.magic != LOG_MAGIC || hdr.len > 2000){
            addr += 4;
            continue;
        }
        addr += sizeof(log_hdr_t) + hdr.len;
        addr = (addr + 3) & ~3;
    }
    g_log_wr = addr;
}

static void log_rotate_if_need(u32 need)
{
    if(g_log_wr + need <= log_end()) return;

    // 简单轮转：擦掉整个LOG区域（先跑通）
    for(u32 s=0; s<FLASH_ADDR_LOG_SECTORS; s++){
        flash_erase_sector(log_begin() + s * FLASH_SECTOR_SIZE);
    }
    g_log_wr = log_begin();
}

void log_flash_init(void)
{
    log_find_write_ptr();
}

int log_push(log_type_t t, const u8 *payload, u16 len)
{
    if(len > sizeof(g_log_buf)) return 0;
    if(g_log_pending) return 0; // 简化版：忙就丢或你改成环形RAM队列

    memcpy(g_log_buf, payload, len);
    g_log_len = len;
    g_log_t = t;
    g_log_pending = 1;
    return 1;
}

void log_flash_proc(void)
{
    if(!g_log_pending) return;
    g_log_pending = 0;

    log_hdr_t hdr;
    hdr.magic = LOG_MAGIC;
    hdr.type  = (u8)g_log_t;
    hdr.rsv   = 0;
    hdr.len   = g_log_len;
    hdr.tick  = clock_time(); // 或换成秒
    hdr.crc   = 0;

    u8 tmp[sizeof(log_hdr_t) + sizeof(g_log_buf)];
    memcpy(tmp, &hdr, sizeof(hdr));
    memcpy(tmp + sizeof(hdr), g_log_buf, g_log_len);
    u16 crc = crc16_modbus(tmp, sizeof(hdr) + g_log_len);
    hdr.crc = crc;

    u32 rec_len = sizeof(log_hdr_t) + g_log_len;
    rec_len = (rec_len + 3) & ~3;

    log_rotate_if_need(rec_len);

    flash_write_page(g_log_wr, sizeof(hdr), (u8*)&hdr);
    flash_write_page(g_log_wr + sizeof(hdr), g_log_len, (u8*)g_log_buf);
    g_log_wr += rec_len;
}
