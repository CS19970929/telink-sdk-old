# SOC KV 存储审计与扩展方案

## A. 现状审计
1. **数据结构/写入频率**：`vendor/b85m_ble_sample/soc_kv_store.c:1-122` 定义 `soc_kv_data_t`（SOC/DSG/Cycle 三个 14bit 值）与 `soc_kv_store_update_and_log_if_changed()`（244-275行）。该函数每轮 `main_loop()` 都被调用（`vendor/b85m_ble_sample/app.c:1797-1799`），但只有当新值与 `g_last_logged` 不同时才写入，写频率取决于 SOC 变化；最坏情况下（SOC 每次计算都有增量）会每 200ms 写一次。
2. **调用的 Flash API**：同文件 43-80 行使用 `flash_read_page`/`flash_write_page`/`flash_erase_sector`（封装为 `flash_write_bytes`/`flash_erase_sector_safe`），均来自 `drivers/8258/flash.c:1-150`，这些 API 会关闭中断。
3. **BLE 连接态调用路径**：`soc_kv_store_update_and_log_if_changed()` 在主循环末尾执行，不区分广播或连接状态，因此在 BLE 已连接且持续 notify 时仍可能触发 Flash 写。
4. **关键约束检查**：
   - `flash_read_bytes` 读取 `REC_BYTES`（2 或 4 字节），满足 `<=64B` 要求。
   - 写入为单条记录 2 或 4 字节，持续时间短，但每次写之前不判断 BLE 状态。
   - `rollover()`（126-202 行）在扇区写满时调用 `flash_erase_sector_safe()`，也没有判断当前是否处于连接态；若在连接状态被触发，erase 可能阻塞 20~100ms，远超 220us 阈值。
5. **掉电安全性**：采用 append-only + 冗余（`REC_WORDS=2` 时 `word + ~word`），扫描时遇到脏记录即停止，具备半写入可恢复能力；但缺少 `magic/version/len` 字段，未来扩展参数较困难。
6. **磨损估算**：若 SOC 变化频繁导致每 0.2s 写一次，每条记录 4B，单扇区 4KB，可写 1024 条，约 205s rollover 一次；若 erase 在连接态执行，不仅影响 BLE，还会加速磨损。合理做法应增加“变化阈值 + 最小时间间隔”。
7. **对 BLE 的潜在影响**：
   - Flash API 会关闭中断，若写/擦发生在 `rev_master` notify 期间，`irq_blt_sdk_handler` 可能延迟，导致 Link Layer MIC failure。
   - Timer0 IRQ（驱动 `sif_send_data_handle()`）较重，与 Flash 写叠加会进一步推高 100us 安全上限。

## B. 扩展方案（KV + 日志 + BLE 时序协作）
1. **KV 设计**：
   - **格式**：`header( magic 2B | version 1B | type 1B | seq 2B | payload_len 2B ) + payload + CRC16`，总长控制在 32B 内，确保一次 `flash_read_page` 不超过 64B。
   - **写入策略**：继续使用 append-only 双扇区；新增 `kv_schedule_write(type,value)`，由主循环在“允许的窗口”批量写入；写前检查 `clock_time_exceed(last_flash_tick, 20*1000)` 等节流条件。
   - **禁止连接态 erase**：维护 `flash_maintenance_pending` 状态，只有在 `device_in_connection_state==0` 且 `ota_is_working==0` 时才调用 `flash_erase_sector_safe()`。
   - **API 建议**：
     - `storage_kv_read(type) -> value`：内部读取时以 32B 分片，用 `flash_read_page` 连续调用。
     - `storage_kv_commit(type,value)`：只标记待写，真正写入由 `storage_schedule_maintenance()` 完成。
2. **日志存储**：
   - 规划独立扇区，采用环形缓冲：`header( timestamp 4B | event_id 2B | payload_len 1B | payload <= 24B | CRC16 )`，全部控制在 32B。
   - 写策略：日志先写入 RAM 缓冲区，等到 BLE 未连接或处于 Idle（`clock_time_exceed(latest_user_event_tick, X)`）时批量 `flash_write_page`。
   - Erase 采用“双区”切换：当当前扇区空间不足时，标记“待擦”，在下一次非连接态执行。
3. **BLE 协作表**：
   | 场景 | 允许操作 | 禁止操作 |
   | --- | --- | --- |
   | 广播态 | 读/写≤32B、允许 erase（需暂停广播） | 长时间连续写 |
   | 连接态 | 只允许 ≤32B 写入，且单次之间至少 20ms；禁止 erase | 任何 `flash_erase_sector` |
   | OTA | 所有 Flash 写入暂停 | —— |
   - 实现：`storage_schedule_maintenance()` 每轮检查 `device_in_connection_state` / `ota_is_working` / `blc_ll_isControllerEventPending()`，决定执行写/擦或延后。
4. **API 模块化建议**：
   - `storage_kv_enqueue(type,value)`：业务层调用。
   - `storage_log_push(event_id, payload)`：故障/异常调用。
   - `storage_service_step()`：在 `main_loop()` 末尾调用，负责在安全窗口内执行写入与擦除（可结合 `clock_time_exceed` 与自定义状态机）。
   - 对 PM 流程：在 `blt_pm_proc()` 决定进入深睡前，调用 `storage_service_step()`，确保待写入数据已完成。

## C. 测试计划
1. **单元级**：
   - 构造半写入场景：手动修改 Flash 扇区，确认 `scan_sector()` 能跳过损坏记录。
   - 版本回退：增加 `version` 字段后，模拟旧版本数据并验证兼容读取。
2. **功能级**：
   - SOC 写入节流：模拟不同工况（加速 SOC 变化/保持稳定），记录 `storage_kv_commit` 次数，确保最坏情况下 ≤ 每秒 1 次。
3. **BLE 稳定性**：连接手机持续 `notify_votage()`，同时通过脚本频繁发送写命令，观察是否出现 `HCI_ERR_CONN_TERM_MIC_FAILURE (0x3D)`。
4. **最坏情况**：在 PC 上模拟高频 Modbus 请求 + UART 高负荷 + I2C 采样（`App_AFEGet()` 仍运行），同时触发 KV 写入，确认没有 notify 丢失或断连。
5. **磨损评估**：按“写入间隔 ≥1s、单扇区 4KB、Flash 寿命 100k 次”计算，预计寿命 > 100k 秒 / 2.7 小时 rollover，满足 BMS 需求；文档中应给出公式并记录真实参数。

## 最小改动补丁清单（建议实施）
1. `vendor/b85m_ble_sample/soc_kv_store.c`：在 `soc_kv_store_update_and_log_if_changed()` 外层增加 “节流 + 状态判断”，引入 `storage_schedule_maintenance()`。
2. `vendor/b85m_ble_sample/app.c:main_loop`：在 `soc_kv_store_update_and_log_if_changed()` 前增加 `storage_service_step()`，并在 `device_in_connection_state==0` 时触发 `flash_erase_sector_safe()`。
3. `vendor/b85m_ble_sample/soc_kv_store.h`：新增 `magic/version/seq` 字段定义，便于扩展参数与日志。

通过上述方案，可在满足 Telink Flash/BLE 时序约束的前提下扩展 KV 与日志存储，并降低磨损及连接风险。
