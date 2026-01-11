# 我的工程差异点与风险分级

## 1. 工程目录判定
- 在 `vendor/` 下仅有 `b85m_ble_sample` 含有 BMS 定制文件（如 `sh367309_datadeal.c/h`、`SocEnhance.c`、`soc_kv_store.c`、`sif_send.c` 等）；其他目录（`b85m_ble_remote`、`b85m_module` 等）仍保持官方示例结构。
- `vendor/b85m_ble_sample/app.c` 中大量引用 BMS 模块（`#include "sh367309_datadeal.h"`、`#include "SocEnhance.h"`、`#include "soc_kv_store.h"`），并且主循环实现 `App_AFEGet()` → `notify_votage()` 等业务逻辑，确认该目录就是“我的工程”。

## 2. 与原 sample 的关键差异与风险

| # | 差异点（文件/函数） | 描述 | 风险级别 |
|---|-------------------|------|---------|
| 1 | `vendor/b85m_ble_sample/sh367309_datadeal.c:App_AFEGet` + `i2c_read_series()` | 新增 SH367309 AFE 协议栈，但未检测 `i2c_read_series()` 是否成功、也没有超时/重试；通信异常时会卡在驱动 `while(reg_i2c_status & FLD_I2C_CMD_BUSY)`，且仍使用旧的 `g_stCellInfoReport` 数据 | **致命**：与项目简报中“AFE 异常必须降级”冲突，可能导致保护动作缺失 |
| 2 | `vendor/b85m_ble_sample/app.c:app_timer_test_irq_proc` + `sif_send_data_handle()` | Timer0 IRQ 每 0.5 ms 执行单线 UART 状态机，未使用 `volatile`/缓存保护 `g_stCellInfoReport`；IRQ 中处理逻辑较长，同时 BLE IRQ 也在同一入口 | **高**：可能造成 BLE 中断延迟、共享数据撕裂，从而引发 notify 丢包或 SIF 数据异常 |
| 3 | `vendor/b85m_ble_sample/app.c:user_init_deepRetn` | 几乎空实现（被 #if0 包住），深度保留唤醒后不会重新初始化 I2C、ADC、GPIO、KV，`App_AFEGet()` 依旧读取旧值 | **高**：与“唤醒后首次采样必须新鲜”要求不符，可能导致 MOS 仍然依据旧数据判断 |
| 4 | `vendor/b85m_ble_sample/soc_kv_store.c` | 自定义 KV 仅保存 SOC/DSG/Cycle，虽有 CRC，却没有对 `flash_write_page` 写入长度>64B 的节流逻辑，也未限制连接态写频率 | **中**：若在 BLE 连接高负载时频繁写 Flash，有概率阻塞 IRQ，影响 notify；同时未覆盖参数/日志需求 |
| 5 | `vendor/b85m_ble_sample/app_att.c:module_onReceiveData` | Modbus 写回调仅根据 `len`、`addr` 触发 `rev_master`，没有 CRC/长度校验、也没有请求队列 | **中**：错误帧可能导致越界访问，连续命令会被后发覆盖，影响 BLE 通信体验 |
| 6 | `vendor/b85m_ble_sample/app.c:notify_big_packet` | 固定 payload=20 B，且没有在发送失败时重试/缓存 | **低**：在实际工程中会限制吞吐，但不至于造成安全问题 |

## 3. 风险说明
- **致命**（差异#1）：违反项目简报第 2 条的安全与鲁棒性目标；应优先实现 I2C 超时、降级策略及错误上报。
- **高**（差异#2、#3）：
  - Timer0 IRQ 长期占用、共享变量无保护带来 BLE 稳定性和数据一致性问题。
  - 深度保留后未恢复外设会让 BMS 误判，尤其在掉电恢复、待机唤醒场景中风险很大。
- **中**（差异#4、#5）：影响可靠性和协议一致性，需要在后续阶段优化。
- **低**（差异#6）：属于性能优化范畴，可在完成安全相关事项后处理。

以上清单为后续 Phase（存储审计、优化报告、调试工具）提供输入，确保针对风险点制定改进计划。
