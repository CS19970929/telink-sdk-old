# BMS I2C 故障注入测试计划

## 1. 目标
- 验证 `bms_i2c_safe.c` 封装（Task16）在各种异常场景下的超时、重试、恢复及日志行为。
- 确认 I2C 异常不会破坏 BLE 连接，尤其避免 `MIC_FAILURE` 或断连。

## 2. 故障注入场景与期望
| 场景 | 注入方法 | 期望行为 |
| --- | --- | --- |
| AFE 断电/地址错误（NACK） | 拔掉 SH367309 VCC 或将 `dev7` 改为不存在的地址 | `bms_i2c_read` 在 `timeout_us` 内返回 -1，调用 `bms_i2c_on_error(LIGHT, TIMEOUT)`，超过重试后进入 `BMS_I2C_ERR_HEAVY` 并触发 `bms_i2c_recover_bus()`，`System_ErrFlag.u8ErrFlag_Com_AFE1` 置 1，若连续失败达到阈值进入安全降级（关闭 MOS/暂停均衡）。 |
| SDA 拉低卡死 | 用探针将 SDA 接地 | 第一次超时后 `bms_i2c_recover_bus()` 输出 9 个 SCL，Stop 条件后恢复；若 SDA 仍被拉低，持续上报重度错误并保持安全降级状态。 |
| SCL 拉低卡死 | 用探针将 SCL 接地 | 因 SCL 无法拉高，超时返回失败，恢复流程同上，日志记录 `reason=TIMEOUT`。 |
| Clock Stretching | 外部注入器延长 SCL 低电平（或降低 I2C 速度至 20kHz） | 在给定 `timeout_us`（如 5000us）内观察是否成功完成；若超时，调大 `timeout_us` 并验证可恢复，确保封装对 clock stretch 具备容忍度。 |
| 间歇性失败 | 在 PC 上用 MCU 模拟器拦截每第 N 次交易并 NACK | `fail_count` 递增，但 `recover_count` 也同步记录；当失败次数低于阈值时不触发降级，只重试一次成功；超过阈值进入中度/重度状态。 |
| 高负载 | 手机持续发送 BLE 命令 + PC 端通过 UART 高速调试；同时后台定时拉低 SDA 造成 I2C 重试 | BLE 日志无断连/MIC_FAILURE；`notify_votage()` 仍按顺序发送。若 I2C 多次失败导致降级，BLE 应上报错误码，日志显示最近故障时间。 |

## 3. 日志与统计项
- 在 `bms_i2c_on_error()` 中记录：
  - `fail_count`、`recover_count`
  - `last_error_reason`（TIMEOUT/RETRY_FAIL/BAD_CONTEXT）
  - `last_ok_time`（使用 `clock_time()` 转换为 ms）
- 通过 BLE 调试口或 UART 输出，例如：
  ```
  I2C_FAIL level=1 reason=0 fail=5 recover=1 last_ok=123456ms
  ```
- 在 `docs/codex/07_BMS_BLE_PROTOCOL_SPEC.md` 中的 notify 模式可增加一条“故障状态寄存器”，将上述信息映射到 0xD115 或新地址，方便上位机采集。

## 4. BLE 协同测试步骤
1. 手机（或 macOS 调试工具）持续以 100ms 间隔读取 0xD000 寄存器，保持连接 5 分钟。
2. 同时按场景注入 I2C 故障（如 SDA 拉低 500ms）。
3. 观察：
   - BLE 是否保持连接，无 `MIC_FAILURE`（可在控制台或抓包工具查看）。
   - 每次 I2C 超时时，是否通过 BLE notify 返回“数据不可信”标志。
   - 故障恢复后，是否自动重新上报最新数据且日志 `recover_count` 递增。

## 5. 逻辑分析仪观测点
- 探针连接：SDA、SCL、GND。
- 重点观测：
  1. 超时前是否仍有 Stop 条件（SCL=1 时 SDA 上升）。
  2. `bms_i2c_recover_bus()` 输出的 9 个 SCL 脉冲及最终 Stop 条件。
  3. 成功恢复后第一帧数据是否满足 `timeout_us` 内完成。
- 将逻辑分析仪捕获的波形与日志相对照，确保时间戳一致。

## 6. 测试矩阵（示例）
| 测试编号 | 场景 | timeout_us | retry | 预期结果 |
| --- | --- | --- | --- | --- |
| T1 | AFE 断电 | 5000 | 1 | 第一次失败，第二次失败 -> 触发 `bms_i2c_recover_bus()`，`System_ErrFlag` 置 1 |
| T2 | SDA 卡死 | 3000 | 0 | 立刻失败，recover 释放 SDA 后再次读成功 |
| T3 | Clock Stretch 10kHz | 5000 | 0 | 正常完成，未触发错误 |
| T4 | 间歇 NACK（每 5 次一次） | 4000 | 1 | 重试成功，不触发降级，`fail_count` ≈ 请求次数/5 |
| T5 | 高负载 + 故障 | 5000 | 1 | BLE 保持连接，日志显示 fail/recover 但 notify 正常 |

## 7. 结论与关联
- 本测试计划验证 Task16 封装的所有关键路径：超时、重试、总线恢复与日志统计。通过上述故障注入，可以证明 `bms_i2c_safe` 符合 BMS 可靠性需求，并为上位机提供可观测数据。
- 测试完成后，请将结果回填到 `docs/codex/reports/BMS_I2C_SAFE_API_DESIGN_AND_PATCH.md` 的验证章节或附录，形成闭环。
