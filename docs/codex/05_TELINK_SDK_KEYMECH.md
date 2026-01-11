# Telink SDK 关键机制梳理

## BLE 事件回调链
- `vendor/b85m_ble_sample/app.c:939-1108 user_init_normal` 在 Host 初始化阶段完成 `blc_gap_peripheral_init()`、`my_att_init()` 并调用 `blc_l2cap_register_handler(blc_l2cap_packet_receive)`；`blc_l2cap_packet_receive()` 的入口在 `stack/ble/host/l2cap/l2cap.h:100/109`，负责把 ATT 写请求转发到属性表回调。
- `vendor/b85m_ble_sample/app_att.c:562-580` 将 Telink SPP 特征的写回调指向 `module_onReceiveData()`，因此 BLE 写/Modbus 请求会在 `module_onReceiveData()`（`vendor/b85m_ble_sample/app_att.c:428-458`）内解析后，将寄存器地址写入 `addr` 并置位 `rev_master`；这些变量随后被 `main_loop()` 读取，形成 “Link Layer 事件 → 属性回调 → 工程逻辑” 的链式触发。
- 控制面事件通过 `bls_app_registerEventCallback()` 注册：`vendor/b85m_ble_sample/app.c:1033-1050` 把 `task_connect()`、`task_terminate()`、`ble_remote_set_sleep_wakeup()`、`user_set_rf_power()` 等函数分别绑定到 `BLT_EV_FLAG_CONNECT/TERMINATE/SUSPEND_ENTER/SUSPEND_EXIT`，Telink 栈会在 `irq_blt_sdk_handler()` 中触发这些回调，工程在其中调整连接参数和唤醒源。

## Notify 发送与 MTU 约束
- 所有上行数据都通过 `notify_big_packet()`（`vendor/b85m_ble_sample/app.c:1320-1355`）统一发送。该函数内部把 `len` 拆成 `TELINK_NOTIFY_PAYLOAD=20` 字节的片段，对每段调用 `blc_gatt_pushHandleValueNotify()`（声明见 `stack/ble/host/attr/gatt.h:62`）；若 `BLE_SUCCESS` 之外的错误出现，会立即退出并打印 offset，避免在 LL IRQ 背景下无限循环。
- `notify_big_packet()` 在主循环上下文调用（`main_loop()` 的 rev_master 分支以及 `notify_protect_status()` 等函数），避免了在硬件中断内直接访问 `blc_gatt_pushHandleValueNotify()` 的限制，同时保证遵循 ATT MTU(23) 的默认 20 B payload 要求；若要扩展到 DLE，需要在此处同步 Telink Host 设置以免分片错误。

## PM / Suspend 控制
- `vendor/b85m_ble_sample/app.c:1033-1070` 中的 `user_init_normal()` 通过 `blc_ll_initPowerManagement_module()` 与 `bls_pm_setSuspendMask()`（API 见 `stack/ble/controller/ll/ll_pm.h:87-100`）使能 ADV/CONN suspend，并根据 `PM_DEEPSLEEP_RETENTION_ENABLE` 调整 SRAM 保留、Early Wakeup tick。
- `ble_remote_set_sleep_wakeup()`（`vendor/b85m_ble_sample/app.c:640-666`）在 `BLT_EV_FLAG_SUSPEND_ENTER` 事件里查询 `bls_pm_getSystemWakeupTick()` 与当前 `clock_time()`（`drivers/8258/timer.h:97-125` 提供系统 tick）之间的差值，若大于 80 ms 就启用 `PM_WAKEUP_PAD`，保证 GPIO 可唤醒。
- `blt_pm_proc()`（`vendor/b85m_ble_sample/app.c:710-882`）围绕充电检测 `CHG_IN_PIN`、物理按键状态决定何时调用 `cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0)`；当 `ota_is_working` 被 `app_enter_ota_mode()` 置位后，`bls_pm_setSuspendMask(SUSPEND_DISABLE)` 与 `bls_pm_setManualLatency(0)` 会阻止 suspend，确保 OTA 期间 RF 稳定。

## Tick / Timer 机制
- Telink SDK 的系统时基由 `reg_system_tick` 驱动，`clock_time()`/`clock_time_exceed()` 在 `drivers/8258/timer.h:97-140` 提供 16 MHz 计数器接口；工程遍布这些 API，例如 `main_loop()` 的 200 ms 调度（`vendor/b85m_ble_sample/app.c:1714-1753`）与 `blt_pm_proc()` 的 1 s 判定都依赖 `clock_time_exceed()`。
- 任务级定时由 Timer0 完成：`app_timer_test_init()`（`vendor/b85m_ble_sample/app.c:529-567`）配置 `reg_tmr0_capt = 500 * CLOCK_SYS_CLOCK_1US` 并开启 `FLD_IRQ_TMR0_EN`；`irq_handler()`（`vendor/b85m_ble_sample/main.c:56-84`）里先执行 `app_timer_test_irq_proc()`，后执行 `irq_blt_sdk_handler()`，因此 Timer0 IRQ 必须尽量短。
- Timer0 IRQ 中的 `app_timer_test_irq_proc()`（`vendor/b85m_ble_sample/app.c:704-742`）清除 `FLD_TMR_STA_TMR0` 后调用 `sif_send_data_handle()`，用固定 tick 驱动单线报文状态机；若系统进入 suspend，Timer0 会被 `bls_pm_setSuspendMask()` 控制，需要在唤醒后重新 `reg_tmr0_tick=0` 保证节拍连续。

## UART / 串行机制
- SDK 自带的 UART DMA/N(D)MA 初始化流程可在 `vendor/b85m_module/app.c:485-509` 看到：`uart_recbuff_init()`、`uart_gpio_set()`、`uart_reset()`、`uart_init()` 以及 `uart_dma_enable()`；DMA IRQ 通过 `dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1)` 触发，本工程未直接调用这些 API。
- 当前工程把 `UART_PRINT_DEBUG_ENABLE`（`vendor/b85m_ble_sample/app_config.h:76-111`）设为 0，未启用 Telink 的硬件 UART；串行上报完全由 Timer0 + `sif_send.c` 的 GPIO 翻转实现（`sif_send_data_handle()` 与 `gpio_write(OWC_TX_PIN, ...)`，参见 `vendor/b85m_ble_sample/sif_send.c:232-410`）。因此不存在 DMA 溢出/NDMA 超时的 SDK 异常处理，任何 Modbus/日志发送异常都需要在 `sif_send_*` 状态机内自行监测。

## I2C 机制
- 低层 I2C API 来自 `drivers/8258/i2c.c:317-355 i2c_read_series()` 与 `drivers/8258/i2c.c:178-270 i2c_write_series()`，通过操作 `reg_i2c_id/reg_i2c_ctrl` 发起起始、地址与数据阶段；Telink SDK 默认在函数内 busy-wait `FLD_I2C_CMD_BUSY`，如果外设 NACK 会一直停留在 while 循环。
- 工程端在 `i2c_master_test_init()`（`vendor/b85m_ble_sample/app.c:885-918`）里调用 `i2c_gpio_set()` 与 `i2c_master_init()`，把 SDA/SCL 固定到 C0/C1；`App_AFEGet()`（`vendor/b85m_ble_sample/sh367309_datadeal.c:1472-1495`）和 `TwiWrite()/MTPWrite()`（`同文件 226-275`）分别用 `i2c_read_series()` 与 `i2c_write_series()` 完成批量读写，但当前代码未检查 I2C 返回值，只是顺序调用并在注释中提到 “任何异常需触发安全降级”，因此 I2C bus-hang 会直接卡死在驱动 while 循环。
- 读取成功后，`ram_reg_309` 的解包与 `g_stCellInfoReport` 更新发生在 `DataLoad_*` 函数群（`vendor/b85m_ble_sample/sh367309_datadeal.c:938-1208`）；若要实现“通信失败立即降级”，必须在 `App_AFEGet()` 包装 `i2c_read_series()` 并根据 `System_ERROR_UserCallback()`（`同文件 207-224`）设置错误标志。
