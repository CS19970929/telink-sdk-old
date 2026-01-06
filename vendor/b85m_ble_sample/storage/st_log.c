#include "st_log.h"
#include "st_cfg.h"
#include "st_flash_port.h"
// #include "st_crc16.h"
// #include <string.h>

#define ST_LOG_SECTOR_MAGIC  0x4C4F4731u  // 'LOG1'
#define ST_LOG_REC_MAGIC     0x4C523031u  // 'LR01'
#define ST_LOG_STATE_VALID   0xA5u

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 start_seq;
    u32 rsv;
    u16 crc;
    u16 pad;
} log_sec_hdr_t;

typedef struct __attribute__((packed)) {
    u8  state;
    u8  rsv0;
    u16 type;
    u16 len;
    u16 rsv1;
    u32 seq;
    u32 ts_ms;
    u32 magic;
} log_rec_hdr_t;

static u32 s_log_sec = 0;
static u32 s_log_write_off = 0;
static u32 s_log_next_seq = 1;

static int s_log_erase_pending = 0;
static u32 s_log_erase_addr = 0;

static u32 log_sec_addr(u32 sec_idx) { return ST_LOG_BASE_ADDR + sec_idx * ST_FLASH_SECTOR_SIZE; }

static u16 crc16_log_hdr_payload(const log_rec_hdr_t *hdr, const u8 *payload)
{
    u16 crc = st_crc16_modbus(((const u8*)hdr) + 1, sizeof(log_rec_hdr_t) - 1);
    crc ^= st_crc16_modbus(payload, hdr->len);
    return crc;
}

static u16 crc16_log_sec_hdr(const log_sec_hdr_t *sh)
{
    return st_crc16_modbus((const u8*)sh, 12);
}

static int log_sector_valid(u32 addr, log_sec_hdr_t *out)
{
    log_sec_hdr_t sh;
    st_flash_read(addr, sizeof(sh), (u8*)&sh);
    if (sh.magic != ST_LOG_SECTOR_MAGIC) return 0;
    if (sh.crc != crc16_log_sec_hdr(&sh)) return 0;
    if (out) *out = sh;
    return 1;
}

static int log_sector_format(u32 addr, u32 start_seq)
{
    if (!st_flash_is_voltage_safe()) return 0;
    st_flash_erase_sector(addr);

    log_sec_hdr_t sh;
    memset(&sh, 0xFF, sizeof(sh));
    sh.magic = ST_LOG_SECTOR_MAGIC;
    sh.start_seq = start_seq;
    sh.rsv = 0;
    sh.crc = crc16_log_sec_hdr(&sh);
    st_flash_write(addr, sizeof(sh), (u8*)&sh);
    return 1;
}

static void log_scan(void)
{
    // 找到最后一个有效 sector 和写指针
    // 策略：扫描所有 sector，找到 start_seq 最大且有效的作为最新 sector
    log_sec_hdr_t best;
    u32 best_idx = 0;
    int found = 0;

    for (u32 i = 0; i < ST_LOG_SECTOR_NUM; i++)
    {
        log_sec_hdr_t sh;
        if (log_sector_valid(log_sec_addr(i), &sh))
        {
            if (!found || sh.start_seq >= best.start_seq) {
                best = sh;
                best_idx = i;
                found = 1;
            }
        }
    }

    if (!found)
    {
        // 首次：格式化 sector0
        log_sector_format(log_sec_addr(0), 1);
        s_log_sec = 0;
        s_log_write_off = sizeof(log_sec_hdr_t);
        s_log_next_seq = 1;
        return;
    }

    s_log_sec = best_idx;
    s_log_write_off = sizeof(log_sec_hdr_t);
    s_log_next_seq = best.start_seq;

    // 扫当前 sector 记录，定位 write_off 与 next_seq
    u32 base = log_sec_addr(s_log_sec);
    u32 off = sizeof(log_sec_hdr_t);

    while (off + sizeof(log_rec_hdr_t) + 2 < ST_FLASH_SECTOR_SIZE)
    {
        log_rec_hdr_t hdr;
        st_flash_read(base + off, sizeof(hdr), (u8*)&hdr);

        if (hdr.state == 0xFF && ((u8*)&hdr)[1] == 0xFF) break;

        if (hdr.state == ST_LOG_STATE_VALID &&
            hdr.magic == ST_LOG_REC_MAGIC &&
            hdr.len <= ST_LOG_MAX_PAYLOAD &&
            (off + sizeof(hdr) + hdr.len + 2) <= ST_FLASH_SECTOR_SIZE)
        {
            if (hdr.seq >= s_log_next_seq) s_log_next_seq = hdr.seq + 1;

            off += sizeof(hdr) + hdr.len + 2;
            off = (off + 3) & ~3u;
        }
        else
        {
            break;
        }
    }

    s_log_write_off = off;
}

static int log_rotate_sector(void)
{
    // 切到下一个 sector（环形）
    u32 next = (s_log_sec + 1) % ST_LOG_SECTOR_NUM;
    u32 addr = log_sec_addr(next);

#if (ST_ERASE_IMMEDIATE)
    if (!log_sector_format(addr, s_log_next_seq)) return 0;
#else
    // 延迟擦除：上层先擦再 format
    s_log_erase_pending = 1;
    s_log_erase_addr = addr;
    return 2; // NEED_ERASE
#endif

    s_log_sec = next;
    s_log_write_off = sizeof(log_sec_hdr_t);
    return 1;
}

int st_log_init(void)
{
    log_scan();
    return 1;
}

st_log_ret_t st_log_append(u16 type, u32 ts_ms, const void *payload, u16 len)
{
    if (len > ST_LOG_MAX_PAYLOAD) return ST_LOG_ERR;

    u32 need = sizeof(log_rec_hdr_t) + len + 2;
    need = (need + 3) & ~3u;

    if (ST_FLASH_SECTOR_SIZE - s_log_write_off < (need + ST_LOG_MIN_REMAIN))
    {
        int rr = log_rotate_sector();
        if (rr == 2) return ST_LOG_NO_SPACE; // 需要先擦
        if (!rr) return ST_LOG_ERR;
    }

    u32 base = log_sec_addr(s_log_sec);
    u32 off  = s_log_write_off;

    log_rec_hdr_t hdr;
    memset(&hdr, 0xFF, sizeof(hdr));
    hdr.state = 0xFF;
    hdr.type  = type;
    hdr.len   = len;
    hdr.seq   = s_log_next_seq++;
    hdr.ts_ms = ts_ms;
    hdr.magic = ST_LOG_REC_MAGIC;

    // header
    st_flash_write(base + off, sizeof(hdr), (u8*)&hdr);

    // payload
    st_flash_write(base + off + sizeof(hdr), len, (u8*)payload);

    // crc
    u16 crc = crc16_log_hdr_payload(&hdr, (const u8*)payload);
    st_flash_write(base + off + sizeof(hdr) + len, 2, (u8*)&crc);

    // commit
    u8 st = ST_LOG_STATE_VALID;
    st_flash_write(base + off, 1, &st);

    s_log_write_off = off + need;
    return ST_LOG_OK;
}

st_log_ret_t st_log_read(u32 seq, st_log_item_info_t *info, void *out, u16 *inout_len)
{
    // 线扫全区（后续你需要性能再加索引）
    for (u32 si = 0; si < ST_LOG_SECTOR_NUM; si++)
    {
        u32 base = log_sec_addr(si);

        log_sec_hdr_t sh;
        if (!log_sector_valid(base, &sh)) continue;

        u32 off = sizeof(log_sec_hdr_t);
        while (off + sizeof(log_rec_hdr_t) + 2 < ST_FLASH_SECTOR_SIZE)
        {
            log_rec_hdr_t hdr;
            st_flash_read(base + off, sizeof(hdr), (u8*)&hdr);

            if (hdr.state == 0xFF && ((u8*)&hdr)[1] == 0xFF) break;

            if (hdr.state == ST_LOG_STATE_VALID &&
                hdr.magic == ST_LOG_REC_MAGIC &&
                hdr.len <= ST_LOG_MAX_PAYLOAD &&
                (off + sizeof(hdr) + hdr.len + 2) <= ST_FLASH_SECTOR_SIZE)
            {
                if (hdr.seq == seq)
                {
                    if (*inout_len < hdr.len) {
                        *inout_len = hdr.len;
                        return ST_LOG_ERR;
                    }

                    u8 buf[ST_LOG_MAX_PAYLOAD];
                    st_flash_read(base + off + sizeof(hdr), hdr.len, buf);

                    u16 crc_stored;
                    st_flash_read(base + off + sizeof(hdr) + hdr.len, 2, (u8*)&crc_stored);

                    u16 crc_calc = crc16_log_hdr_payload(&hdr, buf);
                    if (crc_stored != crc_calc) return ST_LOG_ERR;

                    if (out) memcpy(out, buf, hdr.len);
                    if (info) {
                        info->seq = hdr.seq;
                        info->ts_ms = hdr.ts_ms;
                        info->type = hdr.type;
                        info->len = hdr.len;
                    }
                    *inout_len = hdr.len;
                    return ST_LOG_OK;
                }

                off += sizeof(hdr) + hdr.len + 2;
                off = (off + 3) & ~3u;
                continue;
            }
            break;
        }
    }

    return ST_LOG_ERR;
}

void st_log_poll(void)
{
#if (ST_ERASE_IMMEDIATE==0)
    // 默认不擦，避免卡 BLE；你可以在“安全窗口”手动处理 s_log_erase_pending
#endif
}
