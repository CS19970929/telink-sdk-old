# Telink SDK 运行模型（基于 TLSR825x）

## 1. 启动流程：main → SoC 初始化 → 用户初始化
1. `vendor/b85m_ble_sample/main.c:irq_handler` 是所有中断的入口，先执行用户自定义的 `app_timer_test_irq_proc()`，再调用 BLE 协议栈提供的 `irq_blt_sdk_handler()`（声明于 `stack/ble/controller/ll/ll.h`）。
2. `vendor/b85m_ble_sample/main.c:main` 在 RAM 中运行，流程如下：
   - 配置 32 kHz 时钟、`cpu_wakeup_init()`、`rf_drv_init()`、`gpio_init()`、`clock_init()` 等底层硬件。
   - 根据 `pm_is_MCU_deepRetentionWakeup()` 选择 `user_init_deepRetn()` 或 `user_init_normal()`（都在 `vendor/b85m_ble_sample/app.c`）。
   - 进入 `while(1)` 主循环，周期性清狗并调用 `main_loop()`。
3. 推荐做法：所有应用级初始化（GPIO、AFE、KV、协议参数）应放在 `user_init_normal()` 中，这里已经串联 Telink BLE 控制器/Host 初始化、应用自定义 ATT 表注册等，便于保持栈与业务的初始化顺序。

## 2. 主循环与事件协作
1. `vendor/b85m_ble_sample/app.c:1705 main_loop` 是 Telink 应用的核心循环：
   - 首句调用 `blt_sdk_main_loop()`（`stack/ble/controller/ll/ll.h`）驱动 LinkLayer、Host、定时事件等，这是 BLE 协议栈“事件循环”的实现。
   - 其后执行 UI/键盘逻辑、200 ms 周期任务（`App_AFEGet()`、`adc_app_process_200ms()`、`APP_SOC_IntEnhance_Ctrl()` 等）以及 KV 写入 (`soc_kv_store_update_and_log_if_changed()`)，最后调用 `blt_pm_proc()` 进入低功耗管理。
2. BLE 事件 → 应用回调链：
   - `user_init_normal()` 内通过 `bls_app_registerEventCallback()` 将 `task_connect()`、`task_terminate()`、`ble_remote_set_sleep_wakeup()` 等函数安装到 `BLT_EV_FLAG_*` 事件；这些回调最终在 `irq_blt_sdk_handler()` 中被触发，典型链路是 “LL 中断 → irq_handler → irq_blt_sdk_handler → 应用回调”。
   - ATT 写操作通过 `blc_l2cap_register_handler(blc_l2cap_packet_receive)` + ATT 表中的回调（例如 `vendor/b85m_ble_sample/app_att.c:428 module_onReceiveData`）传递到用户代码。
3. 推荐插桩方式：
   - **快速事务**：注册 BLE 事件回调或属性回调，让 SDK 帮你调度。
   - **周期任务**：在 `main_loop()` 中使用 `clock_time()`/`clock_time_exceed()`（`drivers/8258/timer.h`）做毫秒级节拍。
   - **硬实时任务**：通过硬件定时器中断（见第四节）调用轻量处理函数，处理完毕后把结果投递到主循环。

## 3. 初始化细节（user_init_normal）
`vendor/b85m_ble_sample/app.c:939-1184` 展示了 Telink 官方推荐的初始化顺序：
1. 生成/读取 BLE MAC（`blc_initMacAddress`），设置本地地址类型。
2. 初始化控制器模块：`blc_ll_initBasicMCU()`、`blc_ll_initStandby_module()`、`blc_ll_initAdvertising_module()`、`blc_ll_initConnection_module()`、`blc_ll_initSlaveRole_module()`、`blc_ll_initPowerManagement_module()`。
3. Host 层：`blc_gap_peripheral_init()`、`my_att_init()`（在 `vendor/b85m_ble_sample/app_att.c:602` 调用 `bls_att_setAttributeTable()`）、`blc_l2cap_register_handler()`、可选的 SMP 安全初始化。
4. 应用配置：设置广播包（`bls_ll_setAdvData()`）、Adv 参数（`bls_ll_setAdvParam()`）、启动 OTA（`blc_ota_initOtaServer_module()`）、注册事件回调。
5. 电源管理：`bls_pm_setSuspendMask()`、`blc_pm_setDeepsleepRetentionType()`、GPIO 唤醒配置等。
6. 工程定制初始化：I2C (`i2c_master_test_init()`)、参数加载 (`LoadParam()`)、AFE/GPIO 配置、KV/SOC 模块初始化等。

**应用插入建议**：
- 在 `user_init_normal()` 末尾添加自定义模块初始化（ADC、通信、任务调度器），确保在 `irq_enable()` 前完成。
- 如果需要在深度保留唤醒时恢复外设，可在 `user_init_deepRetn()`（`vendor/b85m_ble_sample/app.c:1230-1267`）复制关键初始化函数。

## 4. 中断体系与任务推荐
1. **IRQ 顶层**：`main.c:irq_handler` → `app_timer_test_irq_proc()` → `irq_blt_sdk_handler()`；用户应确保自己的 IRQ 逻辑短小，避免阻塞 BLE 中断处理。
2. **定时器中断**：`vendor/b85m_ble_sample/app.c:529-567 app_timer_test_init` 启动 Timer0，每 500 µs 触发一次；`app_timer_test_irq_proc()` 在检测到 `reg_tmr_sta & FLD_TMR_STA_TMR0` 后会调用 `sif_send_data_handle()` 并清除中断位。
3. **推荐用法**：
   - 对需要精确定时的任务（例如单线 UART bit-banging），在 IRQ 中只做“搬运/翻转”，不要执行耗时计算，把重逻辑留给主循环。
   - 对一般业务逻辑，优先使用 `main_loop()` 中的 `clock_time_exceed()`，避免额外 IRQ。

## 5. 事件驱动的任务插入点
1. **BLE 连接事件**：`task_connect()`、`task_terminate()`（`vendor/b85m_ble_sample/app.c:689-741`）可作为连接建立/断开时的状态切换钩子，例如刷新通知频率、重设 Modbus 状态机。
2. **OTA/功耗事件**：`bls_ota_registerStartCmdCb(app_enter_ota_mode)`、`bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_ENTER, ble_remote_set_sleep_wakeup)` 等钩子便于在 OTA 或 suspend 前清理任务。
3. **ATT 写回调**：自定义服务的 `att_readwrite_callback_t`（例如 `module_onReceiveData()`）是在 L2CAP 线程上下文执行的，应用应尽量只解析数据并投递标志（如 `rev_master`），避免在回调内执行复杂逻辑。

## 6. 推荐的“应用任务模型”
结合 Telink SDK 的特点，建议按以下层次组织业务：
1. **初始化阶段**（`user_init_normal()`）：完成 BLE 栈配置 + 硬件初始化 + 启动必要的外设。
2. **主循环任务表**：
   - 用 `clock_time_exceed()` 实现多个不同周期任务（例如 5 s 采样、1 s 心跳）。
   - 将 BLE 回复、KV 写入、AFE 报告等拆成状态机，避免一次处理过多数据。
3. **事件回调**：仅做状态切换、标志触发；复杂操作延迟到主循环。
4. **中断处理**：只用于必须的精确时序（Timer0→单线 UART，I2C DMA IRQ 等）。

通过上述方式，可以在不修改 Telink SDK 核心（stack/、drivers/）的前提下，实现业务与 BLE/电源管理的良好解耦。
