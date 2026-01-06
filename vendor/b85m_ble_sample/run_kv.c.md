#include "run_kv.h"
#include "flash_store_cfg.h"
#include "flash.h"
#include <string.h>

#define RUN_KV_MAGIC  0x4B56  // 'KV'

typedef struct __attribute__((packed)) {
    u16 magic;
    u16 key;        // 目前只有1个key也行
    u16 len;        // payload len
    u16 ver;
    u32 seq;
    u16 crc;        // header+payload crc16
} kv_hdr_t;

#define KEY_RUN_STATE   0x0001

static u32 g_write_addr = 0;
static u32 g_seq = 0;

static volatile u8  g_pending = 0;
static run_state_t  g_pending_st;

extern u16 crc16_modbus(const u8 *buf, u32 len);

static u32 kv_area_begin(void){ return FLASH_ADDR_RUN_KV_BASE; }
static u32 kv_area_end(void){ return FLASH_ADDR_RUN_KV_BASE + FLASH_ADDR_RUN_KV_SECTORS * FLASH_SECTOR_SIZE; }

static int is_erased_u8(u8 b){ return b == 0xFF; }

static void kv_format_if_empty(void)
{
    // 如果全是0xFF则保持；否则不动。你也可以强制擦除重建。
}

static void kv_scan_find_latest(u32 *latest_addr, kv_hdr_t *latest_hdr, run_state_t *latest_st)
{
    *latest_addr = 0;
    memset(latest_hdr, 0, sizeof(*latest_hdr));

    u32 addr = kv_area_begin();
    kv_hdr_t hdr;
    run_state_t st;

    u32 best_seq = 0;

    while(addr + sizeof(kv_hdr_t) < kv_area_end())
    {
        flash_read_page(addr, sizeof(hdr), (u8*)&hdr);

        if(is_erased_u8(((u8*)&hdr)[0])) { // 0xFF 代表未写
            break;
        }

        if(hdr.magic != RUN_KV_MAGIC || hdr.key != KEY_RUN_STATE || hdr.len != sizeof(run_state_t)) {
            // 不认识的记录，跳过一个最小步长，避免死循环
            addr += 4;
            continue;
        }

        if(addr + sizeof(kv_hdr_t) + hdr.len > kv_area_end()) break;

        flash_read_page(addr + sizeof(kv_hdr_t), sizeof(st), (u8*)&st);

        // 校验CRC
        u8 temp[sizeof(kv_hdr_t) + sizeof(run_state_t)];
        memcpy(temp, &hdr, sizeof(hdr));
        memcpy(temp + sizeof(hdr), &st, sizeof(st));
        // crc字段本身也参与了？我们让hdr.crc先置0再算
        ((kv_hdr_t*)temp)->crc = 0;
        u16 crc = crc16_modbus(temp, sizeof(temp));
        if(crc == hdr.crc && hdr.seq >= best_seq){
            best_seq = hdr.seq;
            *latest_addr = addr;
            *latest_hdr  = hdr;
            *latest_st   = st;
        }

        // 下一条：按记录长度跳
        addr += sizeof(kv_hdr_t) + hdr.len;
        // 4字节对齐
        addr = (addr + 3) & ~3;
    }

    // 找到写入点：从扫描终点开始写
    g_write_addr = addr;
    g_seq = best_seq;
}

static void kv_ensure_space_or_advance(u32 need)
{
    u32 end = kv_area_end();
    if(g_write_addr + need <= end) return;

    // 写满了：轮转到起始，擦除全部扇区（简单版）
    for(u32 s = 0; s < FLASH_ADDR_RUN_KV_SECTORS; s++){
        flash_erase_sector(kv_area_begin() + s * FLASH_SECTOR_SIZE);
    }
    g_write_addr = kv_area_begin();
}

void run_kv_init(void)
{
    kv_format_if_empty();

    kv_hdr_t hdr;
    run_state_t st;
    u32 latest_addr;
    kv_scan_find_latest(&latest_addr, &hdr, &st);
}

int run_kv_load(run_state_t *out)
{
    kv_hdr_t hdr;
    run_state_t st;
    u32 latest_addr = 0;
    kv_scan_find_latest(&latest_addr, &hdr, &st);
    if(latest_addr == 0) return 0;
    *out = st;
    return 1;
}

int run_kv_request_save(const run_state_t *st)
{
    g_pending_st = *st;
    g_pending = 1;
    return 1;
}

void run_kv_proc(void)
{
    if(!g_pending) return;
    g_pending = 0;

    kv_hdr_t hdr;
    hdr.magic = RUN_KV_MAGIC;
    hdr.key   = KEY_RUN_STATE;
    hdr.len   = sizeof(run_state_t);
    hdr.ver   = RUN_STATE_VER;
    hdr.seq   = ++g_seq;
    hdr.crc   = 0;

    // 计算CRC
    u8 temp[sizeof(kv_hdr_t) + sizeof(run_state_t)];
    memcpy(temp, &hdr, sizeof(hdr));
    memcpy(temp + sizeof(hdr), &g_pending_st, sizeof(g_pending_st));
    u16 crc = crc16_modbus(temp, sizeof(temp));
    hdr.crc = crc;

    u32 rec_len = sizeof(kv_hdr_t) + sizeof(run_state_t);
    rec_len = (rec_len + 3) & ~3;

    kv_ensure_space_or_advance(rec_len);

    flash_write_page(g_write_addr, sizeof(hdr), (u8*)&hdr);
    flash_write_page(g_write_addr + sizeof(hdr), sizeof(g_pending_st), (u8*)&g_pending_st);

    // 写入点前移
    g_write_addr += rec_len;
}
