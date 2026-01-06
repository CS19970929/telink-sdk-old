#pragma once
#include "tl_common.h"

int  storage_init(void);
void storage_poll(void);

// 延迟擦除：在你认为“安全窗口”（比如未连接、或连接 interval 很大、或你主动暂停 BLE 通知）调用
void storage_try_erase_in_safe_window(void);
