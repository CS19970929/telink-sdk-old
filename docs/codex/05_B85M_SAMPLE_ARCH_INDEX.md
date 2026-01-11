# b85m_ble_sample 工程结构与入口索引

## 1. 启动入口
- `vendor/b85m_ble_sample/main.c:main`：MCU 唤醒 → 时钟/射频/GPIO 初始化 → 判断 `pm_is_MCU_deepRetentionWakeup()` → 调用 `user_init_normal()` 或 `user_init_deepRetn()` → `while(1){ main_loop(); }`。
- `vendor/b85m_ble_sample/main.c:irq_handler`：所有中断入口，依次调用 `app_timer_test_irq_proc()` 与 Telink 协议栈 `irq_blt_sdk_handler()`。

## 2. BLE 初始化入口
- `vendor/b85m_ble_sample/app.c:user_init_normal`：
  - MAC 与地址类型：`blc_initMacAddress()`、`blc_ll_setRandomAddr()`。
  - 控制器：`blc_ll_initBasicMCU()`、`blc_ll_initAdvertising_module()`、`blc_ll_initConnection_module()`、`blc_ll_initSlaveRole_module()`、`blc_ll_initPowerManagement_module()`。
  - Host/GAP/L2CAP：`blc_gap_peripheral_init()`、`my_att_init()`、`blc_l2cap_register_handler(blc_l2cap_packet_receive)`。
  - 广播：`bls_ll_setAdvData()`、`bls_ll_setScanRspData()`、`bls_ll_setAdvParam()`、`bls_ll_setAdvEnable(1)`。
  - 事件：`bls_app_registerEventCallback()` 绑定 `task_connect`、`task_terminate`、`ble_remote_set_sleep_wakeup`、`user_set_rf_power` 等。

## 3. ATT 表定义
- `vendor/b85m_ble_sample/app_att.c:my_Attributes`：包含 GAP、GATT、HID、Battery、Telink SPP、OTA 等服务，调用 `bls_att_setAttributeTable()` 注册。
- 关键自定义特征：
  - `SPP_SERVER_TO_CLIENT_DP_H`：Notify/Read，value 回调绑定 `module_onReceiveData()`。
  - `SPP_CLIENT_TO_SERVER_DP_H`：Write/Notify，供手机读写。

## 4. PM / 低功耗入口
- `vendor/b85m_ble_sample/app.c:ble_remote_set_sleep_wakeup`：在 `BLT_EV_FLAG_SUSPEND_ENTER` 回调中设置 GPIO 唤醒条件（当连接态且 suspend 时间 >30 ms）。
- `vendor/b85m_ble_sample/app.c:blt_pm_proc`：主循环尾部调用，依据 `CHG_IN_PIN`、`SW_PIN`、`最低单体电压` 等条件决定进入 `cpu_sleep_wakeup(DEEPSLEEP_MODE, ...)`，同时设置 `bls_pm_setSuspendMask()`。
- `vendor/b85m_ble_sample/app.c:user_set_rf_power`：`BLT_EV_FLAG_SUSPEND_EXIT` 回调，重新设置 `rf_set_power_level_index()`。

## 5. 主循环与调度入口
- `vendor/b85m_ble_sample/app.c:main_loop`：
  1. 调用 `blt_sdk_main_loop()` 驱动 BLE 协议栈。
  2. 处理 UI/键盘。
  3. 200 ms 周期任务：`App_AFEGet()`、`simulate_soc()`、`APP_SOC_IntEnhance_Ctrl()`、`adc_app_process_200ms()`、`charger_detect_and_keyLogi_200ms()`。
  4. 根据 `rev_master` 决定调用 `notify_votage()` 等上报函数。
  5. `soc_kv_store_update_and_log_if_changed()` 写 SOC。
  6. 调用 `blt_pm_proc()` 进入电源管理。

## 6. Notify 发送入口
- `vendor/b85m_ble_sample/app.c:notify_big_packet`：通用 Notify 分片函数。
- 上层封装：`notify_votage()`、`notify_protect_prarm()`、`notify_soc()`、`notify_protect_status()` 等，均在 `main_loop()` 调用 `notify_big_packet(BLS_CONN_HANDLE, SPP_CLIENT_TO_SERVER_DP_H, ...)` 发送。

## 7. Write/数据接收入口
- `vendor/b85m_ble_sample/app_att.c:module_onReceiveData`：ATT 写回调，解析 `rf_packet_att_write_t` 并设置 `rev_master`/`addr`；实质处理在 `main_loop()` 中完成。

## 8. 其他关键模块
- **Timer/IRQ**：`vendor/b85m_ble_sample/app.c:app_timer_test_init` 设置 Timer0；`app_timer_test_irq_proc` 在 IRQ 中驱动 `sif_send_data_handle()`。
- **I2C/AFE**：`vendor/b85m_ble_sample/sh367309_datadeal.c:App_AFEGet` 调用 `i2c_read_series()` 读取 SH367309 RAM，并更新 `g_stCellInfoReport`。
- **存储**：`vendor/b85m_ble_sample/soc_kv_store.c` 提供 append-only KV 存储，`main_loop()` 每轮调用 `soc_kv_store_update_and_log_if_changed()`。

该索引覆盖了 b85m_ble_sample 中“启动→BLE 初始化→ATT 注册→主循环调度→低功耗→收发数据”的主要入口，可作为分析实际 BMS 工程的基线模板。
