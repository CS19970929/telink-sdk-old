# b85m_module SPP/BLE/低功耗深度解析

## A. Module 框架总览
### 1. 启动与主循环调用链
```
Reset → vendor/b85m_module/main.c:main
  ├─ SoC 初始化：cpu_wakeup_init → rf_drv_init → gpio_init → clock_init
  ├─ 判断 pm_is_MCU_deepRetentionWakeup()
  │    ├─ 是：user_init_deepRetn()
  │    └─ 否：user_init_normal()
  └─ while(1)
       ├─ watchdog 清零
       └─ main_loop()
            ├─ blt_sdk_main_loop()
            ├─ app_power_management()
            └─ spp_restart_proc()
```

### 2. 生命周期约定
- **init**：`user_init_normal()` 执行一次，负责 battery check、BLE controller/host 初始化、SMP、ATT 表、SPP UART、HCI 回调等。
- **loop/run**：`main_loop()` 是唯一的轮询入口，先执行 BLE 协议栈，再调用各模块（功耗、SPP restart、可选的 battery check 等）。
- **event/callback**：
  - BLE Link Layer 事件 → `irq_handler()` → `irq_blt_sdk_handler()` → `spp.c:controller_event_handler()`（通过 `blc_hci_registerControllerEventHandler` 注册）。
  - BLE Host 事件 → `app_host_event_callback()`。
  - UART DMA IRQ → `main.c:irq_handler()` 在 `HCI_USE_UART` 条件下处理。

### 3. BLE vs Module 时序（ASCII 图）
```
                 +-------------------+         +-------------------+
                 |  BLE Controller   |         | Module (SPP/Batt) |
                 +-------------------+         +-------------------+
LinkLayer IRQ -->| irq_blt_sdk_handler|--event->| controller_event  |
                 |                   |         | handler (spp.c)   |
                 +-------------------+         +-------------------+
Main loop   -->  | blt_sdk_main_loop |--poll-->| app_power_mgmt    |
                 |                   |         | spp_restart_proc  |
                 +-------------------+         +-------------------+
```
BLE 协议栈运行在 `blt_sdk_main_loop()` 与 IRQ 回调中；模块层通过事件回调获知状态，再在主循环中执行耗时逻辑。

## B. SPP 深度分析
### 1. 定义与代码定位
- SPP 文件：`vendor/b85m_module/spp.c`/`spp.h`。
- ATT 定义：`app_att.c` 中 0x000F-0x0016 段定义 `TelinkSppServiceUUID`，两个特征：
  - `TelinkSppDataServer2Client`（handle `SPP_SERVER_TO_CLIENT_DP_H`）：属性 READ | WRITE_WITHOUT_RSP | WRITE，用于手机→设备。
  - `TelinkSppDataClient2Server`（handle `SPP_CLIENT_TO_SERVER_DP_H`）：属性 READ | NOTIFY，用于设备→手机。
- 回调：`module_onReceiveData()`（`app_att.c:206-234`）是 Write 回调。

### 2. 数据流
**RX（手机→模块）**：
```
手机 Write/WriteCmd → ATT → module_onReceiveData()
   → 将数据打包成 spp_event_t（token=0xFF, eventId=0x07A0）
   → spp_send_data(HCI_FLAG_EVENT_TLK_MODULE, pEvt)
   → 进入 SPP/HCI 通道 → 上位机 UART
```
**TX（模块→手机）**：
```
模块侧：spp.c 中的逻辑（例如 controller_event_handler、spp_restart_proc）
  → 将数据写入 spp_tx_fifo
  → tx_to_uart_cb()/uart_dma_send → HCI → blc_gatt_pushHandleValueNotify
  → Telink BLE SPP Notify（handle SPP_CLIENT_TO_SERVER）
```
`spp_rx_fifo` 和 `spp_tx_fifo`（`app.c:26-54`）是环形缓冲队列，保障串口与 BLE 之间的流控。

### 3. SPP 与 BLE 的关系
- 这是 Telink BLE 自定义 SPP（类似 Nordic UART Service），运行在 BLE ATT 层；并非经典 BR/EDR SPP。
- SPP 模块通过 `blc_register_hci_handler()` 把 UART DMA 数据注册为 BLE HCI handler，实现“Telink 模块 ↔ 外部 MCU”的串口协议。

### 4. 可靠性机制
- 采用 FIFO 做缓冲，但无帧序号/CRC/ACK；依赖 BLE 的有序传输。
- 队列满的处理：`app_module_busy()` 中检查 `UART_TX_BUSY`/`UART_RX_BUSY`，但若 FIFO 满，只是阻塞，不会丢弃或重试。

### 5. 对 BMS 的借鉴
- 可沿用“BLE ↔ UART/HCI” 的 FIFO + 回调框架，复用 `spp_rx_fifo/spp_tx_fifo` 和 `spp_send_data()` 的思路。
- 需要补充：帧头/CRC/seq、命令队列溢出日志、发送失败重试、与 I2C 任务协同。

## C. BLE 通讯细节
### 1. ATT 表/Handle
- `app_att.c` 定义所有服务，SPP handle 在 0x000F-0x0016，OTA handle 0x0017-0x001B。
- Handle 常量在 `app_att.h` 定义，可供其他模块引用。

### 2. 接收路径
- `module_onReceiveData()` 是唯一入口，`len = p->l2capLen - 3`；未做粘包处理。
- 将数据封成 `spp_event_t` 经 `spp_send_data()` 发往外部主机。

### 3. 发送路径
- `blc_gatt_pushHandleValueNotify()` 是核心 API（在 `spp.c` 内），没有显式节流或返回值检查。
- 数据由 UART/HCI 事件触发，`spp_tx_fifo` 作为队列；一旦 `uart_dma_send()` 成功，就 `my_fifo_pop()`，没有重试机制。

### 4. 连接参数/MTU
- `controller_event_handler()` 在连接后调用 `bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS,CONN_INTERVAL_15MS,...)`，确保 10-15ms interval；MTU 未扩展，默认 23。若需要更大 MTU 需在此基础上扩展。

### 5. 与 Android/iOS 的关系
- 无节流/无 ACK/MTU 固定 20B → 当主机（Android）使用 Write Without Response + 高频命令时易丢包。iOS 因 Write With Response 节流而表现更稳定。
- 推荐在 `blc_gatt_pushHandleValueNotify` 返回非 `BLE_SUCCESS` 时重试、在 `module_onReceiveData` 中限制命令频率。

## D. 低功耗（PM）
### 1. 关键接口
- `app_power_management()`：`main_loop()` 中调用，决定 `bls_pm_setSuspendMask` 和 GPIO 唤醒。
- `app_module_busy()`：检查 UART FIFO 和 `GPIO_WAKEUP_MODULE`，若 BUSY 则禁止进入 suspend，并拉高 `GPIO_WAKEUP_MODULE` 让外部 MCU 知晓。
- `battery_check` 提供 `app_suspend_enter_low_battery()`，在低电压时阻止 deep sleep。

### 2. 状态机
- **广播态**：若 `app_module_busy()` 返回 0，则允许 `SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV`。
- **连接态**：若连接且不忙，允许 `SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN`；否则 `SUSPEND_DISABLE`。

### 3. 阻止条件
- `tick_wakeup`（wake flag）非零：表示刚唤醒 500us 内不允许进入 suspend。
- `module_uart_data_flg` 未清零：表示 UART 仍有数据待发。
- `battery_check` 报低电压：阻止 deep。

### 4. 对 BMS 的迁移建议
- 采用同样的“busy 投票”机制：定义 `bms_module_busy()`，综合 I2C 采样/Flash 写入/日志等状态，统一交给 `app_power_management` 判断。
- 未连接时，main loop 可设置“采样定时器为 5s”，同时 `storage_set_ble_busy(0)` 允许 `storage_service_step()` 擦除日志；连接时标记 `storage_set_ble_busy(1)`，减少 Flash 操作。
- 在 `app_power_management` 中增加“AFE/I2C 正在运行”判断，避免在采样过程中进入 suspend。

## E. 可直接抄的清单 vs 必须避坑
### 1. 值得直接复用的 10 个函数/文件
1. `vendor/b85m_module/app.c:user_init_normal()` — 初始化顺序模板。
2. `vendor/b85m_module/app.c:main_loop()` — BLE+模块调度骨架。
3. `vendor/b85m_module/app.c:app_power_management()` — 功耗投票与 suspend 控制。
4. `vendor/b85m_module/app.c:app_module_busy()` — UART/GPIO 忙碌判定（可扩展为多外设）。
5. `vendor/b85m_module/spp.c:controller_event_handler()` — BLE 事件集中处理。
6. `vendor/b85m_module/app_att.c:my_att_init()` — ATT/GATT 表结构。
7. `vendor/b85m_module/app_att.c:module_onReceiveData()` — Write 回调 → 事件封包流程。
8. `vendor/b85m_module/battery_check.c:adc_vbat_detect_init()` — ADC 初始化范式。
9. `vendor/b85m_module/main.c:irq_handler()` — BLE IRQ + UART DMA 组合处理方式。
10. `vendor/b85m_module/spp.c:spp_restart_proc()` — 模块命令到系统行为的映射，示范如何在 main loop 执行耗时命令。

### 2. 必须避开的 10 个坑
1. `module_onReceiveData()` 无 CRC/长度校验，BMS 必须加防护。
2. `notify` 无节流/重试（`blc_gatt_pushHandleValueNotify` 返回值被忽略）。
3. 命令队列无上限报警（`spp_rx_fifo` 满时只阻塞）。
4. UART DMA 在 IRQ 中处理较多逻辑，需谨慎扩展，避免在 IRQ 中做重活。
5. battery_check 只检测电压，不做安全降级，BMS 需加入 MOS 控制。
6. `app_module_busy()` 只判断 UART，BMS 要扩展 I2C/Flash 状态，否则 PM 会在采样期间进入 suspend。
7. 默认 MTU 固定 23，不适配 Android/iOS 差异。
8. `spp_restart_proc` 直接调用 `cpu_sleep_wakeup`，BMS 若需要重启，应确保外设安全停机。
9. `main_loop` 没有 watchdog 对特定模块进行独立监控，BMS 需自己加。
10. 没有日志系统，调试困难；BMS 应结合 `bms_storage` 记录事件。

### 3. 推荐骨架（BMS 通讯 + PM）
```
user_init_normal()
  ├─ battery_check_init
  ├─ bms_i2c_safe_setup + storage_init
  ├─ BLE init (blc_ll_xxx, my_att_init)
  ├─ bms_comm_init (类似 spp_init)
  └─ register BLE/APP callbacks

main_loop()
  ├─ blt_sdk_main_loop()
  ├─ bms_comm_process()     // BLE ↔ 上位机
  ├─ bms_sampling_process() // 每 200ms / 5s 采样
  ├─ storage_service_step()
  └─ app_power_management() // busy 投票（I2C/Flash/Comm）

controller_event_handler()
  ├─ 更新连接状态、调节 conn param
  ├─ storage_set_ble_busy()
  └─ 触发日志/告警通知

app_module_busy()
  ├─ 检查 UART FIFO、I2C busy、Flash busy
  └─ 影响 suspend/deep sleep
```

## 结论摘要
- **SPP/BLE/PM 范式是否适合 BMS 商用？** 适合作为骨架，但需补齐可靠性（CRC/seq/日志）、超时/节流、资源投票等安全机制。
- **需要加强的三块**：
  1. BLE 承载：加入 send queue、节流、ACK/重试，与 Android/iOS 兼容。
  2. 功耗与外设协调：扩展 `app_module_busy`，确保 I2C/Flash 任务不会被 suspend 打断，并实现连接态/非连接态不同调度策略。
  3. 日志与健康度：结合 `bms_storage` 记录事件、fail_count，方便调试 Android 漏帧等问题。

Telink 的 b85m_module 展示了官方推荐的“模块化 + 事件驱动 + 主循环调度”模式。将其思想迁移到 BMS（尤其在通信与功耗层面）能显著提升工程可维护性，但必须补齐安全与可靠性，才能满足 BMS 商用需求。
