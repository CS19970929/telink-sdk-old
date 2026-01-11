# 工程地图

## vendor 目录定位
- `vendor/b85m_ble_sample/`：本 BMS 工程，包含 AFE/I2C、SOC 估算、KV 存储、Modbus-over-BLE 以及单线 SIF 等全部业务文件（`main.c`、`app.c`、`app_att.c`、`sh367309_datadeal.c`、`SocEnhance.c`、`sif_send.c`、`soc_kv_store.c` 等）。
- `vendor/b85m_ble_remote/`、`vendor/b85m_module/` 等目录仍保留 Telink 官方 HID／模块化示例（可在这些目录的 `main.c` 或 `app.c` 中看到相同的 BLE 初始化骨架），但未被当前 SDK↔工程映射引用，只用于对照。

## b85m_ble_sample 内部模块
- 启动/主循环：`main.c` 提供 `irq_handler` 和 `main()`，所有应用逻辑都在 `app.c` 的 `user_init_*()` 与 `main_loop()` 中实现。
- BLE 协议：`app_att.c` 构建 GAP/GATT/HID/SPP 属性表并通过 `module_onReceiveData()` 处理 BLE 写；`app_config.h` 汇总板级、GPIO 与调试开关。
- AFE/I2C：`sh367309_datadeal.c/h` 定义 `ram_reg_309`、`App_AFEGet()`、`MTPWrite()` 等 SH367309 驱动与故障检测，并维护 `g_stCellInfoReport`。
- SOC 估算：`SocEnhance.c/h` 暴露 `soc_param_lib_init()`、`APP_SOC_IntEnhance_Ctrl()` 等函数，组合 AFE 采样、充放电逻辑与校准状态。
- 参数与存储：`param.c` 的 `LoadParam()/SaveParam()` 读取 Flash/EEPROM；`soc_kv_store.c` 提供磨损均衡 KV（`soc_kv_store_init()`、`soc_kv_store_update_and_log_if_changed()`）。
- 外设接口：`sif_send.c/h` 构建单线 SIF（定时输出）报文，`sci_upper.h` 定义上报数据结构；`button.c` 等文件保留原 sample 的输入逻辑。

## AFE→保护→MOS→上报→存储链路
1. **采样 / 预处理**：`main_loop()` 每 200 ms 调用 `App_AFEGet()`（`vendor/b85m_ble_sample/app.c:1725`），该函数在 `sh367309_datadeal.c:1472-1495` 通过 `i2c_read_series()` 把 SH367309 RAM 读入 `ram_reg_309`，随后依次执行 `UpdateVoltageFromBqMaximo()`、`DataLoad_CellVolt()`、`DataLoad_Temperature()`、`DataLoad_Current()` 等函数，把各项数据写入 `g_stCellInfoReport`。
2. **保护判断与状态**：`sh367309_datadeal.c:1212-1466` 维护 `FaultWarnRecord2()`、`Monitor_TempBreak()` 以及多段 switch，用 `ram_reg_309` 的状态位驱动 `g_stCellInfoReport.unMdlFault_*` 和 `SystemStatus`；MOS 控制位写入 `SH367309_Reg_Store.REG_MTP_CONF`，并在 `MTPWrite()`（`sh367309_datadeal.c:244-275`）里通过 I2C 下发。
3. **MOS/继电器控制**：`user_init_normal()` 在初始化阶段调用 `SH367309_Enable_AFE_Wdt_Cadc_Drivers()`、设置 `AFE_CTL_PIN/MCC_C_PIN/CHG_IN_PIN` 等 GPIO；运行期的 `charger_detect_and_keyLogi_200ms()`（`vendor/b85m_ble_sample/app.c:1156-1236`）会根据 `CHG_IN_PIN` 输入切换 `CHGMOS/DSGMOS/CADCON` 位并写 `MTP_CONF`，同时驱动 `MCC_C_PIN` 控制外部继电器。
4. **数据换算 / SOC**：`SocEnhance.c:95-960` 读取 `g_stCellInfoReport`，通过 `APP_SOC_IntEnhance_Ctrl()` 结合 `SOC_Cali_Flag` 进入 `SOC_Cont_AH_Int_CHG/DSG`，以及 `soc_cali()` 修正 0/100% 状态；计算中间结果 `SOC_Calculate_Element` 最终同步到 `g_stCellInfoReport.SocElement`。
5. **上报链路**：
   - **BLE**：`main_loop()` 在检测到 `rev_master=true` 时，基于 `addr` 选择 `notify_votage()/notify_protect_prarm()/notify_soc()/notify_protect_status()`（`vendor/b85m_ble_sample/app.c:1639-1688`）生成 Modbus 映射数据，最终通过 `notify_big_packet()` → `blc_gatt_pushHandleValueNotify()` 上行。
   - **SIF 单线**：Timer0 IRQ 中的 `sif_send_data_handle()`（`vendor/b85m_ble_sample/app.c:704-742` 调用 `sif_send_data_handle()`），进一步调用 `sif_send_PRIVATE_PACKETS_REALTIME_INFO()`、`sif_send_PRIVATE_PACKETS_CELLVOLTAGE()` 等函数（`vendor/b85m_ble_sample/sif_send.c:817-935`）读取 `g_stCellInfoReport`、`SOC_Calculate_Element` 拼帧，并用 `gpio_write(OWC_TX_PIN, ...)` 发送。
6. **存储**：
   - 参数加载：`user_init_normal()` 在 I2C 初始化后调用 `LoadParam()`（`vendor/b85m_ble_sample/app.c:1085`），`LoadParam()` 通过 `flash_read_page()`/`flash_write_page()`（`vendor/b85m_ble_sample/param.c:34-83` 与 `drivers/8258/flash.c:1-120`）恢复保护参数；若版本不符则写入默认结构。
   - SOC KV：`user_init_normal()` 调用 `soc_kv_store_init()` 与 `soc_param_lib_init()`（`vendor/b85m_ble_sample/app.c:1146-1174`、`SocEnhance.c:137-175`），`main_loop()` 结尾的 `soc_kv_store_update_and_log_if_changed()`（`vendor/b85m_ble_sample/app.c:1799`）会根据 SOC/DSG/Cycle 的变化调用 `soc_kv_store_put()`，后者在 `soc_kv_store.c:208-274` 里使用 Flash API（`flash_erase_sector_safe()/flash_write_page()`）实现 A/B 区翻转与掉电安全写入。

## 中断 / 主循环共享变量
- `g_stCellInfoReport`（`vendor/b85m_ble_sample/app.c:384` 定义）：主循环在 `App_AFEGet()`、`adc_app_process_200ms()` 里频繁写入；Timer0 中断里的 `sif_send_PRIVATE_PACKETS_REALTIME_INFO()`、`sif_send_PRIVATE_PACKETS_CELLVOLTAGE()`（`vendor/b85m_ble_sample/sif_send.c:817-935`）在不同上下文读取该结构，没有 `volatile` 或锁保护。
- `rev_master` / `addr`（`vendor/b85m_ble_sample/app_att.c:420-452`）：由 BLE ATT 写回调 `module_onReceiveData()`（运行在 LINK Layer 事件/中断上下文）设置，在 `main_loop()` 中读取后决定哪一个 notify 函数执行；变量本身未加 `volatile`，依赖 Telink 栈串行化。
- `SystemStatus`（`vendor/b85m_ble_sample/sh367309_datadeal.c:12`）在 `App_AFEGet()` 内写入，但 `sif_send_PRIVATE_PACKETS_REALTIME_INFO()`、`notify_protect_status()`（`vendor/b85m_ble_sample/app.c:1473-1638`）会在 BLE/SIF 上报线程读取，若未来在中断中扩展 SIF 接收同样会触发竞态，需要集中保护。
