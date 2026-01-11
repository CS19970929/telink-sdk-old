# SOC KV 存储审计与扩展设计

## A. 现状审计

### 1. 数据结构与格式
- 代码位置：`vendor/b85m_ble_sample/soc_kv_store.c`、`soc_kv_store.h`。
- 目前仅存储 3 个字段：`soc`（当前 SOC）、`dsg`（放电积分/SOH）、`cycle`（循环次数）。`scan_sector()`（34-80 行）默认值分别为 50/0/1。
- 存储格式：单条记录为 2B（或 4B，取决于 `SOC_STORE_REDUNDANT`），高 2bit 表示 `type`（0=SOC，1=SOH/DSG，2=Cycle），低 14bit 为值（`clamp_u14_safe()` 限制在 0~16382）。没有 `magic/version/len/seq/crc`，仅通过 `word + ~word` 校验（62 行）。

### 2. 写入触发与频率
- `soc_kv_store_update_and_log_if_changed()`（244-275 行）在 `main_loop()` 每轮调用（`vendor/b85m_ble_sample/app.c:1797-1799`），只要新值与 `g_last_logged` 不同就写 Flash。
- 变化阈值为 1（单位 1% SOC / 0.01?），没有时间节流；最坏情况（SOC 每 200ms 变 1）将以 5Hz 写入。
- `cycle` 同理，只要有变化就写。上电/关机没有特殊处理。

### 3. Flash API 使用
- 读：`scan_sector()` 循环调用 `flash_read_page()`，每次读取 `REC_BYTES`（2 或 4 B），符合 `<=64B` 要求。
- 写：`append_record()` 在 2 或 4 B 单元上调用 `flash_write_page()`；`rollover()`（101-146 行）在扇区满时调用 `flash_erase_sector_safe()` 两次（新扇区、旧扇区）。
- 调用上下文：写入/擦除都在主循环中进行，没有判断 BLE 连接态或 `blc_ll_isControllerEventPending()`。

### 4. 掉电安全
- 采用 append-only + `word + ~word` 冗余，可检测半写入；`rec_is_valid()` 在遇到无效数据时停止扫描，恢复到最近一次有效写入。
- 但没有 `magic/version`，无法扩展字段；也没有 `len/seq`，难以扩展到复杂结构。

### 5. BLE timing 风险
- 写入操作虽然仅 2-4B，但频率高（可能 5Hz），在连接态也会发生。`flash_write_page` 会关闭中断，时间取决于 Flash（通常几十微秒）；若叠加 Timer 中断或 BLE LL 时间片，存在 `MIC_FAILURE` 风险。
- `rollover()` 调用 `flash_erase_sector`（几十毫秒），若在连接态发生，100% 导致断连。当前逻辑没有避开连接态。

### 6. 问题清单
- **致命**：`rollover()` 可能在 BLE 连接态直接执行 `flash_erase_sector`（101-146 行），触发 20~100ms 的中断关闭，导致 LinkLayer 失败；同时 erasing 两个扇区加剧磨损。
- **高**：写入无节流，SOC 每变 1 就写一次，最坏 5Hz；在长期运行中易导致扇区磨损，且写频繁阻塞主循环。
- **中**：缺少 `magic/version/seq`，无法扩展更多参数；读取逻辑无法区分不同结构，未来引入新字段会破坏旧数据。
- **低**：`flash_erase_sector_safe()` 仅注释“允许卡顿时使用”，但实际在主循环直接调用；没有统计或报警机制，不易调试。

## B. 扩展存储架构设计

### 1. 扇区规划
- **参数 KV 区**：占用两个扇区（A/B）。扇区大小 4KB，可容纳大量记录。策略：平时 append-only，在进入非连接态或关机前切换扇区。
- **事件日志区**：至少两个扇区，采用环形或链式延伸，允许追加写。日志写满后切换下一个扇区并将旧扇区标记为“待擦”；擦除只在维护窗口进行（广播态或深睡前）。
- **维护窗口**：在 `main_loop()` 引入 `storage_maintenance_step()`，根据 `device_in_connection_state`、`ota_is_working` 决定是否允许写/擦。连接态禁止任何 erase。

### 2. 参数 KV 方案
- **数据格式**：
  ```
  struct kv_record_header {
      u16 magic;      // 固定 0xA55A
      u8  version;    // 结构版本
      u8  type;       // 参数类型
      u16 len;        // payload 长度（<= 32）
      u16 seq;        // 单调递增
      u16 crc;        // header + payload CRC16
  };
  payload[...];
  ```
- **写入流程**：
  1. 将记录写入 SRAM 缓冲。
  2. 使用 `flash_write_page` 分片（<=32B）写入扇区（可能需要多次写 + padding）。
  3. 写完后更新 `write_off`。
  4. 一旦扇区空间不足，在非连接态执行 `rollover`。
- **节流机制**：
  - SOC 变化 <1% 或小于 `Δ` 时不写。
  - 同一参数的写入间隔 >= 1 秒。
  - 关机前显式调用 `storage_flush_all()`，在 500ms 维护窗口写入一次。
- **读取**：启动时扫描扇区（类似 `scan_sector`）并记下最大 seq；若 A/B 都有效，选 seq 更大的扇区。

### 3. 事件日志方案
- **记录格式**：
  ```
  struct log_entry {
      u16 magic;     // 0xDEAD
      u16 seq;
      u16 event_id;
      u32 timestamp;
      u8  payload_len;
      u8  payload[24];
      u16 crc;
  };
  ```
- **写入策略**：
  - 使用 RAM 缓冲（例如 64B），主循环将事件写入缓冲，每当 BLE/主循环空闲时 flush 到 Flash，单次写 <=32B。
  - 每条日志写入后更新 `tail` 指针；若剩余空间不足从下一个扇区开始，原扇区标记为“待擦”。
  - 擦除策略：`storage_maintenance_step()` 检查 `maintenance_pending`，只有在 `device_in_connection_state==0` 且 `ota_is_working==0` 时触发 `flash_erase_sector`。
- **导出接口**：提供 `storage_log_iterate(start_seq, count)` 以 BLE notify 方式分批发送，或在 macOS/Win 工具中按 seq 拉取。

### 4. API 设计
```c
int storage_kv_get(u8 type, void *out, u16 len);
int storage_kv_set(u8 type, const void *in, u16 len, u32 timeout_us);
int storage_log_push(u16 event_id, const void *payload, u8 len);
int storage_log_read(u16 seq, void *out, u8 *len);
void storage_maintenance_step(void); // 在 main_loop() 末尾调用
```
- API 内部依赖 `bms_i2c_safe` 的调度经验：禁止在 IRQ 中调用，所有写入按片段执行，必要时挂起写入等待维护窗口。

### 实际实现（bms_storage.c/h）

为验证方案可落地，仓库已新增 `vendor/b85m_ble_sample/bms_storage.c/.h`：

- **参数 KV**：
  - 记录头包含 `magic/version/type/len/seq/crc`，payload ≤32B。`storage_param_set/get()` 更新 RAM 缓存并 append 到当前扇区；扇区满时切换到另一扇区，并把旧扇区标记为 `pending_erase`，由 `storage_service_step()` 在非连接态擦除。
  - 支持默认类型 `SOC/SOH/CYCLE` 以及 `USER0/USER1` 扩展，方便平滑替换 `soc_kv_store`。

- **事件日志**：
  - `storage_log_push()` 写入 `log_record_t`，payload ≤24B。日志同样采用双扇区、延迟擦除和序号索引，`storage_log_fetch()` 可按 seq 读取多条记录，便于 BLE/调试工具导出。

- **维护接口**：
  - `storage_init()` 在启动恢复上下文；`storage_set_ble_busy()` 供 BLE 事件或 `main_loop()` 设置连接状态；`storage_service_step()` 负责处理 pending erase 或出厂复位。
  - 所有 Flash 读写均分片 ≤32B，符合 “flash_read_page <=64B” 和 “写入短事务” 的约束。

- **集成建议**：
  - 在 `user_init_normal()` 完成后调用 `storage_init()`，在 `task_connect()/task_terminate()` 中设置 `storage_set_ble_busy(1/0)`。
  - 用 `storage_param_set()` 替换或并行 `soc_kv_store_put()`；将 I2C/保护等事件调用 `storage_log_push(event_id, payload, len, timestamp)`。
  - 在 `main_loop()` 尾部调用 `storage_service_step()`，保证连接态不触发擦除。

### 5. 最小改动路线图
1. **短期**：在现有 `soc_kv_store_update_and_log_if_changed()` 外层添加节流（SOC 变化≥1 且间隔≥1s），并禁止连接态 `rollover()`（检测 `device_in_connection_state`）。
2. **中期**：重构 `soc_kv_store` 为“header+payload+CRC”格式，加入 `magic/version/seq`，便于扩展新参数。
3. **中期+**：实现 `storage_kv_set/get` 接口，将其他参数（保护阈值等）逐步挪入该仓库。
4. **长期**：新增 `storage_log_push()` 与日志区，实现 I2C/保护/告警事件持久化。建立 `storage_maintenance_step()` 状态机，统一调度 Flash 写/擦。
5. 最后在 BLE/调试工具中暴露日志读取接口，支持远程诊断。

## C. 测试计划
1. **掉电测试**：
   - 在写入过程中断电（拔电或控制实验电源），重启后检查 `scan_sector()` 是否恢复到最近一次完整记录。
   - 重复 100 次记录快速变化 + 掉电，验证 no corruption。
2. **磨损估算**：
   - 假设节流后写入频率 ≤1Hz，单扇区 4KB，记录 4B → 1024 条/扇区；Flash 寿命 100k 次 → 1024*100k ≈ 1e8 条，换算为 1Hz 写入时 ≈ 3.2 年。需在文档中记录：`寿命 = (可写次数 × 单扇区记录数) / 写入频率`。
3. **BLE 协同**：
   - 连接手机持续 notify，强制写入（调用 `storage_kv_set()`）并观察 BLE 是否掉线（抓包确认无 `MIC_FAILURE`），必要时启用日志记录写入耗时。
4. **日志压力测试**：
   - 模拟大量事件（例如 每 10ms push 一条），确保写入缓冲机制能在 100% CPU下也不阻塞；当日志区写满后验证维护窗口能够按策略擦除。

## 结论摘要
- **当前实现**：可作为最小可用 SOC 持久化，但在 BLE 连接态存在擦除风险（致命）与写频率过高（高）。缺少扩展能力与数据格式标识。
- **推荐方案**：通过新增 header+CRC 的可扩展 KV + 追加事件日志区，并引入 `storage_maintenance_step()` 来协调 Flash 与 BLE 时序，即可达到商用品质。短期内至少要做写入节流和连接态禁止 erase。
- **补丁建议（最小）**：
  1. 在 `soc_kv_store_update_and_log_if_changed()` 加入时间节流和 `device_in_connection_state` 判断，避免连接态频繁写。
  2. 在 `rollover()` 执行前检查 `device_in_connection_state`，若为真则延后，等广播态/深睡前执行。

落地后，可与 Task16/17 的 I2C 安全封装、故障日志联动，形成完整的 BMS 安全链路。
