# Telink SDK BLE 核心机制（基于 vendor/b85m_ble_sample）

## 1. 初始化/配置调用链
1. **入口**：`vendor/b85m_ble_sample/main.c:main` 完成 SoC 时钟、GPIO、射频等底层初始化后，根据 `pm_is_MCU_deepRetentionWakeup()` 选择 `user_init_deepRetn()` 或 `user_init_normal()` 并进入 `main_loop()`。
2. **BLE 控制器初始化**：`vendor/b85m_ble_sample/app.c:939-1040 user_init_normal` 依次调用 `blc_ll_initBasicMCU()`、`blc_ll_initAdvertising_module()`、`blc_ll_initConnection_module()`、`blc_ll_initSlaveRole_module()`（均在 `stack/ble/controller/ll/ll.h` 声明），确保 Standby/Adv/Conn 角色就绪。
3. **Host/GAP/GATT 配置**：同一函数中调用 `blc_gap_peripheral_init()`、`my_att_init()`（`vendor/b85m_ble_sample/app_att.c:602`，内部执行 `bls_att_setAttributeTable()`）以及 `blc_l2cap_register_handler(blc_l2cap_packet_receive)`，把属性表和 L2CAP 处理链注册到协议栈。
4. **广播参数**：`user_init_normal()` 设置 Adv/ScanRsp 数据（`bls_ll_setAdvData()`、`bls_ll_setScanRspData()`）并调用 `bls_ll_setAdvParam()`、`bls_ll_setAdvEnable(1)` 启动广播；如果曾经绑定，则调用 `bls_smp_param_loadByIndex()` 并切换到 directed adv。
5. **事件钩子**：`bls_app_registerEventCallback()` 把 `task_connect()`、`task_terminate()`、`ble_remote_set_sleep_wakeup()`、`user_set_rf_power()` 等函数绑定到 `BLT_EV_FLAG_*`，后续由 `irq_blt_sdk_handler()` 调度。

> **应用层插入点**：按照示例，将所有自定义模块的初始化（I2C/GPIO/任务调度器等）放在 `user_init_normal()` 尾部，确保在 `irq_enable()` 之前完成。

## 2. 广播→连接→断开事件链
1. **广播启动**：`user_init_normal()` 内 `bls_ll_setAdvEnable(1)` 之后即进入广播态。
2. **连接回调**：`vendor/b85m_ble_sample/app.c:689-741 task_connect` 在 `BLT_EV_FLAG_CONNECT` 触发后执行，调用 `bls_l2cap_requestConnParamUpdate(CONN_INTERVAL_10MS, ...)` 调整连接参数，并设置 `device_in_connection_state=1` 与 `interval_update_tick`，供主循环判断上报节奏。
3. **断开回调**：`vendor/b85m_ble_sample/app.c:640-688 task_terminate` 在 `BLT_EV_FLAG_TERMINATE` 事件中重置连接状态并记录 `advertise_begin_tick`，确保 `blt_pm_proc()` 能根据 `advertise_begin_tick` 控制低功耗/重新广播。
4. **广告时长切换**：`app_switch_to_indirect_adv()`（`vendor/b85m_ble_sample/app.c:666-687`）在 diret adv 结束后重新配置 undirected adv，避免 iOS/Android 长时间连不上。

## 3. GATT/ATT 服务与 Handle 布局
1. **属性表**：`vendor/b85m_ble_sample/app_att.c:426-606 my_Attributes` 定义 GAP/GATT、HID、Battery、SPP 服务等，Telink SPP 的两条特征数组（`TelinkSppDataServer2ClientCharVal`、`TelinkSppDataClient2ServerCharVal`）在同文件 60-90 行定义权限与 UUID。
2. **自定义服务**：SPP 特征的 value 节点附带 `att_readwrite_callback_t`，其中 `SppDataServer2ClientData` 绑定 `module_onReceiveData()`（`vendor/b85m_ble_sample/app_att.c:428`），用于接收手机写入；`SppDataClient2ServerDataCCC` 提供 notify 订阅。
3. **Handle/UUID**：`app_att.h` 定义各 Handle（如 `SPP_CLIENT_TO_SERVER_DP_H`），应用层在 `notify_big_packet()` 及 `blc_gatt_pushHandleValueNotify()` 中引用这些常量，实现“逻辑名→ATT Handle”的映射。

## 4. 手机 Write（含 Write Without Response）的到达路径
1. L2CAP 层：`vendor/b85m_ble_sample/app.c:970 user_init_normal` 调用 `blc_l2cap_register_handler(blc_l2cap_packet_receive)`，Telink SDK 内部在 L2CAP 收到数据时解析 ATT PDU 并匹配到属性表回调。
2. 属性回调：当手机 Write 到 `SPP_CLIENT_TO_SERVER_DP_H` 时，`module_onReceiveData()`（`vendor/b85m_ble_sample/app_att.c:428-458`）被调用，解析 `rf_packet_att_write_t` 中的 `value` 并提取 Modbus 地址。为避免在 LinkLayer IRQ 上执行重任务，函数只设置 `rev_master=true`、记录 `addr` 并简单调用 `MODS_Poll()`。
3. 主循环处理：`vendor/b85m_ble_sample/app.c:1757-1793 main_loop` 检查 `device_in_connection_state && rev_master` 后根据 `addr` 调用 `notify_votage()/notify_protect_prarm()/notify_soc()/notify_protect_status()`。这种“回调只置标志→主循环消费”的模式保证了 BLE IRQ 不被阻塞。

## 5. Notify/Indicate 发送机制
1. **封装函数**：`vendor/b85m_ble_sample/app.c:1320-1355 notify_big_packet` 负责将任何长度的 payload 切分为 20 B（默认 MTU=23）的块，并在 while 循环中调用 `blc_gatt_pushHandleValueNotify()`（定义见 `stack/ble/host/attr/gatt.h:62`）。发送失败会立即返回并输出日志。
2. **调用上下文**：所有 `notify_xxx`（电压、保护参数、SOC 等）均在 `main_loop()` 的非中断上下文调用 `notify_big_packet()`；Timer0 中断仅维护单线 UART，与 BLE Notify 严格隔离，避免在 IRQ 中直接发包。
3. **长度/MTU 注意事项**：若启用 DLE/大 MTU，可在 `notify_big_packet()` 中根据 `blc_gatt_getCurrentMTUSize()` 动态调整 chunk，但示例代码固定为 20 B，提醒用户在高吞吐场景自行扩展。

## 6. 常见坑与工程建议
| 坑点 | 代码证据 | 建议 |
| --- | --- | --- |
| 阻塞主循环导致 BLE 事件延迟 | `main_loop()` 首句必须执行 `blt_sdk_main_loop()`（`vendor/b85m_ble_sample/app.c:1708`） | 所有重任务都放在 `blt_sdk_main_loop()` 之后并考虑分片，确保主循环快速返回 |
| 在 IRQ 中发 notify | Timer0 IRQ 只处理 `sif_send_data_handle()`（`vendor/b85m_ble_sample/app.c:704-742`） | BLE Notify 必须在主循环或 BLE 事件回调中调用 `notify_big_packet()`，否则可能与 `irq_blt_sdk_handler()` 竞争 |
| Write 回调中执行耗时任务 | `module_onReceiveData()` 运行在 ATT 回调上下文 | 仅解析数据并设置标志，把真实处理放到主循环，避免阻塞 L2CAP RX |
| 连接参数导致吞吐不足 | `task_connect()` 请求 10 ms interval/latency 0 | 若需要更高吞吐，可在此函数中根据应用动态调整 interval/latency，并确保对端支持 |
| iOS 后台/重连 | `task_terminate()` 断开后重置 `advertise_begin_tick` 并重新进入广播 | 在应用中监测断开原因并适时调整广告策略（如 `app_switch_to_indirect_adv()`），保证手机后台也能重新连接 |
| GATT 表修改引发 Handle 变化 | `my_Attributes` 顺序决定 Handle 值 | 修改服务/特征时务必同步 `app_att.h` 中的 Handle 枚举以及上位机对照表 |

通过以上链路梳理，可以快速理解 Telink SDK 中 BLE 的“初始化 → 广播 → 事件回调 → GATT 读写 → notify”全流程，并据此在工程中安全地插入业务逻辑、扩展服务或优化吞吐。
