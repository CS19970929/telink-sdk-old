# SDK↔工程关联地图

## 启动 / 主循环
- `vendor/b85m_ble_sample/main.c:irq_handler` 统一处理中断：先调用本地 `app_timer_test_irq_proc()`，再把 BLE 中断转发给 `irq_blt_sdk_handler()`；该函数在 `stack/ble/controller/ll/ll.h:164` 声明，是 Telink 协议栈处理控制器事件的唯一入口。
- `vendor/b85m_ble_sample/main.c:main` 完成晶振/射频/时钟初始化后，根据 `pm_is_MCU_deepRetentionWakeup()` 选择 `user_init_deepRetn()` 或 `user_init_normal()`，然后在 while 循环内持续调用 `main_loop()`；BLE SDK 主循环在 `main_loop()` 内由 `blt_sdk_main_loop()`（`stack/ble/controller/ll/ll.h:172`）驱动。
- `vendor/b85m_ble_sample/app.c:1705-1799 main_loop` 在每次循环中：先跑 `blt_sdk_main_loop()`，随后执行 200 ms 采样任务（`App_AFEGet()`、`adc_app_process_200ms()`、`APP_SOC_IntEnhance_Ctrl()` 等）并在末尾调用 `soc_kv_store_update_and_log_if_changed()` 做 Flash 持久化。

## BLE 初始化与 ATT 表
- `vendor/b85m_ble_sample/app.c:939-1184 user_init_normal` 负责 BLE 协议栈初始化：依次调用 `blc_ll_initBasicMCU()`、`blc_ll_initAdvertising_module()`、`blc_ll_initConnection_module()` 等控制器接口（这些 API 在 `stack/ble/controller/ll/ll.h:191` 起声明），并通过 `blc_gap_peripheral_init()`（`stack/ble/host/gap/gap.h:104`）与 `my_att_init()` 完成 Host + ATT 表注册。
- `vendor/b85m_ble_sample/app_att.c:602 my_att_init` 最终调用 `bls_att_setAttributeTable()`（`stack/ble/host/attr/att.h:145`）把本工程的 GAP/GATT/HID/SPP 表 push 给栈；自定义 SPP 特征的写回调绑定到 `module_onReceiveData()`（`vendor/b85m_ble_sample/app_att.c:428`），用于接收 Modbus BLE 写入。

## BLE 事件 / Notify 路径
- `vendor/b85m_ble_sample/app.c:1033-1050` 通过 `bls_app_registerEventCallback()`（`stack/ble/controller/ll/ll.h:272`）注册 `task_connect()`、`task_terminate()`、`ble_remote_set_sleep_wakeup()` 等事件处理，形成 “SDK → 工程” 的回调链；回调内部根据连接状态设置 `device_in_connection_state`、调节唤醒源。
- BLE L2CAP/ATT 下行由 `blc_l2cap_register_handler(blc_l2cap_packet_receive)` 建立栈内默认处理链（API 在 `stack/ble/host/l2cap/l2cap.h:100/109`）；工程的 `module_onReceiveData()` 在 `app_att.c` 中被 Attribute Callback 调用，解析写入 Modbus 帧并将寄存器地址保存到 `addr`/`rev_master`，由 `main_loop()` 统一触发各类 `notify_xxx()` 上报。
- 所有 notify 最终走 `vendor/b85m_ble_sample/app.c:1320 notify_big_packet`，该函数把大包分片为 20 B，有失败会打印；它内部调用 `blc_gatt_pushHandleValueNotify()`（接口见 `stack/ble/host/attr/gatt.h:62`），确保符合当前 MTU。

## PM / Suspend 关联
- `vendor/b85m_ble_sample/app.c:1033-1070` 在 `user_init_normal()` 内启用 `blc_ll_initPowerManagement_module()`（`stack/ble/controller/ll/ll_pm.h:87`），并通过 `bls_pm_setSuspendMask()`/`blc_pm_setDeepsleepRetentionType()`（`stack/ble/controller/ll/ll_pm.h:94` 等）配置 ADV/CONN suspend 条件。
- `vendor/b85m_ble_sample/app.c:650-713 ble_remote_set_sleep_wakeup` 根据剩余唤醒 tick 追加 GPIO 唤醒；`blt_pm_proc()`（`vendor/b85m_ble_sample/app.c:710-882`）是工程对 `blt_pm_proc()` 调用栈的包装，会在 1 s 周期内根据充电口/按键状态调用 `cpu_sleep_wakeup()` 或维持连接，并在 OTA 时强制 `SUSPEND_DISABLE`。

## Timer / Tick 入口
- `vendor/b85m_ble_sample/app.c:529-566 app_timer_test_init` 直接配置 `reg_tmr0_*` 寄存器启用 500 µs tick 的 Timer0，并在 `main.c:irq_handler` 中把 `FLD_TMR_STA_TMR0` 中断交给 `app_timer_test_irq_proc()`。
- `vendor/b85m_ble_sample/app.c:704-742 app_timer_test_irq_proc` 判定 `reg_tmr_sta & FLD_TMR_STA_TMR0` 后会调用 `sif_send_data_handle()` 并清除 `reg_tmr_sta`；Timer0 由此成为自定义单线 UART 的调度入口。

## UART / Modbus 接入
- 工程未使用 Telink `uart_*` DMA，而是通过 `vendor/b85m_ble_sample/sif_send.c:232-410 sif_send_data_handle` + Timer0 中断按位翻转 `OWC_TX_PIN` 输出自定义 1‑Wire 帧（常量 `SIF_SEND_COUNT`、`state_mode` 控制 10 ms 状态机）；发送内容来自 `sif_send_PRIVATE_PACKETS_REALTIME_INFO()` 等函数（`sif_send.c:817-935`），这些函数读取 `g_stCellInfoReport` 并生成 Modbus/NUS 所需字段。
- BLE 侧 Modbus 写入由 `module_onReceiveData()` 接管，它解析 `rf_packet_att_write_t`，将寄存器地址写入 `addr` 并置 `rev_master=true`；`main_loop()` 检查该 flag 后选择调用 `notify_votage()/notify_protect_prarm()/notify_soc()` 等函数（`vendor/b85m_ble_sample/app.c:1639-1688`），统一走 BLE notify 通道实现“回复”。

## I2C / AFE 接入
- `vendor/b85m_ble_sample/app.c:885-918 i2c_master_test_init` 采用 `i2c_gpio_set()`/`i2c_master_init()` 配置 SDA/SCL，并在 `i2c_master_mainloop()` 里调用 `i2c_read_series()` 读取 SH367309 RAM；`i2c_read_series()` 的底层驱动位于 `drivers/8258/i2c.c:317-355`，负责发起 ID、地址和数据阶段。
- 真正的 AFE 采样在 `vendor/b85m_ble_sample/sh367309_datadeal.c:1472-1495 App_AFEGet` 内完成：每轮通过 `i2c_read_series()` 读取 `ram_reg_309`，然后依次调用 `UpdateVoltageFromBqMaximo()`、`DataLoad_CellVolt()`、`DataLoad_Temperature()`、`DataLoad_Current()` 等函数，把结果写入全局 `g_stCellInfoReport` 并依据 `ram_reg_309` 更新 `SystemStatus`，最后 `Fault_ChangeToMCU()` 输出保护位。
- 写 AFE 配置时则走 `TwiWrite()`/`MTPWrite()`（`vendor/b85m_ble_sample/sh367309_datadeal.c:226-275`），内部利用 `i2c_write_series()` 将配置字节 + CRC 写入 `SH367309_Reg_Store` 映射的寄存器；相关 bit 位（如 `CHGMOS/DSGMOS/SLEEP`）在 `SH367309_Reg_Store.REG_MTP_CONF` 中定义（`vendor/b85m_ble_sample/sh367309_datadeal.h:374`）。

## 工程定制插入点（相对 Telink 原 sample）
- `vendor/b85m_ble_sample/app.c:200-383` 添加 `adc_app_*` 抽象，基于 SDK 的 `adc_sample_and_get_result()`（`drivers/8258/adc.c:393`）轮询 3 路 GPIO，提供 mV/温度转换。
- `vendor/b85m_ble_sample/sh367309_datadeal.c`/`.h` 新增完整 SH367309 AFE 协议栈（含 I2C 通信、故障记录、MOS 控制）。
- `vendor/b85m_ble_sample/SocEnhance.c:95-960` 用 `APP_SOC_IntEnhance_Ctrl()` 等函数结合 `g_stCellInfoReport` 计算 SOC，并暴露 `soc_param_lib_init()` 供 `user_init_normal()` 调用。
- `vendor/b85m_ble_sample/soc_kv_store.c:1-310` 实现 KV Flash 存储，直接调用 `flash_read_page/flash_write_page/flash_erase_sector`（驱动在 `drivers/8258/flash.c:1-150`），`main_loop()` 通过 `soc_kv_store_update_and_log_if_changed()` 每次运行时维护 SOC/循环次数。
- `vendor/b85m_ble_sample/sif_send.c` 新增单线 UART + 自定义帧发送逻辑，依赖 Timer0 中断调度，与 SDK 原 HID Sample 完全不同，构成对外 Modbus/调试接口。
