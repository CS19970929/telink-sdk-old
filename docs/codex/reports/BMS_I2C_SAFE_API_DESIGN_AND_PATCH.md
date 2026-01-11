# BMS I2C 安全封装设计与补丁说明

## 1. 驱动缺口与证据
- Telink 官方 I2C 驱动位于 `drivers/8258/i2c.c`，读写函数 `i2c_write_series()`、`i2c_read_series()`（第 240~360 行）在每一步都执行 `while(reg_i2c_status & FLD_I2C_CMD_BUSY);`，没有返回值。如果从设备 NACK 或总线被拉低，CPU 将一直 busy-wait。
- `i2c_master_init()`（`drivers/8258/i2c.h:82-108`）要求参数 SlaveID 包含 R/W 位，因此在多从设备场景中需要重复设置 `reg_i2c_id`，但 API 不提供动态切换能力。
- 示例工程 `vendor/b85m_ble_sample/sh367309_datadeal.c:1472 App_AFEGet` 直接调用 `i2c_read_series()` 读取 SH367309 RAM，一旦 AFE 不在线或响应慢，会阻塞主循环并影响 BLE。

## 2. 安全封装概述
新增源码：
- `vendor/b85m_ble_sample/bms_i2c_safe.c`
- `vendor/b85m_ble_sample/bms_i2c_safe.h`

功能特性：
1. **API**：
   ```c
   int bms_i2c_read(u8 dev7, u16 reg, u8 reg_len, u8 *buf, u16 len, u32 timeout_us, u8 retry);
   int bms_i2c_write(u8 dev7, u16 reg, u8 reg_len, const u8 *buf, u16 len, u32 timeout_us, u8 retry);
   void bms_i2c_safe_setup(I2C_GPIO_GroupTypeDef group, unsigned char divClock);
   void bms_i2c_recover_bus(void);
   void bms_i2c_on_error(bms_i2c_error_level_t level, bms_i2c_error_reason_t reason); // weak hook
   ```
   - `dev7` 为 7 位地址，函数内部自动生成读/写地址。
   - `timeout_us` 是单次事务的最大耗时，若 busy-wait 超时立即返回失败。
   - `retry` 表示额外重试次数（总尝试 = retry+1）。

2. **超时与上下文保护**：
   - `bms_i2c_wait()` 使用 `clock_time_exceed()` 避免死等，保证事务在 `timeout_us` 内完成。
   - `bms_i2c_check_context()` 检测 `reg_irq_src`，禁止在 IRQ 中调用，一旦命中返回 `BMS_I2C_REASON_BAD_CONTEXT`。

3. **总线恢复**：
   - `bms_i2c_recover_bus()` 将 SDA/SCL 切换为 GPIO，若 SDA 被拉低，输出 9 个 SCL 脉冲释放，再生成 Stop 条件并调用 `reset_i2c_moudle()` + `i2c_master_init()`。

4. **故障上报**：
   - 通过弱函数 `bms_i2c_on_error()` 提供三种等级（轻/中/重），方便工程在 `soc_kv_store` 或 BLE 日志中统计。

5. **配置**：
   - `bms_i2c_safe_setup()` 选择 I2C 引脚组与分频，可复用现有 C0/C1 100kHz 配置。若未显式调用，默认使用 `I2C_GPIO_GROUP_C0C1`、100kHz。

## 3. 集成改动点（建议）
| 位置 | 当前逻辑 | 替换方案 |
| --- | --- | --- |
| `vendor/b85m_ble_sample/app.c:user_init_normal` | `i2c_master_test_init()` 设置 GPIO + 100kHz | 在函数末尾调用 `bms_i2c_safe_setup(I2C_GPIO_GROUP_C0C1, CLOCK_SYS_CLOCK_HZ/(4*100000));` |
| `vendor/b85m_ble_sample/sh367309_datadeal.c:1472 App_AFEGet` | `i2c_read_series(...)` 直接读 AFE RAM | 改为 `bms_i2c_read(AFE_ID>>1, 0x4000, 2, (u8*)&ram_reg_309, len, 5000, 1);` 并根据返回值决定是否触发降级（Task14 已示范超时处理） |
| 其他 I2C 写配置函数 | `i2c_write_series()` | 对应替换为 `bms_i2c_write()`，从而获得返回状态 |

> 注意：所有 I2C 操作都应在主循环或任务上下文执行，禁止在 Timer/GPIO IRQ 里调用。`bms_i2c_read/write` 已在运行时检查 IRQ 上下文，违规调用会直接返回。

## 4. 验证建议
1. **单元测试**：仿照 `bms_i2c_try_read` 的 timeout 参数，故意将从设备地址写错，确认函数在 5ms 内返回 -1，并进入 `bms_i2c_on_error()`。
2. **总线卡死**：将 SDA 短接 GND，调用 `bms_i2c_read()`，确保先返回失败，再调用 `bms_i2c_recover_bus()` 能释放总线，之后重新读取成功。
3. **BLE 协同**：连接手机持续读寄存器，同时让 AFE 断电，观察 BLE 不再出现 `MIC_FAILURE`，并在日志中输出 `BMS_I2C_REASON_TIMEOUT` 计数。

## 5. Patch Notes
- 新增 `vendor/b85m_ble_sample/bms_i2c_safe.c/.h`，实现带超时/重试/总线恢复的 I2C 安全 API。
- API 默认禁止 IRQ 调用，并提供弱函数供工程记录失败次数。
- 建议在 `user_init_normal()` 中调用 `bms_i2c_safe_setup()`，在所有 AFE 读写位置替换为 `bms_i2c_read/write`。

## 6. 风险与后续
- 该封装依赖 `clock_time()`，因此在 deep retention 唤醒后需保证系统 tick 已恢复（`user_init_deepRetn()` 已补齐）。
- 对于频繁大块读写，建议 `timeout_us` ≥ `len * 100` 微秒，避免误判。
- 后续可与 Task17 的故障注入计划联动，使用 `bms_i2c_on_error()` 统计 `fail_count/recover_count` 并通过 BLE 上报。
