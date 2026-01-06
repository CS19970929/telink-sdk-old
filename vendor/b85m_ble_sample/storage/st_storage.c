#include "st_storage.h"
#include "st_kv.h"
#include "st_log.h"
#include "st_cfg.h"
#include "st_flash_port.h"

// 这里用 extern 拿到 st_kv.c / st_log.c 的 pending 标志不优雅。
// 为了简洁，我给你一个“统一建议”：
//   你可以把 pending 标志改成对外 API（st_kv_need_erase / st_kv_do_erase）
// 但这里先给“最少改动”的落地方式：你直接在各模块里改成对外 API。
// ——所以本文件先只做 init/poll。

int storage_init(void)
{
    // 注册你要持久化的运行态 key（示例）
    // 你可以定义：SOC、累计 Ah、循环次数、上次状态、上次关机原因等
    static const u16 keys[] = { 0x0001, 0x0002, 0x0003, 0x0010 };
    st_kv_register_keys(keys, sizeof(keys)/sizeof(keys[0]));

    if (!st_kv_init()) return 0;
    if (!st_log_init()) return 0;
    return 1;
}

void storage_poll(void)
{
    st_kv_poll();
    st_log_poll();
}

void storage_try_erase_in_safe_window(void)
{
#if (ST_ERASE_IMMEDIATE==0)
    // 推荐做法：你在 st_kv/st_log 内提供 “need_erase + addr” 的 getter
    // 然后这里判断并擦除。
    // 目前先留空，避免你误在连接关键期擦除导致安卓丢包。
#endif
}
