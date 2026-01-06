#pragma once
#include "tl_common.h"

// ====================== Flash Geometry ======================
#ifndef ST_FLASH_SECTOR_SIZE
#define ST_FLASH_SECTOR_SIZE    4096u
#endif

// 你现在 PARAM_ADDR=0x78000（示例），请确保 KV/LOG 不与代码区和参数区重叠！
// 建议：1M flash 的话，留后面一大段给 KV/LOG。
//
// 下面给一个默认例子：
//   KV: 2 sectors (8KB)  at 0x74000
//   LOG: 16 sectors (64KB) at 0x76000
//
// 你需要按你固件实际布局修改：

#define ST_KV_BASE_ADDR         0x74000u
#define ST_KV_SECTOR_NUM        2u

#define ST_LOG_BASE_ADDR        0x76000u
#define ST_LOG_SECTOR_NUM       16u

// ====================== Behavior knobs ======================
// 是否允许在 storage_poll 里直接擦扇区（可能卡 12~500ms，影响 BLE）
// 0：延迟擦除（推荐，BLE 更稳）
// 1：立即擦除（实现简单，但可能影响连接/安卓表现）
#define ST_ERASE_IMMEDIATE      0

// KV 支持的 key 数（固定表，便于迁移时拷贝最新值）
#define ST_KV_MAX_KEYS          16u

// 单条 KV 最大 value 长度（你存 SOC/计数/小结构体足够了）
#define ST_KV_MAX_VALUE_LEN     128u

// 单条 LOG 最大 payload（事件 + 快照建议 < 128）
#define ST_LOG_MAX_PAYLOAD      128u

// LOG 追加时，剩余空间不足这个阈值就切换到下个 sector
#define ST_LOG_MIN_REMAIN       64u
