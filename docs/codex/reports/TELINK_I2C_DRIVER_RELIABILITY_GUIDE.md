# Telink I2C 驱动可靠性分析与 BMS 最佳实践

## A. 驱动定位与接口梳理
- **源码位置**：
  - `drivers/8258/i2c.c` / `drivers/8258/i2c.h`：TLSR825x 芯片的硬件 I2C 驱动（TLSR8251 属于此系列）。
  - `drivers/8278/i2c.c/h` 提供 827x 同款接口，工程若迁移到 827x 可保持兼容。
- **核心 API（以 8258 为例）**：
  - 初始化相关：`i2c_gpio_set(I2C_GPIO_GroupTypeDef)` 选择 SDA/SCL 脚；`i2c_master_init(unsigned char SlaveID, unsigned char DivClock)` 设置目标地址与时钟；`reset_i2c_moudle()` 软复位模块。
  - 主机读写：`i2c_write_series()`、`i2c_read_series()`、`i2c_write_byte()`、`i2c_read_byte()`，内部直接访问 `reg_i2c_*` 寄存器并 busy-wait `FLD_I2C_CMD_BUSY`（见 `drivers/8258/i2c.c:200-360`）。
  - 从机模式：`i2c_slave_init()`、`i2c_slave_mapping_mode_data_buffer_config()`，支持 DMA 或 Mapping，两种模式的差异在 `i2c.h` 注释中有详细描述（Map 模式固定 64 字节偏移；DMA 模式支持 0x40000-0x4FFFF 的 SRAM 空间）。
- **软 I2C**：SDK 未提供软件 bit-bang 驱动，若需，请使用 GPIO 自行实现（参照 `drivers/8258/gpio.c`），或复用示例 `application/soft_i2c`（若有）。硬件 I2C 更稳定，推荐在 TLSR8251 上使用 `drivers/8258/i2c.c`。

## B. 使用方法
1. **标准初始化（主机模式）**：
   1. 选择引脚组：`i2c_gpio_set(I2C_GPIO_GROUP_C0C1)`（SH367309 在示例中走 C0/C1；见 `vendor/b85m_ble_sample/app.c:885-897`）。
   2. 设置 Slave ID 与时钟：`i2c_master_init(AFE_ID, CLOCK_SYS_CLOCK_HZ / (4 * Fs))`，其中 `DivClock = CLOCK_SYS_CLOCK_HZ/(4*I2C频率)`；示例使用 100kHz。
   3. 若需要复位：`reset_i2c_moudle()`，再重新执行上述步骤。
   4. GPIO 上拉：Telink EVM 默认外部 4.7k 上拉，若自行布板请保证 SDA/SCL 上拉到 3.3V。
2. **写寄存器事务**：
   - 调用 `i2c_write_series(((u16)addr << 8) | len, 2, buf, len)`；API 会按 “Start + Slave(W) + 地址字节 + 数据 ... + Stop” 时序发送，`AddrLen` 参数决定地址长度（0/1/2/3 字节）。
   - Telink 约定 `SlaveID` 含读写位：例如 AFE ID=0x5C（写）/0x5D（读）。使用 `i2c_master_init(0x5C, div)` 即可，API 内部通过 `reg_i2c_id |= FLD_I2C_WRITE_READ_BIT` 切换读写（`drivers/8258/i2c.c:260`）。
3. **读寄存器事务**：
   - 先写寄存器地址（`i2c_read_series` 内部已经处理：写地址→重复 Start→读数据），因此应用只需传入 `(Addr<<8)|Len` 与 `AddrLen=2`。
   - 如果目标设备需要分离的 “写寄存器地址 + Start + 读数据”，`i2c_read_series` 已按此流程实现（`drivers/8258/i2c.c:317-360`）。
4. **SH367309 推荐封装**：
   ```c
   int afe_read_block(u8 reg, u8 *buf, u8 len) {
       unsigned int addr = ((unsigned int)reg << 8) | len;
       i2c_write_series(addr, 2, NULL, 0); // 可选：若需要单独写寄存器
       i2c_read_series(addr, 2, buf, len);
       return 0; // Telink API 无返回值，可自行扩展
   }
   ```
   实际工程应在封装层添加超时/失败判断（见第 C 节）。

## C. 可靠性与异常处理能力
1. **是否可得知成功/失败？**
   - 所有 API 均为 `void` 或直接返回读取数据，没有状态位；示例代码中也没有检查 ACK。`reg_i2c_status` 仅用于检查 `FLD_I2C_CMD_BUSY` 是否完成，因此驱动无法告诉上层 NACK 还是成功。
2. **超时机制**：
   - `i2c_write_series()`、`i2c_read_series()`中均为 `while(reg_i2c_status & FLD_I2C_CMD_BUSY);` 死等（`drivers/8258/i2c.c:233, 317` 等）。若从设备未响应或总线被拉低，将永久阻塞 CPU。需要应用层引入 `clock_time_exceed()` 监控（如 Task14 已实现的 `afe_i2c_read_bytes`）。
3. **总线卡死恢复**：
   - SDK 提供 `reset_i2c_moudle()`（`drivers/8258/i2c.h:45-52`），可清除 I2C 数字逻辑；但若 SDA 被从设备拉低，需要应用层切换 GPIO 模式输出 9 个 SCL 脉冲再复位（驱动未内置）。
4. **从设备不在线时的表现**：
   - `reg_i2c_status` 永远 Busy → while 死等，CPU 卡死。应用层可通过外部看门狗/超时封装自救，并记录 `System_ErrFlag`。
5. **Clock Stretching**：
   - 代码中没有显式处理 SCL 拉伸；硬件手册指出 825x I2C 支持 Clock Stretch（SCL 接口自动判断），但由于驱动 busy-wait，长时间拉伸也会被视为 Busy，因此需要设定合理的 I2C 频率（≤100kHz）并配置超时判定。
6. **对 BLE 的影响**：
   - I2C 驱动没有关闭中断，但死循环会占用 CPU，若在中断上下文调用（例如 Timer IRQ）会阻塞 `irq_blt_sdk_handler`。在主循环中调用也会延长一次循环时间，可能导致 `blt_sdk_main_loop()` 延迟，出现 `MIC_FAILURE(0x3D)`。最佳策略是在主循环调度 I2C，并保证单次事务长度与通信频率控制在 5ms 以内。

## D. BMS 场景最佳实践
1. **健康度监测**：
   - 维护 `afe_fail_count`、`afe_recover_count`，在每次 I2C 读写失败（超时）时递增，连续失败阈值（例如 3 次）后触发 `System_ErrFlag.u8ErrFlag_Com_AFE1`，并打上“数据不可信”标记；成功一次即清零并记录恢复时间。
   - 采样结果结构体（如 `g_stCellInfoReport`）增加 `bool fresh` 标记，只有当本轮 I2C 成功时才置 1，BLE/SIF 上报前检查该标记，避免发送旧数据。
2. **异常恢复状态机**：
   - **轻度**：对单次失败立即重试 1~2 次（延时 1ms），仍失败则进入中度。
   - **中度**：调用 `reset_i2c_moudle()`、重新配置 GPIO/I2C、发送 9 个 SCL 脉冲释放 SDA，然后再次尝试读写。
   - **重度**：多次恢复失败后，执行 `AFE_Sleep()`、关闭 `CHGMOS/DSGMOS`，进入安全降级；同时通知上位机（BLE notify + SIF）。
   - 状态机可集成在 `App_AFEGet()` 外层，与 Task14 的 `afe_mark_comm_error()` 逻辑一致。
3. **BLE 调度策略**：
   - 在 `main_loop()` 中调度 I2C，确保 `blt_sdk_main_loop()` 先执行。
   - 若 BLE 连接频繁通知，可在 I2C 事务前检查 `blc_ll_isControllerEventPending()`，尽量避开控制器忙的窗口。
   - 对长帧读写（>32B）可分段读取，每段之间插入 `clock_time_exceed()` 判定，避免连续忙等。
4. **日志点**：
   - 每次进入轻度/中度/重度状态时，记录时间戳与状态到 `storage_log_push(EVENT_I2C_FAIL, {...})`，以 BLE/串口上报。
   - 统计一段时间内的失败次数，通过调试工具 (如 `tools/bms_debug_mac`) 导出。

## 结论摘要
- **可靠性结论**：Telink 原生 I2C 驱动功能完整但缺乏错误返回与超时保护，在高安全需求的 BMS 中“原样使用”并不足够。缺口主要在：无法检测 NACK、缺乏超时机制、总线卡死时会一直 busy-wait。通过应用层封装（超时、重试、状态机），可以补齐可靠性并保证 BLE 不受影响。
- **推荐封装 API**：
  ```c
  int bms_i2c_read_block(u8 reg, u8 *buf, u8 len, u8 retry);
  int bms_i2c_write_block(u8 reg, const u8 *buf, u8 len, u8 retry);
  void bms_i2c_recover(void);          // 重新 init + 9 个 SCL
  void bms_i2c_mark_fault(int level);  // 轻/中/重度故障上报
  ```
  API 内部使用 `clock_time_exceed()` 监控 busy-wait，并与 `System_ErrFlag` / MOS 控制联动。

## 测试计划
1. **逻辑分析仪**：在 SDA/SCL 上注入 NACK、拉低 SCL 模拟 clock stretch，观察封装层是否能在设定时间内退出并上报。
2. **故障注入**：
   - 断开 SH367309 电源，确认状态机进入重度并关闭 MOS。
   - 将 SDA 短接 GND，验证 `bms_i2c_recover()` 能通过 GPIO 释放总线。
3. **BLE 协同测试**：在手机持续发 Modbus 命令 + I2C 重试的场景下，抓包确认没有 `MIC_FAILURE`，并检查 notify 延迟。
4. **低功耗场景**：在 `blt_pm_proc()` 可能进入深睡的窗口重复触发 I2C 通信，保证唤醒后第一帧数据新鲜。

通过上述实践，TLSR8251 + SH367309 的 I2C 通信可以达到 BMS 所需的可靠性，并与 BLE 低功耗策略共存。
