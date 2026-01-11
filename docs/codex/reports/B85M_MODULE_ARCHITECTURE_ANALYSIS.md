# b85m_module 模板工程架构分析

## A. 工程结构与入口流程
1. **目录职责**（`vendor/b85m_module/`）
   - `main.c`：SoC 启动入口与 `irq_handler()`，仅保留时钟/唤醒/射频等底层初始化，并把所有应用逻辑委托到 `app.c`。
   - `app.c/app.h`：核心框架，包含 BLE 初始化、主循环、功耗管理、SPP UART glue、GPIO 唤醒/忙碌判定等，等同于“Module 层”的调度中心。
   - `app_att.c/.h`：GAP/GATT/ATT 定义、属性表、SMP 设置，是 BLE Host 层 glue。
   - `spp.c/.h`：Telink Module Demo 的“业务模块”，封装了 controller event 处理、SPP 命令解析、UART/HCI 转发、restart 指令等。
   - `battery_check.c/.h`：低电压检测模块，提供 ADC 初始化、阈值判断、与 PM 的联动。
   - `app_config.h`：宏配置，总开关（PM、SMP、UART、LOG 等）。

2. **启动流程**（`main.c:main`）：
   ```text
   reset → main()
     → cpu_wakeup_init / rf_drv_init / gpio_init / clock_init
     → 判断 pm_is_MCU_deepRetentionWakeup()
        ├─ deepRetn: user_init_deepRetn()
        └─ normal : user_init_normal()
     → irq_enable()
     → while(1): wd_clear(); main_loop()
   ```
   - `user_init_normal()`（`app.c:375-540`）执行 battery check、MAC 读取、`blc_ll_initBasicMCU()` 等 BLE 控制器初始化、`my_att_init()` 建 ATT 表、SMP/BLE 事件回调注册、SPP UART 初始化、HCI 回调登记等，是官方范式的“初始化顺序脚本”。
   - 深度保留唤醒时的 `user_init_deepRetn()`（`app.c:563-605`）只做 `blc_ll_initBasicMCU()` + `blc_ll_recoverDeepRetention()` + `rf_set_power_level_index()`，保持 BLE 连接状态。

3. **主循环模型**（`app.c:626-655`）：
   - 首句固定 `blt_sdk_main_loop()` 驱动 BLE 协议栈。
   - `app_power_management()` 检查 module/mcu 是否忙（`app_module_busy()` 根据 `GPIO_WAKEUP_MODULE` + UART FIFO 判定），决定是否允许 suspend/deep retention，并设置 `GPIO_WAKEUP_MCU` 等信号。
   - `spp_restart_proc()` 用于处理“模块重启”命令（`spp.c:543-563`），即在 main loop 的末尾检查 SPP 命令标志。
   - 额外任务通过 `#if` 宏（如 `BATT_CHECK_ENABLE`）插入，整体属于“事件 + 轮询混合”模型：BLE 事件在 IRQ/hci handler 中处理，模块级事务在 main loop 轮询。

## B. Module 设计范式
1. **模块文件组织**：以 SPP 为例，`spp.c/.h` 包含：HCI/controller event handler、UART DMA 回调、模块重启命令、悬挂标志等；`battery_check.c/.h` 则提供独立 API (`app_battery_power_check`, `battery_set_detect_enable`) 并通过宏控制编译。这体现“一个功能一个文件组”的范式。
2. **生命周期**：
   - `user_init_normal()` 中一次性调用所有模块的 init（，例如 `bls_l2cap_register_handler`、`battery_check` 依赖 `BATT_CHECK_ENABLE`）。
   - main loop 中模块 run 函数（如 `app_power_management()`、`spp_restart_proc()`、`battery_check` 的 `clock_time_exceed`）被统一调用；模块内部再细分为“状态标志+回调”。
   - 与 BLE 事件的关系：`spp.c:controller_event_handler()`通过 `blc_hci_registerControllerEventHandler()` 在 BLE 中断上下文感知 connect/terminate/conn param update/ suspend exit 等事件，然后触发 UART/HCI 发送或唤醒逻辑。
3. **解耦手段**：
   - 通过 `extern` 的状态变量（如 `module_uart_data_flg`, `module_wakeup_module_tick`）与 `GPIO_WAKEUP_MODULE` 等硬件信号交互；模块间没有复杂的事件总线，但所有共享变量都集中在 `app.c`。
   - API 约定：模块提供 `*_init`、`*_proc`（run）、`*_callback`（事件）三类函数，并通过 `app_config.h` 宏控制是否编译，便于裁剪。
   - 这种结构简单、可读，但在 BMS 这种多外设场景下需谨慎管理全局变量与硬件状态。

## C. BLE 使用方式
1. **初始化**：`user_init_normal()` 明确分两段：先 BLE Controller（`blc_ll_*`）再 Host/GATT（`blc_gap_peripheral_init()`、`my_att_init()`、SMP），并紧接着配置广播数据。这种流程可直接复用到 BMS。
2. **事件处理**：
   - `controller_event_handler()`（`spp.c:30-210`）处理 connect/terminate/conn param update/suspend 等事件，并通过 `spp_send_data()` 把事件上报给主机，形成“模块内部事件 → HCI/UART”的链路。
   - `app_host_event_callback()`（`spp.c:243-340`）监听 GAP/SMP 事件，用于打印日志或触发业务逻辑。
   - 应用态（main loop）只需监控 `module_uart_data_flg` 等标志，不直接碰 BLE 寄存器，体现“BLE 事件集中处理”的思想。
3. **与模块的关系**：
   - BLE 事件始终在中断上下文（`irq_blt_sdk_handler`）或 Telink 提供的回调中处理，模块业务在 main loop 或 UART DMA 回调中执行；文中多处注释强调“不要在 BLE 关键窗口里做耗时操作”。
   - SPP 模块利用 `blc_register_hci_handler()` 将 UART DMA 数据接入 BLE HCI 层，示范了如何建立 Module ↔ BLE ↔ Host 的抽象接口。

## D. 低功耗与电源管理
1. `app_power_management()`（`app.c:299-349`）是模块的 PM 中枢：
   - 通过 `app_module_busy()` 检查 UART FIFO + GPIO，决定是否允许 suspend。
   - 根据 `BLE_MODULE_PM_ENABLE` 配置 `bls_pm_setSuspendMask()`，并设置 `PM_WAKEUP_PAD` 以保证外部唤醒。
   - `app_suspend_enter()` / `app_suspend_exit()` 将 module 的唤醒引脚拉高/拉低，与主机配合。
2. battery_check 与 PM：
   - 低电压检测（`battery_check.c`）在 main loop 中以 `clock_time_exceed()` 方式运行，并在 `app_suspend_enter_low_battery()` 中决定是否允许进入深睡。
3. 时序敏感操作：
   - UART DMA/Flash/SMP 初始化都被安排在 `user_init_normal()` 电池检测之后，Telink 注释强调“battery check must do before flash write/erase”，值得借鉴。

## E. BMS 项目的继承 vs 避坑
1. **值得继承**：
   - **初始化顺序脚本**：`user_init_normal()` 的结构清晰，按照 battery check → BLE controller → GATT → SMP → 应用模块 → UART 的顺序处理，适合直接移植。
   - **事件集中处理**：`controller_event_handler()`/`app_host_event_callback()` 集中管理 BLE 事件，避免在各模块散落处理，是 BMS 应保持的模式。
   - **功耗投票机制**：`app_module_busy()` 通过 GPIO + FIFO 检查决定是否允许 suspend，适合扩展成“AFE/I2C/Flash 的 busy 投票”。

2. **需要谨慎的点**：
   - 大量 `extern` 全局变量、GPIO 信号耦合紧密，如 `module_uart_data_flg`、`GPIO_WAKEUP_MODULE`。在 BMS 多任务场景中需封装为接口，避免直接操作裸数据。
   - SPP 示例没有任何超时/健康度机制（UART busy 或 HCI 错误只靠标志位），BMS 必须加上超时/重试。
   - Flash/HCI 操作在回调中直接调用，需确保在 BLE 关键窗口不做耗时任务。

3. **不适合直接照搬的做法**：
   - SPP demo 假设上位机永远可信，未对命令做校验；BMS 的 BLE 命令应有 CRC/白名单。
   - battery_check 只判断阈值并阻止进入 sleep，没有进一步的安全策略；BMS 需引入“低电压降级”。
   - 模块重启（`spp_restart_proc`）直接调用 `cpu_sleep_wakeup`，BMS 应慎用，以免破坏 AFE 状态。

## F. 对 BMS 的具体建议
1. **可迁移思想**：
   - 维护一个 `bms_module_busy()`，仿照 `app_module_busy()`，综合 I2C/AFe 采样/Flash 写入的 busy 标志，统一供 PM 判断。
   - 定义 `bms_controller_event_handler()`，集中处理 connect/disconnect/OTA/SMP 事件，并通过日志接口上报。
   - `user_init_normal()` 可以按 b85m_module 的顺序重写，使 BLE/电源/应用初始化更加可控。

2. **需要 BMS 化改造**：
   - 在 module 层引入状态机和健康度管理，例如 I2C 封装（Task16）可以作为“AFE 模块”的 run 函数；`bms_storage` 提供日志记录。
   - 低功耗策略要考虑 AFE/I2C 的唤醒恢复，避免像 demo 那样只依赖 GPIO。
   - 对于外设操作（ADC、I2C、Flash）必须加超时/重试，不能像 demo 一样 busy-wait。

3. **未来重构建议**：
   - 若 BMS 需要大量定制（AFE/I2C/存储/安全），建议以 `b85m_ble_sample` 为基础继续演进，因为其结构已经围绕 BMS 业务改造。
   - 但可以抽取 `b85m_module` 的骨架（init 顺序、事件集中处理、PM 投票）作为“地基”，并把 BMS 业务模块按照 SPP/Battery 的方式拆分为 `afe.c`、`storage.c`、`comm.c` 等，避免 app.c 过度膨胀。

## 设计哲学总结
- b85m_module 的核心理念是：**“BLE 协议栈集中管理 + 模块化外围功能 + 明确的生命期管理”**。所有事件都通过官方 API 回调，主循环只做轻量轮询；功耗通过统一的 busy 检查控制；与主机的交互（SPP/UART）通过 DMA 和 HCI glue 层解耦。这种结构是 Telink 推荐的“模块级 SDK 使用范式”。

## BMS 落地建议总结
- 继承模块化思想：将 AFE/I2C、SOC 估算、存储、通信等拆分成独立模块，并在 `user_init_normal()`/`main_loop()` 中统一调度。
- 引入安全机制：在每个模块加上超时、重试、健康度与日志，利用 Task16/17/18 的封装确保可靠性。
- 统一事件与功耗管理：仿照 `controller_event_handler()` 与 `app_power_management()`，构建 BMS 专用的 BLE 事件中心和 PM 投票机制，确保在连接态也能安全地协调 I2C/Flash 操作。
