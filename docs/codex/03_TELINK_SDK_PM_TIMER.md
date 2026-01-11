# Telink SDK 低功耗与时间基准

## 1. 低功耗模式总览
- Telink BLE 应用入口 `vendor/b85m_ble_sample/main.c:main` 在完成硬件初始化后调用 `user_init_normal()`（或 `user_init_deepRetn()`），随后不断执行 `main_loop()`；主循环末尾的 `blt_pm_proc()`（`vendor/b85m_ble_sample/app.c:710-882`）负责根据业务状态进入 suspend/deepSleep。
- SDK 通过 `blc_ll_initPowerManagement_module()`（`vendor/b85m_ble_sample/app.c:1033`）启用控制器 PM，再用 `bls_pm_setSuspendMask()` 配置可进入的模式。例如 `user_init_normal()` 在 OTA 未进行时执行：
  ```c
  if (!ota_is_working) {
      bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
  }
  ```
  表示广播/连接态均允许 suspend 或带 SRAM 保留的 deep retention。
- `blc_pm_setDeepsleepRetentionType()`、`blc_pm_setDeepsleepRetentionThreshold()`、`blc_pm_setDeepsleepRetentionEarlyWakeupTiming()`（同一文件 1048-1066 行）进一步配置保留内存大小与提前唤醒时间，确保控制器能按 LL 时序唤醒。

## 2. 进入/阻止 suspend 的条件与策略
- **进入条件**：`blt_pm_proc()` 每秒检查 CHG/按键状态（`clock_time_exceed(sleep_tick, 1000*1000)`），当充电器未插入且键松开时，累计 `sleep_cnt` 达 5 次就调用 `AFE_Sleep()` + `cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0)`，让系统进入深度睡眠；当最低单体电压低于 3.0 V 并持续 60 秒，则强制睡眠并配置 `cpu_set_gpio_wakeup(SW_PIN, Level_Low, 0)` 作为唤醒源。
- **阻塞因素**：
  - `ota_is_working` 在 OTA 过程中被 `app_enter_ota_mode()`（`vendor/b85m_ble_sample/app.c:515-527`）置 1，此时 `user_init_normal()` 里的 `bls_pm_setSuspendMask(SUSPEND_DISABLE)` 禁止 suspend，避免 OTA 过程断电。
  - 若 `sendTerminate_before_enterDeep==1`，`blt_pm_proc()` 会等待 `BLT_EV_FLAG_TERMINATE` 将其置 2 后再允许进 deepSleep（ 742 行），确保 LL Terminate 数据包发完。
  - UI 功能（键盘/按键）在示例中默认关闭。如果打开 `UI_BUTTON_ENABLE`，代码会在 `main_loop()` 中根据 `button_not_released` 改变 `bls_pm_setSuspendMask()`，防止按键期间休眠。
- **推荐策略**：
  - 所有可能阻塞 suspend 的状态位都应集中管理，例如 OTA、长时间通知、外设忙等，以免与 `bls_pm_setSuspendMask()` 冲突。
  - 进入深睡前必须确保 AFE/I2C 等外设进入安全状态（示例调用 `AFE_Sleep()` 即是如此）。

## 3. 唤醒后的恢复点
- 协议栈在从 suspend/retention 唤醒时触发 `BLT_EV_FLAG_SUSPEND_EXIT`，示例通过 `bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, user_set_rf_power)`（`vendor/b85m_ble_sample/app.c:1040-1046`）重新设置射频功率，防止 suspend 重置 RF 寄存器。
- `user_init_deepRetn()`（`vendor/b85m_ble_sample/app.c:1230-1267`）是深度保留唤醒时的入口，当前示例仍处于 #if 0；在 BMS 工程中应在此函数里恢复 `i2c_master_test_init()`、GPIO 状态、KV/SOC 缓存等，确保“唤醒后的首次采样为新鲜数据”。
- 建议在 `BLT_EV_FLAG_SUSPEND_ENTER` 的回调（`ble_remote_set_sleep_wakeup()`，同文件 640-666 行）中根据 wakeup tick 设置 `bls_pm_setWakeupSource(PM_WAKEUP_PAD)`，确保满足 30 ms 以上的 suspend 时间时能通过 GPIO 唤醒。

## 4. 时间基准与 Timer
- **系统 tick**：`drivers/8258/timer.h:97-140` 定义 `clock_time()`（直接返回 `reg_system_tick`）和 `clock_time_exceed(ref, us)`（以 16 MHz tick 计算微秒差值）。主循环和 PM 逻辑都通过这些 API 实现毫秒级调度，无需额外定时器。
- **硬件 Timer**：`vendor/b85m_ble_sample/app.c:529-567 app_timer_test_init` 打开 Timer0（500 µs 周期），`app_timer_test_irq_proc()` 在 `reg_tmr_sta & FLD_TMR_STA_TMR0` 时调用 `sif_send_data_handle()` 并清除中断。若需要更大的时间片，可调整 `reg_tmr0_capt` 或启用 Timer1/2（示例保留模板）。
- **Suspend 与 tick 的关系**：
  - `clock_time()` 在 deepSleep 期间不会累计，因此在进入 `cpu_sleep_wakeup()` 前应记录参考 tick；醒来后 `clock_time_exceed()` 依据新的 `clock_time()` 继续运行。
  - 若使用 retention 模式（`DEEPSLEEP_RETENTION_*`），SRAM 中的 `sleep_tick`、`update_bms_info_tick` 等 `_attribute_data_retention_` 变量（见 `vendor/b85m_ble_sample/app.c:1718`) 会保留，醒来后可继续使用。

## 5. BMS 场景下的 5 秒周期任务建议
1. **主循环调度**：在 `main_loop()` 中新增一个 `_attribute_data_retention_ static u32 afe_sample_tick`，利用 `clock_time_exceed(afe_sample_tick, 5 * 1000 * 1000)` 判断是否超过 5 秒；当 BLE 未连接（`!device_in_connection_state`）时才执行 AFE 采样，否则保持 200 ms 频率。
2. **配合 suspend**：
   - 当决定降频采样时，可调用 `bls_pm_setManualLatency()` 增大 Latency（例如 4-6 个连接事件），让控制器有更长时间进入 suspend。
   - 若准备让 MCU 进入 `cpu_sleep_wakeup(DEEPSLEEP_MODE, ...)`，应在入睡前通过 `bls_pm_setWakeupSource(PM_WAKEUP_PAD|PM_WAKEUP_TIMER)` 配置硬件定时唤醒；Timer 唤醒时间可通过 `bls_pm_setWakeupTimer(us)`（若使用 Telink 提供的 API）或外部 RTC 实现。
3. **BLE 连接时的兼容性**：即便在低频模式，`blt_sdk_main_loop()` 必须每轮调用；因此“5 s 采样”仅影响业务逻辑（`App_AFEGet()`、上报频率），而不是暂停主循环。必要时可在 `task_connect()` 中设置 `device_in_connection_state` 并在 `main_loop()` 检查该变量，动态调整采样周期。

## 6. 实践要点
- 所有进入/退出低功耗的路径必须考虑 AFE 的安全状态：示例在多处调用 `AFE_Sleep()`，BMS 工程应扩展为“通信失败→立即休眠并关 MOS”。
- `clock_time_exceed()` 的参数为微秒，与上层毫秒概念不同；例如 5 秒需要传入 `5 * 1000 * 1000`。
- Timer 中断函数（如 `app_timer_test_irq_proc()`）用 `_attribute_ram_code_` 标记，并在 `irq_handler` 中优先执行；保持函数极短可以确保 `irq_blt_sdk_handler()` 获得足够时间处理 BLE 事件。
- 深度保留唤醒 (`user_init_deepRetn`) 目前空缺，实际工程应补齐恢复流程（重新配置 GPIO、打开 ADC/AFE），以满足项目简报中“唤醒后首次采样必须新鲜”的要求。

借助上述机制，可以在 Telink SDK 中实现“未连接时 5 s 采样 + suspend，连接或 OTA 时恢复高频”的策略，同时保证 BLE 时序与 BMS 安全。
