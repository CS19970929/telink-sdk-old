# BMS 工程优化建议

## 致命
- **I2C/AFE 通信缺乏降级策略**（`vendor/b85m_ble_sample/sh367309_datadeal.c:1472 App_AFEGet`）：`i2c_read_series()` 没有返回值判断，也未设置超时；当 SH367309 NACK 或总线卡死时，CPU 会在驱动 while 循环中阻塞，`g_stCellInfoReport` 仍保持旧值。*建议*：封装 `afe_i2c_transfer()` 检测 `FLD_I2C_CMD_BUSY` 超时，失败时设置 `System_ErrFlag`、关闭 `CHGMOS/DSGMOS` 并上报告警。*验证*：断开 AFE SDA/SCL，确认 MCU 能进入安全状态并通知上位机。

- **深度保留唤醒未恢复外设**（`vendor/b85m_ble_sample/app.c:1230 user_init_deepRetn`）：函数被 #if0 包裹，唤醒后不会重新初始化 I2C/GPIO/SOC，可能继续使用旧数据。*建议*：在 `user_init_deepRetn()` 中调用与 `user_init_normal()` 相同的 `i2c_master_test_init()`、`SH367309_Enable_AFE_Wdt_Cadc_Drivers()`、`soc_kv_store_init()` 等。*验证*：进入/退出 `cpu_sleep_wakeup(DEEPSLEEP_MODE, ...)` 后读取首帧电压，确认数据更新。

## 高
- **Timer0 IRQ 与 BLE 中断共享**（`vendor/b85m_ble_sample/app.c:704 app_timer_test_irq_proc`、`main.c:irq_handler`）：Timer0 每 0.5ms 调用 `sif_send_data_handle()` 执行状态机，时间较长且访问 `g_stCellInfoReport`，可能阻塞 `irq_blt_sdk_handler`，造成 notify 丢包。*建议*：在 `sif_send_data_handle()` 中仅做移位，将帧缓存交给主循环，同时将 `g_stCellInfoReport` 标记为 `volatile` 并在更新时短暂禁止 Timer0 中断。*验证*：BLE 长连接 + 高频 notify 期间监控 `HCI_ERR_CONN_TERM_MIC_FAILURE` 是否消失。

- **Modbus 写回调未过滤无效帧**（`vendor/b85m_ble_sample/app_att.c:428 module_onReceiveData`）：任何 Write 都会触发 `rev_master=true`，即便 CRC/长度错误，也会导致 `notify_votage()` 等错误执行。*建议*：在回调内加入 CRC16 校验、长度检查，并将请求放入循环队列防止覆盖。*验证*：使用脚本发送畸形帧，确认设备不会回应且有错误日志。

## 中
- **Flash KV 未考虑连接态约束**（`vendor/b85m_ble_sample/soc_kv_store.c:126 rollover`）：扇区满时直接 `flash_erase_sector_safe()`，可能在连接态执行，违反 220us/erase 禁止。*建议*：加入 `storage_schedule_maintenance()` 状态机，确保 erase 只在广播态或 idle 窗口执行。*验证*：连接状态下强制达到 rollover，确认不会立即擦除且 BLE 不掉线。

- **notify 分片写死 20B**（`vendor/b85m_ble_sample/app.c:1320 notify_big_packet`）：未根据实际 MTU 调整，导致 BLE 传输效率低。*建议*：在 `task_connect()` 完成后读取 `blc_att_getCurrentMTUSize()`，动态设置分片长度，并在发送失败时重试。*验证*：扩大 MTU 后抓包，确认单帧 payload 增大。

## 低
- **日志/状态输出缺少统一接口**（散落在 `printf`、SIF、BLE notify）：调试困难。*建议*：抽象 `bms_log_push(level, event_id, payload)`，统一写入 Flash 日志与串口输出。*验证*：触发告警时可从同一日志接口获取信息。

## 最小改动路线图
1. 实现 AFE I2C 超时与安全降级（`sh367309_datadeal.c`）。
2. 补齐 `user_init_deepRetn()`，同步恢复外设。
3. 重构 Timer0 IRQ，将重量级逻辑移至主循环，并给 `g_stCellInfoReport` 加保护。
4. 在 `module_onReceiveData()` 中加入 CRC/长度验证与请求队列。
5. 增加 `storage_schedule_maintenance()`，禁止连接态 erase。
6. `notify_big_packet()` 动态适配 MTU 并添加失败重试。
7. 建立统一日志接口，后续与 Flash 日志区对接。

## 长期重构路线图
- **通信层解耦**：将 BLE、SIF、UART 的收发规则统一抽象为 Transport 接口，配套协议解析（Modbus/TLV），便于扩展 PC/移动调试工具。
- **状态机化主循环**：将 `App_AFEGet()`、SOC 计算、保护判断拆成独立任务，并以事件总线同步，提升并发安全性。
- **存储子系统**：实现 KV + 日志 + 维护窗口的独立模块，与 PM/OTA 状态联动，Future 还可加入 OTA 元数据/设备信息。
- **测试基础设施**：提供脚本化 I2C/Flash 模拟、BLE 压测工具（可复用 tools/bms_debug_*），形成持续回归能力。
