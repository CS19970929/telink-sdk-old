#ifndef BMS_STORAGE_H
#define BMS_STORAGE_H

#include "tl_common.h"
#include "drivers.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STORAGE_SECTOR_SIZE
#define STORAGE_SECTOR_SIZE      FLASH_SECTOR_SIZE
#endif

#ifndef STORAGE_PARAM_SECTOR_A
#define STORAGE_PARAM_SECTOR_A   FLASH_ADDR_USER_DATA_BASE1
#endif
#ifndef STORAGE_PARAM_SECTOR_B
#define STORAGE_PARAM_SECTOR_B   (STORAGE_PARAM_SECTOR_A + STORAGE_SECTOR_SIZE)
#endif

#ifndef STORAGE_LOG_SECTOR_A
#define STORAGE_LOG_SECTOR_A     (STORAGE_PARAM_SECTOR_B + STORAGE_SECTOR_SIZE)
#endif
#ifndef STORAGE_LOG_SECTOR_B
#define STORAGE_LOG_SECTOR_B     (STORAGE_LOG_SECTOR_A + STORAGE_SECTOR_SIZE)
#endif

#define STORAGE_PARAM_SLOT_MAX      16
#define STORAGE_PARAM_MAX_LEN       32
#define STORAGE_LOG_PAYLOAD_MAX     24

typedef enum {
	STORAGE_PARAM_SOC     = 0x01,
	STORAGE_PARAM_SOH     = 0x02,
	STORAGE_PARAM_CYCLE   = 0x03,
	STORAGE_PARAM_USER0   = 0x10,
	STORAGE_PARAM_USER1   = 0x11,
} storage_param_type_t;

typedef struct {
	u16 event_id;
	u16 seq;
	u32 timestamp;
	u8  payload_len;
	u8  payload[STORAGE_LOG_PAYLOAD_MAX];
} storage_log_entry_t;

void storage_init(void);
void storage_set_ble_busy(int busy);
void storage_service_step(void);
void storage_request_factory_reset(void);

int storage_param_set(u8 type, const void *data, u16 len);
int storage_param_get(u8 type, void *out, u16 max_len, u16 *out_len);

int storage_log_push(u16 event_id, const void *payload, u8 len, u32 timestamp);
u16 storage_log_fetch(u16 start_seq, storage_log_entry_t *out, u8 max_entries);

#ifdef __cplusplus
}
#endif

#endif
