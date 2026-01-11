#include "bms_storage.h"
#include <string.h>

#define PARAM_MAGIC      0xA55Au
#define LOG_MAGIC        0xDEADu
#define PARAM_VERSION    1

typedef struct __attribute__((packed)) {
	u16 magic;
	u8  version;
	u8  type;
	u16 len;
	u16 seq;
	u16 crc;
} param_record_hdr_t;

typedef struct __attribute__((packed)) {
	u16 magic;
	u16 seq;
	u16 event_id;
	u32 timestamp;
	u8  payload_len;
	u8  payload[STORAGE_LOG_PAYLOAD_MAX];
	u16 crc;
} log_record_t;

typedef struct {
	u32 active_base;
	u32 write_off;
	u16 seq;
	u8 loaded;
	u8 pending_erase_base;
} param_ctx_t;

typedef struct {
	u32 active_base;
	u32 write_off;
	u16 seq;
	u16 head_seq;
	u8 loaded;
	u8 pending_erase_base;
} log_ctx_t;

typedef struct {
	u16 len;
	u8  data[STORAGE_PARAM_MAX_LEN];
	u8  valid;
} param_cache_t;

static param_ctx_t g_param_ctx;
static log_ctx_t   g_log_ctx;
static param_cache_t g_param_cache[STORAGE_PARAM_SLOT_MAX];
static int g_storage_ble_busy = 0;
static u8 g_factory_reset = 0;

static u16 storage_crc16(const u8 *data, u16 len)
{
	u16 crc = 0xFFFF;
	for (u16 i = 0; i < len; i++)
	{
		crc ^= data[i];
		for (u8 b = 0; b < 8; b++)
		{
			if (crc & 1)
			{
				crc = (crc >> 1) ^ 0xA001;
			}
			else
			{
				crc >>= 1;
			}
		}
	}
	return crc;
}

static inline u32 align4(u32 x)
{
	return (x + 3) & ~3u;
}

static void flash_write_chunks(u32 addr, const u8 *buf, u16 len)
{
	while (len)
	{
		u16 chunk = (len > 32) ? 32 : len;
		flash_write_page(addr, chunk, (u8 *)buf);
		addr += chunk;
		buf += chunk;
		len -= chunk;
	}
}

static u8 storage_read_bytes(u32 addr, u8 *buf, u16 len)
{
	while (len)
	{
		u16 chunk = (len > 32) ? 32 : len;
		flash_read_page(addr, chunk, buf);
		addr += chunk;
		buf += chunk;
		len -= chunk;
	}
	return 1;
}

static void storage_param_cache_clear(void)
{
	for (int i = 0; i < STORAGE_PARAM_SLOT_MAX; i++)
	{
		g_param_cache[i].len = 0;
		g_param_cache[i].valid = 0;
	}
}

static void storage_switch_param_sector(u32 new_base)
{
	g_param_ctx.active_base = new_base;
	g_param_ctx.write_off = 0;
	flash_write_page(new_base, 0, NULL);
}

static void storage_mark_param_erase(u32 base)
{
	g_param_ctx.pending_erase_base = (u8)((base == STORAGE_PARAM_SECTOR_A) ? 1 : 2);
}

static int storage_param_sector_ready(u32 base)
{
	param_record_hdr_t hdr;
	storage_read_bytes(base, (u8 *)&hdr, sizeof(hdr));
	return (hdr.magic == PARAM_MAGIC);
}

static void storage_param_scan_sector(u32 base)
{
	u32 off = 0;
	while (off + sizeof(param_record_hdr_t) <= STORAGE_SECTOR_SIZE)
	{
		param_record_hdr_t hdr;
		storage_read_bytes(base + off, (u8 *)&hdr, sizeof(hdr));
		if (hdr.magic != PARAM_MAGIC || hdr.len == 0 || hdr.len > STORAGE_PARAM_MAX_LEN)
		{
			break;
		}
		u16 seq = hdr.seq;
		u8 type = hdr.type;
		u16 data_len = hdr.len;
		u8 payload[STORAGE_PARAM_MAX_LEN];
		storage_read_bytes(base + off + sizeof(hdr), payload, align4(data_len));
		u16 calc_crc = storage_crc16((u8 *)&hdr, sizeof(hdr) - sizeof(u16));
		calc_crc = storage_crc16(payload, data_len) ^ calc_crc;
		if (calc_crc == hdr.crc && type < STORAGE_PARAM_SLOT_MAX)
		{
			g_param_cache[type].len = data_len;
			memcpy(g_param_cache[type].data, payload, data_len);
			g_param_cache[type].valid = 1;
			if (seq >= g_param_ctx.seq)
			{
				g_param_ctx.seq = seq + 1;
			}
			g_param_ctx.write_off = off + sizeof(hdr) + align4(data_len);
		}
		else
		{
			break;
		}
		if (g_param_ctx.write_off > STORAGE_SECTOR_SIZE)
		{
			g_param_ctx.write_off = STORAGE_SECTOR_SIZE;
			break;
		}
		else
		{
			off = g_param_ctx.write_off;
		}
	}
}

static void storage_log_scan_sector(u32 base)
{
	u32 off = 0;
	while (off + sizeof(log_record_t) <= STORAGE_SECTOR_SIZE)
	{
		log_record_t rec;
		storage_read_bytes(base + off, (u8 *)&rec, sizeof(rec));
		if (rec.magic != LOG_MAGIC || rec.payload_len > STORAGE_LOG_PAYLOAD_MAX)
		{
			break;
		}
		u16 calc_crc = storage_crc16((u8 *)&rec, sizeof(rec) - sizeof(u16));
		if (calc_crc != rec.crc)
		{
			break;
		}
		if (rec.seq >= g_log_ctx.seq)
		{
			g_log_ctx.seq = rec.seq + 1;
		}
		if (g_log_ctx.head_seq == 0 || rec.seq < g_log_ctx.head_seq)
		{
			g_log_ctx.head_seq = rec.seq;
		}
		g_log_ctx.write_off = off + sizeof(rec);
		if (g_log_ctx.write_off > STORAGE_SECTOR_SIZE)
		{
			g_log_ctx.write_off = STORAGE_SECTOR_SIZE;
			break;
		}
		else
		{
			off = g_log_ctx.write_off;
		}
	}
}

static void storage_param_init(void)
{
	memset(&g_param_ctx, 0, sizeof(g_param_ctx));
	storage_param_cache_clear();
	if (storage_param_sector_ready(STORAGE_PARAM_SECTOR_A))
	{
		g_param_ctx.active_base = STORAGE_PARAM_SECTOR_A;
		storage_param_scan_sector(STORAGE_PARAM_SECTOR_A);
	}
	if (storage_param_sector_ready(STORAGE_PARAM_SECTOR_B))
	{
		u32 old_seq = g_param_ctx.seq;
		param_cache_t snapshot[STORAGE_PARAM_SLOT_MAX];
		memcpy(snapshot, g_param_cache, sizeof(snapshot));
		param_ctx_t ctx_backup = g_param_ctx;
		storage_param_cache_clear();
		memset(&g_param_ctx, 0, sizeof(g_param_ctx));
		g_param_ctx.active_base = STORAGE_PARAM_SECTOR_B;
		storage_param_scan_sector(STORAGE_PARAM_SECTOR_B);
		if (g_param_ctx.seq <= old_seq)
		{
			memcpy(g_param_cache, snapshot, sizeof(snapshot));
			g_param_ctx = ctx_backup;
		}
	}
	if (!g_param_ctx.active_base)
	{
		g_param_ctx.active_base = STORAGE_PARAM_SECTOR_A;
		g_param_ctx.write_off = 0;
		g_param_ctx.seq = 1;
	}
	g_param_ctx.loaded = 1;
}

static void storage_log_init(void)
{
	memset(&g_log_ctx, 0, sizeof(g_log_ctx));
	g_log_ctx.active_base = STORAGE_LOG_SECTOR_A;
	g_log_ctx.write_off = 0;
	g_log_ctx.seq = 1;
	storage_log_scan_sector(STORAGE_LOG_SECTOR_A);
	log_ctx_t ctx_backup = g_log_ctx;
	storage_log_scan_sector(STORAGE_LOG_SECTOR_B);
	if (g_log_ctx.seq <= ctx_backup.seq)
	{
		g_log_ctx = ctx_backup;
	}
	g_log_ctx.loaded = 1;
}

void storage_init(void)
{
	storage_param_init();
	storage_log_init();
}

void storage_set_ble_busy(int busy)
{
	g_storage_ble_busy = busy;
}

void storage_request_factory_reset(void)
{
	g_factory_reset = 1;
}

static void storage_param_schedule_erase(void)
{
	if (!g_storage_ble_busy && g_param_ctx.pending_erase_base)
	{
		u32 base = (g_param_ctx.pending_erase_base == 1) ? STORAGE_PARAM_SECTOR_A : STORAGE_PARAM_SECTOR_B;
		flash_erase_sector(base);
		g_param_ctx.pending_erase_base = 0;
	}
}

static void storage_log_schedule_erase(void)
{
	if (!g_storage_ble_busy && g_log_ctx.pending_erase_base)
	{
		u32 base = (g_log_ctx.pending_erase_base == 1) ? STORAGE_LOG_SECTOR_A : STORAGE_LOG_SECTOR_B;
		flash_erase_sector(base);
		g_log_ctx.pending_erase_base = 0;
		g_log_ctx.head_seq = g_log_ctx.seq;
	}
}

void storage_service_step(void)
{
	if (g_factory_reset)
	{
		if (!g_storage_ble_busy)
		{
			flash_erase_sector(STORAGE_PARAM_SECTOR_A);
			flash_erase_sector(STORAGE_PARAM_SECTOR_B);
			flash_erase_sector(STORAGE_LOG_SECTOR_A);
			flash_erase_sector(STORAGE_LOG_SECTOR_B);
			g_factory_reset = 0;
			storage_init();
		}
		return;
	}
	storage_param_schedule_erase();
	storage_log_schedule_erase();
}

static int storage_param_append(u8 type, const void *data, u16 len)
{
	if (len > STORAGE_PARAM_MAX_LEN)
	{
		return -1;
	}
	if (!g_param_ctx.loaded)
	{
		storage_param_init();
	}
	if (g_param_ctx.write_off + sizeof(param_record_hdr_t) + align4(len) > STORAGE_SECTOR_SIZE)
	{
		u32 old_base = g_param_ctx.active_base;
		u32 new_base = (old_base == STORAGE_PARAM_SECTOR_A) ? STORAGE_PARAM_SECTOR_B : STORAGE_PARAM_SECTOR_A;
		g_param_ctx.active_base = new_base;
		g_param_ctx.write_off = 0;
		storage_mark_param_erase(old_base);
	}
	param_record_hdr_t hdr;
	hdr.magic = PARAM_MAGIC;
	hdr.version = PARAM_VERSION;
	hdr.type = type;
	hdr.len = len;
	hdr.seq = g_param_ctx.seq++;
	hdr.crc = storage_crc16((u8 *)&hdr, sizeof(hdr) - sizeof(u16));
	hdr.crc ^= storage_crc16((const u8 *)data, len);
	flash_write_chunks(g_param_ctx.active_base + g_param_ctx.write_off, (u8 *)&hdr, sizeof(hdr));
	flash_write_chunks(g_param_ctx.active_base + g_param_ctx.write_off + sizeof(hdr), data, len);
	if (align4(len) > len)
	{
		u8 padding[4] = {0xFF, 0xFF, 0xFF, 0xFF};
		flash_write_chunks(g_param_ctx.active_base + g_param_ctx.write_off + sizeof(hdr) + len, padding, align4(len) - len);
	}
	g_param_ctx.write_off += sizeof(hdr) + align4(len);
	return 0;
}

int storage_param_set(u8 type, const void *data, u16 len)
{
	if (type >= STORAGE_PARAM_SLOT_MAX || !data)
	{
		return -1;
	}
	if (len > STORAGE_PARAM_MAX_LEN)
	{
		return -2;
	}
	memcpy(g_param_cache[type].data, data, len);
	g_param_cache[type].len = len;
	g_param_cache[type].valid = 1;
	return storage_param_append(type, data, len);
}

int storage_param_get(u8 type, void *out, u16 max_len, u16 *out_len)
{
	if (type >= STORAGE_PARAM_SLOT_MAX || !out)
	{
		return -1;
	}
	if (!g_param_cache[type].valid)
	{
		return -2;
	}
	if (max_len < g_param_cache[type].len)
	{
		return -3;
	}
	memcpy(out, g_param_cache[type].data, g_param_cache[type].len);
	if (out_len)
	{
		*out_len = g_param_cache[type].len;
	}
	return 0;
}

static int storage_log_append(const log_record_t *rec)
{
	if (g_log_ctx.write_off + sizeof(log_record_t) > STORAGE_SECTOR_SIZE)
	{
		u32 old_base = g_log_ctx.active_base;
		u32 new_base = (old_base == STORAGE_LOG_SECTOR_A) ? STORAGE_LOG_SECTOR_B : STORAGE_LOG_SECTOR_A;
		g_log_ctx.active_base = new_base;
		g_log_ctx.write_off = 0;
		g_log_ctx.head_seq = rec->seq;
		g_log_ctx.pending_erase_base = (old_base == STORAGE_LOG_SECTOR_A) ? 1 : 2;
	}
	flash_write_chunks(g_log_ctx.active_base + g_log_ctx.write_off, (const u8 *)rec, sizeof(log_record_t));
	g_log_ctx.write_off += sizeof(log_record_t);
	return 0;
}

int storage_log_push(u16 event_id, const void *payload, u8 len, u32 timestamp)
{
	if (len > STORAGE_LOG_PAYLOAD_MAX)
	{
		len = STORAGE_LOG_PAYLOAD_MAX;
	}
	log_record_t rec;
	rec.magic = LOG_MAGIC;
	rec.seq = g_log_ctx.seq++;
	rec.event_id = event_id;
	rec.timestamp = timestamp;
	rec.payload_len = len;
	if (payload && len)
	{
		memcpy(rec.payload, payload, len);
	}
	else
	{
		memset(rec.payload, 0, sizeof(rec.payload));
	}
	rec.crc = storage_crc16((u8 *)&rec, sizeof(rec) - sizeof(u16));
	storage_log_append(&rec);
	return rec.seq;
}

u16 storage_log_fetch(u16 start_seq, storage_log_entry_t *out, u8 max_entries)
{
	if (!max_entries || !out)
	{
		return 0;
	}
	u16 fetched = 0;
	for (int sector_idx = 0; sector_idx < 2 && fetched < max_entries; sector_idx++)
	{
		u32 base = (sector_idx == 0) ? g_log_ctx.active_base : ((g_log_ctx.active_base == STORAGE_LOG_SECTOR_A) ? STORAGE_LOG_SECTOR_B : STORAGE_LOG_SECTOR_A);
		u32 off = 0;
		while (off + sizeof(log_record_t) <= STORAGE_SECTOR_SIZE && fetched < max_entries)
		{
			log_record_t rec;
			storage_read_bytes(base + off, (u8 *)&rec, sizeof(rec));
			if (rec.magic != LOG_MAGIC || rec.payload_len > STORAGE_LOG_PAYLOAD_MAX)
			{
				break;
			}
			u16 crc = storage_crc16((u8 *)&rec, sizeof(rec) - sizeof(u16));
			if (crc != rec.crc)
			{
				break;
			}
			if (rec.seq >= start_seq)
			{
				out[fetched].event_id = rec.event_id;
				out[fetched].seq = rec.seq;
				out[fetched].timestamp = rec.timestamp;
				out[fetched].payload_len = rec.payload_len;
				memcpy(out[fetched].payload, rec.payload, rec.payload_len);
				fetched++;
			}
			off += sizeof(rec);
		}
	}
	return fetched;
}
