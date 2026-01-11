# Fix Patch Notes（Task14）

## 1. AFE I2C 通信降级
- **修改文件**：`vendor/b85m_ble_sample/sh367309_datadeal.c`
- **内容**：
  - 新增 `afe_i2c_read_bytes()`，复制 Telink I2C 读流程并加入 `clock_time_exceed()` 超时判断（默认 5ms）。
  - 在 `App_AFEGet()` 中使用该函数并在失败时调用 `afe_mark_comm_error()`，设置 `System_ErrFlag.u8ErrFlag_Com_AFE1`、执行 `AFE_Sleep()` 并触发 `System_ERROR_UserCallback(ERROR_AFE1)`；成功时清零错误标志。
- **验证**：断开 AFE I2C 线后运行，确认 MCU 不死锁、`System_ErrFlag` 置位、MOS 关闭；恢复线路后再次读取可清除告警并继续上报。

## 2. 深度保留唤醒恢复外设
- **修改文件**：`vendor/b85m_ble_sample/app.c`
- **内容**：
  - `user_init_deepRetn()` 不再 #if0，保留 Telink 推荐的 `blc_ll_initBasicMCU()` / `blc_ll_recoverDeepRetention()`，并重新调用 `i2c_master_test_init()`、`SH367309_Enable_AFE_Wdt_Cadc_Drivers()`、`soc_param_lib_init()`，确保唤醒后首次采样为最新值。
- **验证**：配置设备进入 `cpu_sleep_wakeup(DEEPSLEEP_MODE, ...)` 后唤醒，读取 0xD000 通知应为新采集的电压且无残留错误标志。

## 3. Modbus Write 校验与队列化
- **修改文件**：
  - `vendor/b85m_ble_sample/app.c`：新增命令队列 `bms_cmd_enqueue()/bms_cmd_dequeue()`；`main_loop()` 支持批量消费地址。
  - `vendor/b85m_ble_sample/app_att.c`：在 `module_onReceiveData()` 中校验 CRC16、长度，合法后调用 `bms_cmd_enqueue()`。
  - `vendor/b85m_ble_sample/app.h`：声明队列 API。
- **效果**：BLE Write 命令在 LinkLayer 回调中先验证，再进入循环缓冲，主循环逐个响应，避免错误帧触发、连续读覆盖问题。
- **验证**：
  1. 向设备发送 CRC 错误的 Modbus 帧，设备不再响应。
  2. 连续发送多条合法读指令，确认全部按顺序返回。

这些修改均保持最小改动原则，未引入浮点或 64 位除法，且不会影响协议格式。
