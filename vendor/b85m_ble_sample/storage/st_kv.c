#include "st_kv.h"
#include "st_cfg.h"
#include "st_flash_port.h"
// #include "st_crc16.h"
//  #include <string.h>

#define ST_KV_SECTOR_MAGIC 0x4B564831u // 'KVH1'
#define ST_KV_REC_MAGIC 0x4B565231u    // 'KVR1'
#define ST_KV_STATE_VALID 0xA5u

// sector header
typedef struct __attribute__((packed))
{
    u32 magic;
    u32 gen; // generation counter
    u32 rsv;
    u16 crc; // crc over (magic,gen,rsv)
    u16 pad;
} kv_sec_hdr_t;

// record header
typedef struct __attribute__((packed))
{
    u8 state; // 0xFF -> 0xA5 commit
    u8 rsv0;
    u16 key;
    u16 len;
    u16 rsv1;
    u32 seq;
    u32 magic;
} kv_rec_hdr_t;

static u16 s_keys[ST_KV_MAX_KEYS];
static u16 s_key_num = 0;

static u32 s_active_sec = 0; // 0 or 1
static u32 s_active_gen = 0;
static u32 s_write_off = 0; // offset within active sector
static u32 s_next_seq = 1;

static int s_need_migrate = 0;
static int s_erase_pending = 0;
static u32 s_erase_addr = 0;

static u32 kv_sec_addr(u32 sec_idx) { return ST_KV_BASE_ADDR + sec_idx * ST_FLASH_SECTOR_SIZE; }
static u32 kv_active_addr(void) { return kv_sec_addr(s_active_sec); }
static u32 kv_inactive_addr(void) { return kv_sec_addr(s_active_sec ^ 1u); }

u16 st_crc16_modbus(const u8 *buf, u16 len)
{
    u16 crc = 0xFFFF;
    for (u16 i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for (u8 b = 0; b < 8; b++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

static u16 crc16_hdr_payload(const kv_rec_hdr_t *hdr, const u8 *payload)
{
    // CRC 涓嶅寘鍚� state锛堝洜涓� state 鏈�鍚庡啓锛�
    u16 crc = 0xFFFF;
    const u8 *p = (const u8 *)hdr;

    // 浠� hdr->rsv0 寮�濮嬬畻锛堣烦杩� state锛�
    crc = st_crc16_modbus(p + 1, sizeof(kv_rec_hdr_t) - 1);
    crc = st_crc16_modbus(payload, hdr->len) ^ crc; // 娣峰悎涓�涓嬶紙绠�鍗曞悎骞讹級
    // 涓轰簡涓嶅紩鍏ュ鏉傗�滃閲� CRC鈥濓紝杩欓噷鐢ㄤ竴绉嶅彲閲嶅鏂瑰紡锛�
    // 鏇翠弗璋ㄥ彲鎶婁袱娈垫嫾璧锋潵绠楋紝浣嗚 RAM buffer銆備笅闈㈠仛鎷兼帴绠楋細
    // 杩欓噷淇濇寔绠�鍗曪細鐢ㄤ袱娈电畻鍚庡紓鎴栧悎骞躲�傝冻澶熺敤锛堥殢鏈洪敊璇鐜囦粛寰堜綆锛夈��
    return crc;
}

static u16 crc16_sector_hdr(const kv_sec_hdr_t *sh)
{
    return st_crc16_modbus((const u8 *)sh, 12); // magic+gen+rsv
}

static void kv_read(u32 addr, void *buf, u32 len)
{
    st_flash_read(addr, len, (u8 *)buf);
}

static void kv_write(u32 addr, const void *buf, u32 len)
{
    st_flash_write(addr, len, (const u8 *)buf);
}

static int kv_is_blank_u8(u8 b) { return b == 0xFF; }

static int kv_sector_valid(u32 sec_addr, kv_sec_hdr_t *out)
{
    kv_sec_hdr_t sh;
    kv_read(sec_addr, &sh, sizeof(sh));

    if (sh.magic != ST_KV_SECTOR_MAGIC)
        return 0;
    if (sh.crc != crc16_sector_hdr(&sh))
        return 0;

    if (out)
        *out = sh;
    return 1;
}

static int kv_sector_format(u32 sec_addr, u32 gen)
{
    if (!st_flash_is_voltage_safe())
        return 0;

    st_flash_erase_sector(sec_addr);

    kv_sec_hdr_t sh;
    memset(&sh, 0xFF, sizeof(sh));
    sh.magic = ST_KV_SECTOR_MAGIC;
    sh.gen = gen;
    sh.rsv = 0;
    sh.crc = crc16_sector_hdr(&sh);

    kv_write(sec_addr, &sh, sizeof(sh));
    return 1;
}

// 鎵弿 active sector锛屽畾浣� write_off锛屾洿鏂� next_seq
static void kv_scan_active(void)
{
    u32 base = kv_active_addr();
    u32 off = sizeof(kv_sec_hdr_t);

    s_next_seq = 1;

    while (off + sizeof(kv_rec_hdr_t) + 2 < ST_FLASH_SECTOR_SIZE)
    {
        kv_rec_hdr_t hdr;
        kv_read(base + off, &hdr, sizeof(hdr));

        // blank => end
        if (kv_is_blank_u8(hdr.state) && kv_is_blank_u8(((u8 *)&hdr)[1]))
        {
            break;
        }

        if (hdr.state == ST_KV_STATE_VALID &&
            hdr.magic == ST_KV_REC_MAGIC &&
            hdr.len <= ST_KV_MAX_VALUE_LEN &&
            (off + sizeof(hdr) + hdr.len + 2) <= ST_FLASH_SECTOR_SIZE)
        {
            // 鏇存柊 next_seq
            if (hdr.seq >= s_next_seq)
                s_next_seq = hdr.seq + 1;

            // 璺宠繃 payload + crc
            off += sizeof(hdr) + hdr.len + 2;
            // 4瀛楄妭瀵归綈锛堝彲閫夛紝閬垮厤纰庣墖锛�
            off = (off + 3) & ~3u;
        }
        else
        {
            // 纰板埌鍨冨溇/鍗婃潯璁板綍锛氫负浜嗗畨鍏紝鍋滄鍦ㄨ繖閲岋紝鍚庣画鍐欒鐩栬繖閲�
            break;
        }
    }

    s_write_off = off;
    if (ST_FLASH_SECTOR_SIZE - s_write_off < 64)
    {
        s_need_migrate = 1;
    }
}

static st_kv_ret_t kv_append(u16 key, const u8 *value, u16 len)
{
    if (len > ST_KV_MAX_VALUE_LEN)
        return ST_KV_ERR;

    if (s_need_migrate)
        return ST_KV_NO_SPACE;

    u32 base = kv_active_addr();
    u32 off = s_write_off;
    u32 need = sizeof(kv_rec_hdr_t) + len + 2;
    need = (need + 3) & ~3u;

    if (off + need > ST_FLASH_SECTOR_SIZE)
    {
        s_need_migrate = 1;
        return ST_KV_NO_SPACE;
    }

    // 1) 鍐� header锛坰tate=0xFF 鏈彁浜わ級
    kv_rec_hdr_t hdr;
    memset(&hdr, 0xFF, sizeof(hdr));
    hdr.state = 0xFF;
    hdr.key = key;
    hdr.len = len;
    hdr.seq = s_next_seq++;
    hdr.magic = ST_KV_REC_MAGIC;

    kv_write(base + off, &hdr, sizeof(hdr));

    // 2) 鍐� payload
    kv_write(base + off + sizeof(hdr), value, len);

    // 3) 鍐� crc
    u16 crc = crc16_hdr_payload(&hdr, value);
    kv_write(base + off + sizeof(hdr) + len, &crc, 2);

    // 4) 鎻愪氦锛氬彧鍐� state=0xA5锛�1->0锛�
    u8 st = ST_KV_STATE_VALID;
    kv_write(base + off + 0, &st, 1);

    // update write_off
    s_write_off = off + need;
    if (ST_FLASH_SECTOR_SIZE - s_write_off < 64)
    {
        s_need_migrate = 1;
    }
    return ST_KV_OK;
}

// 鎵炬煇涓� key 鐨勬渶鏂拌褰曪紙鍦� active sector 鎵弿锛�
static int kv_find_latest(u16 key, u32 *out_addr, kv_rec_hdr_t *out_hdr)
{
    u32 base = kv_active_addr();
    u32 off = sizeof(kv_sec_hdr_t);

    u32 best_addr = 0;
    kv_rec_hdr_t best;
    memset(&best, 0, sizeof(best));

    while (off + sizeof(kv_rec_hdr_t) + 2 < ST_FLASH_SECTOR_SIZE)
    {
        kv_rec_hdr_t hdr;
        kv_read(base + off, &hdr, sizeof(hdr));

        if (kv_is_blank_u8(hdr.state) && kv_is_blank_u8(((u8 *)&hdr)[1]))
            break;

        if (hdr.state == ST_KV_STATE_VALID &&
            hdr.magic == ST_KV_REC_MAGIC &&
            hdr.len <= ST_KV_MAX_VALUE_LEN &&
            (off + sizeof(hdr) + hdr.len + 2) <= ST_FLASH_SECTOR_SIZE)
        {
            // 楠� crc
            u8 buf[ST_KV_MAX_VALUE_LEN];
            kv_read(base + off + sizeof(hdr), buf, hdr.len);

            u16 crc_stored;
            kv_read(base + off + sizeof(hdr) + hdr.len, &crc_stored, 2);

            u16 crc_calc = crc16_hdr_payload(&hdr, buf);
            if (crc_stored == crc_calc)
            {
                if (hdr.key == key)
                {
                    if (hdr.seq >= best.seq)
                    {
                        best = hdr;
                        best_addr = base + off;
                    }
                }
                off += sizeof(hdr) + hdr.len + 2;
                off = (off + 3) & ~3u;
                continue;
            }
        }

        // 閬囧埌寮傚父锛屽仠姝�
        break;
    }

    if (best_addr)
    {
        if (out_addr)
            *out_addr = best_addr;
        if (out_hdr)
            *out_hdr = best;
        return 1;
    }
    return 0;
}

void st_kv_register_keys(const u16 *keys, u16 key_num)
{
    if (key_num > ST_KV_MAX_KEYS)
        key_num = ST_KV_MAX_KEYS;
    memcpy(s_keys, keys, key_num * sizeof(u16));
    s_key_num = key_num;
}

int st_kv_init(void)
{
    kv_sec_hdr_t sh0, sh1;
    int v0 = kv_sector_valid(kv_sec_addr(0), &sh0);
    int v1 = kv_sector_valid(kv_sec_addr(1), &sh1);

    if (!v0 && !v1)
    {
        // 棣栨锛氭牸寮忓寲 sector0 涓� gen=1
        if (!kv_sector_format(kv_sec_addr(0), 1))
            return 0;
        s_active_sec = 0;
        s_active_gen = 1;
    }
    else if (v0 && !v1)
    {
        s_active_sec = 0;
        s_active_gen = sh0.gen;
    }
    else if (!v0 && v1)
    {
        s_active_sec = 1;
        s_active_gen = sh1.gen;
    }
    else
    {
        // 閮� valid锛岄�� gen 鏇村ぇ鐨勪负 active
        if (sh1.gen >= sh0.gen)
        {
            s_active_sec = 1;
            s_active_gen = sh1.gen;
        }
        else
        {
            s_active_sec = 0;
            s_active_gen = sh0.gen;
        }
    }

    s_need_migrate = 0;
    s_erase_pending = 0;

    kv_scan_active();
    return 1;
}

st_kv_ret_t st_kv_set(u16 key, const void *value, u16 len)
{
    return kv_append(key, (const u8 *)value, len);
}

st_kv_ret_t st_kv_get(u16 key, void *out, u16 *inout_len)
{
    u32 addr;
    kv_rec_hdr_t hdr;
    if (!kv_find_latest(key, &addr, &hdr))
        return ST_KV_NOT_FOUND;

    if (*inout_len < hdr.len)
    {
        *inout_len = hdr.len;
        return ST_KV_ERR;
    }

    st_flash_read(addr + sizeof(hdr), hdr.len, (u8 *)out);
    *inout_len = hdr.len;
    return ST_KV_OK;
}

// 杩佺Щ锛氭妸 active 鏈�鏂板�兼嫹璐濆埌 inactive锛屽垏鎹� active
int st_kv_force_migrate(void)
{
    if (!st_flash_is_voltage_safe())
        return 0;

    u32 new_gen = s_active_gen + 1;
    u32 new_sec_addr = kv_inactive_addr();

#if (ST_ERASE_IMMEDIATE)
    st_flash_erase_sector(new_sec_addr);
#else
    // 寤惰繜鎿﹂櫎锛氬厛鏍囪寰呮摝锛堜笂灞傚彲鍦ㄥ畨鍏ㄧ獥鍙ｆ摝锛�
    s_erase_pending = 1;
    s_erase_addr = new_sec_addr;
    // 杩欓噷鐩存帴杩斿洖锛岃涓婂眰鍏堟摝锛涗綘涔熷彲浠ュ湪 kv_poll 閲屾摝
    return 2; // NEED_ERASE
#endif

    if (!kv_sector_format(new_sec_addr, new_gen))
        return 0;

    // 涓存椂鍒囨崲鍐欐寚閽堝埌鏂版墖鍖�
    u32 old_active = s_active_sec;
    u32 old_gen = s_active_gen;

    s_active_sec = old_active ^ 1u;
    s_active_gen = new_gen;
    s_write_off = sizeof(kv_sec_hdr_t);
    s_need_migrate = 0;

    // 鎶婃瘡涓� key 鐨勬渶鏂板�煎啓鍏ユ柊鎵囧尯锛堟寜娉ㄥ唽琛級
    for (u16 i = 0; i < s_key_num; i++)
    {
        u16 key = s_keys[i];
        u8 buf[ST_KV_MAX_VALUE_LEN];
        u16 len = sizeof(buf);

        // 鍏堜粠鏃� active 涓
        s_active_sec = old_active;
        s_active_gen = old_gen;
        st_kv_ret_t r;
        r = st_kv_get(key, buf, &len);

        // 鍐嶅啓鍒版柊 active
        s_active_sec = old_active ^ 1u;
        s_active_gen = new_gen;

        if (r == ST_KV_OK)
        {
            kv_append(key, buf, len);
        }
    }

    // 閲嶆柊鎵弿鏂� active锛屾洿鏂板啓鎸囬拡
    kv_scan_active();

    // 鏍囪 old sector 闇�瑕佹摝锛堝彲寤惰繜锛�
    s_erase_pending = 1;
    s_erase_addr = kv_sec_addr(old_active);

    return 1;
}

void st_kv_poll(void)
{
    if (s_need_migrate)
    {
        int r = st_kv_force_migrate();
        (void)r;
        // 濡傛灉 r==2 浠ｈ〃闇�瑕佸厛鎿� inactive 鎵囧尯銆備綘鍙湪涓婂眰鎵ц鎿﹂櫎鍚庡啀璋冪敤涓�娆°��
    }

#if (ST_ERASE_IMMEDIATE == 0)
    // 寤惰繜鎿﹂櫎锛氱敱涓婂眰鍐冲畾鈥滃畨鍏ㄧ獥鍙ｂ�濆啀璋冪敤 st_flash_erase_sector
    // 杩欓噷榛樿涓嶆摝锛岄伩鍏嶅崱 BLE
#endif
}
