# 优化建议（按严重级别）

## 致命
- **问题**：`App_AFEGet()` 未检测 I2C 返回值，通信失败或总线卡死不会触发安全降级。
  - **位置**：`vendor/b85m_ble_sample/sh367309_datadeal.c:1472-1495 App_AFEGet`
  - **风险**：`i2c_read_series()`（`drivers/8258/i2c.c:317-355`）内部 busy-wait `FLD_I2C_CMD_BUSY`；若 AFE NACK 或线缆短路，CPU 会无限等待且 `SystemStatus`/`g_stCellInfoReport` 保持旧值，违反 docs/02_约束中“不允许继续使用旧数据”的要求，也无法触发 `System_ERROR_UserCallback()`。
  - **建议**：将 `i2c_read_series()` 封装为带超时的 `afe_i2c_read()`，在超时或返回错误时立即设置 `System_ErrFlag.u8ErrFlag_Com_AFE1` 并调用 `AFE_Sleep()`、关闭 `CHGMOS/DSGMOS`；读取成功后再进入 `DataLoad_*`，否则跳过旧数据。`MTPWrite()` 同理，若写失败需退回安全状态。
  - **验证**：利用示波或 I2C 线开路方式复现 NACK，确认 MCU 不再卡死，`SystemStatus` 进入故障，MOS 输出关闭且 BLE/SIF 上报错误码。

## 高
- **问题**：中断与主循环共享的 `g_stCellInfoReport` 未做 `volatile`/锁保护，SIF 中断可能读到被编译器缓存或半更新的数据。
  - **位置**：结构体定义 `vendor/b85m_ble_sample/app.c:384`，Timer0 中断在 `vendor/b85m_ble_sample/app.c:704-742` 调用 `sif_send_data_handle()`；报文内容在 `vendor/b85m_ble_sample/sif_send.c:817-935` 直接读取 `g_stCellInfoReport`。
  - **风险**：`App_AFEGet()` 与 `DataLoad_*` 在主循环里逐字段写 `g_stCellInfoReport`，但中断同时读取同一数组，编译器可能把写操作缓存到寄存器或被中断抢占产生撕裂帧，导致 SIF 报文出现混合数据，BLE 的 `notify_protect_status()` 也可能读到未更新完的 `protect_status`。
  - **建议**：将 `g_stCellInfoReport` 声明改为 `volatile` 并在更新期间使用最小锁（例如禁用 Timer0 或在 `App_AFEGet()` 周围用 `irq_disable()/irq_restore()`）；或维护“双缓冲”：主循环更新缓冲区，更新完成后一次性 memcpy 到中断可见的数据区。
  - **验证**：在调试固件中打开 `printf` 或 SIF 报文校验，观察是否还会出现同一帧内部的电压/CRC 不匹配。也可增加单元测试：在 `App_AFEGet()` 中模拟长时间更新，检查中断读取是否总是得到完整数据。

- **问题**：深度休眠复位流程未恢复 BMS 外设，`user_init_deepRetn()` 几乎为空。
  - **位置**：`vendor/b85m_ble_sample/app.c:1230-1267 user_init_deepRetn`
  - **风险**：当 `blt_pm_proc()`（`app.c:710-882`）触发 retention wakeup 时只调用 `user_init_deepRetn()`，但该函数被 #if 0 包裹，没有重新初始化 I2C/I/O/SH367309，也没有重新调用 `soc_kv_store_init()` 或 `user_set_rf_power()`。从 deep retention 唤醒后，GPIO 和 AFE 可能保持睡眠态，首次采样不是“新鲜有效”，违反 docs/01/02 里“唤醒后首次采样必须有效”的要求。
  - **建议**：在 `user_init_deepRetn()` 中补齐 `blc_ll_initBasicMCU()` 之后的 BMS 初始化：重新调用 `i2c_master_test_init()`、`SH367309_Enable_AFE_Wdt_Cadc_Drivers()`、恢复 `adc_app_init()` 和 `user_set_rf_power()`，并根据需要重置 `sif_send` 状态机。若恢复顺序与 `user_init_normal()` 不同，可抽取公共函数避免重复。
  - **验证**：让设备进出深度休眠，读取 BLE/SIF 的第一帧电压、温度，确认不会出现 0 值或旧数据；同时通过 I2C 逻辑分析仪确认唤醒后立即有通信。

## 中
- **问题**：保护参数 `LoadParam()` 没有 CRC/版本校验，掉电或 Flash bit 翻转会加载随机门限。
  - **位置**：`vendor/b85m_ble_sample/param.c:34-83 LoadParam`
  - **风险**：函数直接 `flash_read_page(PARAM_ADDR, sizeof(PARAM_T), &g_tParam)`，仅比较 `ParamVer`，没有 CRC 或冗余扇区；若写入过程中掉电或 Flash 老化导致 bit 反转，MCU 会把损坏的 `g_tParam.protect` 作为真实门限，可能导致 MOS 不动作或误动作。
  - **建议**：在 `PARAM_T` 中加入 CRC 字段（例如 16bit），写入时先填充 CRC 再擦写；读取时先校验 CRC，失败则落入默认参数并报告 `System_ERROR_UserCallback(ERROR_AFE1)`。必要时实现 A/B 扇区（类似 `soc_kv_store`），避免单扇区被频繁擦写。
  - **验证**：人为修改 Flash 部分 bit 或在写入时拔电，重启后应检测到 CRC 错误，BMS 进入安全默认参数并上报告警。

- **问题**：BLE/Modbus 请求只保留最后一条且未做 CRC 校验，吞吐和稳定性受限。
  - **位置**：`vendor/b85m_ble_sample/app_att.c:428-458 module_onReceiveData`、`vendor/b85m_ble_sample/app.c:1757-1785 main_loop`。
  - **风险**：`module_onReceiveData()` 收到任意写请求后仅将 `addr = (data[2]<<8)|data[3]` 与 `rev_master=true`，既不校验 Modbus CRC（`Sci_CRC16RTU` 只在回复时使用），也没有队列；`main_loop()` 只处理一个 `addr`，若外部在 200 ms 窗口内发送多条命令，只有最后一条被响应，其余丢失。错误帧会直接触发 notify，可能造成越界访问。
  - **建议**：在 `module_onReceiveData()` 中校验 `len>=6` 且 CRC 正确后再入队，并将请求放入循环缓冲区（例如存储 `addr`+`长度`）；`main_loop()` 逐个出队，可一次发送多个 `notify`。若需要简单实现，至少在 `rev_master` 置位前校验 CRC 并忽略无效帧。
  - **验证**：编写 PC 端脚本快速发送多条读命令，确认全部都收到对等 notify，并且 CRC 错误的读请求被丢弃且有错误日志。

## 低
- **问题**：`notify_big_packet()` 永远使用 20 B payload，没有根据实际 MTU/DLE 调整，浪费吞吐。
  - **位置**：`vendor/b85m_ble_sample/app.c:1320-1355`
  - **风险**：连接建立后 `task_connect()` 仅请求 10 ms interval，没有配置 ATT MTU，`notify_big_packet()` 也把最大分片写死为 20 B。即便对端协商到 247 B MTU，本工程仍会发多次 20 B，导致 SOC/保护参数需要多帧才发完，延迟增大。
  - **建议**：在连接后调用 `blc_att_requestMtuSizeExchange()` 获取实际 MTU，或读取 `blc_gatt_getCurrentMTUSize()`，把 `TELINK_NOTIFY_PAYLOAD` 换算成 `mtu-3`。同时为大通知加上节流或 `blt_push_fifo` 的返回值重试机制，提升吞吐可靠性。
  - **验证**：使用支持 DLE 的调试 App 连接，抓包验证单帧 payload 是否增长到新的 MTU-3，整体通知时间缩短。

## 最小改动修复路线图（<=10条）
1. 在 `App_AFEGet()` 外层添加 `afe_i2c_read()` 包装并实现超时与安全降级。
2. 将 `g_stCellInfoReport` 标记为 `volatile` 并在 `App_AFEGet()` 更新期间短暂关停 Timer0，避免 SIF 访问撕裂数据。
3. 补全 `user_init_deepRetn()`，在 retention 唤醒后重新初始化 I2C、ADC、SIF GPIO、KV/SOC 模块。
4. 给 `PARAM_T` 增加 CRC 字段，对 `LoadParam()/SaveParam()` 实施读写校验。
5. 在 `module_onReceiveData()` 中加入 Modbus CRC/长度检查并实现简易请求队列。
6. `notify_big_packet()` 根据 `blc_gatt_getCurrentMTUSize()` 动态调整 payload 大小。

## 长期重构路线图
- 建立统一的“AFE 通信管理器”，把 I2C 访问、故障恢复、MOS 控制放入状态机，并提供接口给主循环/低功耗模块调用，避免分散在 `App_AFEGet()`、`charger_detect_and_keyLogi_200ms()` 中的重复逻辑。
- 把 BLE/Modbus 与 SIF 报文封装成队列驱动（包含 CRC、超时、QoS），并抽象出“上报管线”以共享 `g_stCellInfoReport` 的快照，兼容更多接口（UART、CAN）。
- 为 Flash 存储建立统一的“持久化服务”，抽象 kv/参数/日志的擦写策略，集中处理掉电安全与磨损均衡，并在低功耗前后统一 flush/恢复。
