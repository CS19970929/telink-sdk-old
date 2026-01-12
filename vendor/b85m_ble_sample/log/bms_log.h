#ifndef BMS_LOG_H
#define BMS_LOG_H

#include "tl_common.h"

typedef enum {
	BMS_LOG_LEVEL_ERROR = 0,
	BMS_LOG_LEVEL_WARN,
	BMS_LOG_LEVEL_INFO,
	BMS_LOG_LEVEL_DEBUG,
} bms_log_level_t;

void bms_log_init(void);
void bms_log_set_level(bms_log_level_t level);
void bms_log_printf(bms_log_level_t level, const char *fmt, ...);
void bms_log_hexdump(bms_log_level_t level, const char *tag, const u8 *buf, u16 len);
void bms_log_event(bms_log_level_t level, u16 event_id, u32 a, u32 b, u32 c);
void bms_log_flush(void);

#endif
