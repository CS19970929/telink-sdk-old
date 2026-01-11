# iOS 正常、Android 缺帧问题根因分析与修复建议

## A. 设备端承载审计
1. **BLE ATT 结构**：`vendor/b85m_ble_sample/app_att.c` 定义 SPP 服务。
   - TX（设备→手机）特征：`TelinkSppDataClient2ServerCharVal`，handle `SPP_CLIENT_TO_SERVER_DP_H`，属性 READ | NOTIFY（行 90-110）。CCCD `SPP_CLIENT_TO_SERVER_DATA_CCC`。
   - RX（手机→设备）特征：`TelinkSppDataServer2ClientCharVal`，handle `SPP_SERVER_TO_CLIENT_DP_H`，属性 READ | WRITE_WITHOUT_RSP | WRITE，回调 `module_onReceiveData()`（行 428-460）。
2. **接收回调**：`vendor/b85m_ble_sample/app_att.c:428 module_onReceiveData` 直接读取 `rf_packet_att_write_t`，未做 CRC/长度校验，原逻辑仅把 `addr` 写入全局并设置 `rev_master=true`。Task14 后也只入队命令。**没有 ACK，也未对 Write 类型区分**。
3. **发送路径**：`vendor/b85m_ble_sample/app.c:1320 notify_big_packet()` 将 `test_buf` 切成 20B（写死 `TELINK_NOTIFY_PAYLOAD 20`）并调用 `blc_gatt_pushHandleValueNotify()`。发送速率不受控，在 `main_loop()` 中检测 `rev_master` 后一次性发送所有数据，无间隔、无返回值校验。
4. **协议封包**：上层帧（如 `notify_votage()`）按 Modbus 格式 `[slave][func=0x03][byte_len][data...][CRC16]`（`app.c:1470-1688` 等）。**没有帧序号、也没有粘包处理**，完全依赖 BLE 顺序送达。
5. **发送节流/队列**：无专门队列，`main_loop()` 里 `while (bms_cmd_dequeue)` 直接发送，若上位机连续发命令，设备会紧接着推多帧 Notify。若 notify 返回非 `BLE_SUCCESS`，当前代码仅打印且立即返回（`notify_big_packet` 中 `if (ret != BLE_SUCCESS) return ret;`），无重试。

## B. Android 缺帧的高概率根因
1. **CCCD 订阅差异**：Android 常在 `ConnectGatt` 成功后异步写 CCC，若应用端在 `BLT_EV_FLAG_CONNECT` 后立即发送 notify（BMS 在 `main_loop()` 中一有命令就发），Android 可能尚未订阅 -> 开头几帧看似“缺少”。证据：`module_onReceiveData` 不等待 CCC，`notify_big_packet` 也不检测 `client_character_cfg`。
2. **MTU/分包**：`notify_big_packet` 固定 20B，不会触发 MTU >23 情况，但 Android 常使用 Write Without Response + 大 MTU；若手机写入 payload > MTU-3，设备端 `module_onReceiveData` 没有粘包重组，可能只读取 `p->l2capLen-3`，剩余部分被丢弃。（iOS 默认 Write With Response + 20B，反而安全。）
3. **Write 类型差异**：Android 习惯用 Write Without Response，发送间隔由 App 决定；BMS 侧没有防抖，若 Android App 连续写多条命令，设备 `bms_cmd_enqueue` 仅能缓存 4 条（Task14 设定 `BMS_CMD_QUEUE_SIZE 4`），超过后会丢弃“最旧命令”并覆盖。iOS 默认 Write With Response，会自然节流，不触发队列溢出。
4. **发送过快/无 ACK**：设备端对 `blc_gatt_pushHandleValueNotify` 返回值仅在 `notify_big_packet` 中简单 `return ret`，主循环不重试也不记录失败。Android 端若在高延时/低速设备上收到大量 notify，可能直接丢弃；iOS BLE 栈往往缓存更大，表现正常。
5. **并发问题**：`module_onReceiveData` 在 ATT 回调（LinkLayer IRQ context）里执行 `MODS_Poll`、`bms_cmd_enqueue` 等操作。若改为复杂逻辑，可能阻塞 `irq_blt_sdk_handler`。目前虽然轻量，但 `bms_cmd_enqueue` 可能触发 `memcpy` 等操作，建议保持最小化。
6. **连接参数**：`task_connect`（`app.c:702-734`）把 interval 固定成 10ms latency 0；Android 设备在短 interval 下更容易出现 notify 堵塞，需要结合 MTU/ConnParam 调整。
7. **特征属性/权限**：TX 特征仅 READ|NOTIFY，CCCD 权限默认 RDWR；无额外差异，非主要风险点。

## C. 修复方案
### 1. 不改协议，仅增强承载层
| 改动 | 文件/函数 | 内容 | 风险与验证 |
| --- | --- | --- | --- |
| Send 节流 | `vendor/b85m_ble_sample/app.c:notify_big_packet/main_loop` | 新增发送队列 + 定时器：例如 5ms 间隔执行 `blc_gatt_pushHandleValueNotify`，若返回非 `BLE_SUCCESS` 立即重试或延迟。保持 `while(bms_cmd_dequeue)` 但每次只发送一帧 | 需维护发送状态机；验证 Android 在高速命令下不再缺帧（抓包） |
| CCCD 检查 | `main_loop` 或 `notify_votage` | 在发送前读取 `SppDataServer2ClientDataCCC` 是否订阅，未订阅则等待/提示 | 防止 Android 未订阅时的“伪缺帧” |
| MTU 感知 | 初始化阶段调用 `blc_att_getCurrentMTUSize()`，动态设置 `TELINK_NOTIFY_PAYLOAD=MTU-3` | iOS/Android 都受益；需在 `BLT_EV_FLAG_CONNECT` 后更新 chunk 长度 |
| Write 粘包 | 在 `module_onReceiveData` 中按 `p->l2capLen` 解析，同时加入 `len` 检查，如 `len >= payload_min`，避免 Android 写入超长帧导致截断 | 需评估上层协议支持度 |

### 2. 增强可靠性（推荐）
- **帧序号 + ACK**：在 `test_buf` 头部增加 `seq`（2B），每次 notify 后等待手机回 ACK（Write），未 ACK 重试最多 N 次；Android App 也需升级。
- **命令队列扩容 + overflow 日志**：将 `BMS_CMD_QUEUE_SIZE` 提升到 8~16，并在队列满时记录日志/告警，方便定位 “Android 写太快” 的场景。
- **超时判定**：若 `bms_cmd_queue` 长时间无消费（例如 BLE 未订阅 notify），应清队列并提示用户重新连接。

### 3. 最小补丁清单（建议）
1. `vendor/b85m_ble_sample/app.c`：
   - 在 `main_loop()` 中引入 `notify_state` 状态机，确保每 `X` us 只发送一帧。调整 `notify_big_packet` 支持变量 chunk 长度。
   - 记录 `last_notify_status`，若 `blc_gatt_pushHandleValueNotify` 返回非 `BLE_SUCCESS`，日志/重试。

2. `vendor/b85m_ble_sample/app_att.c`：
   - 在 `module_onReceiveData` 中校验 `len`、根据 `p->type` 判断 Write vs WriteCmd，必要时丢弃超长命令并日志。

3. `vendor/b85m_ble_sample/app_att.h`：
   - 暴露 `SppDataServer2ClientDataCCC` 指针，供 main loop 检查订阅状态。

4. 文档/日志：
   - 在 I2C/存储日志中加入 `tx_seq/rx_seq`、`notify_fail_count`，方便问题复现。

## D. 验证与抓证据
1. **设备端日志**：
   - 通过串口或 `storage_log_push` 输出 `tx_seq`、`rx_seq`、`notify_status`、`MTU`（可通过 `blt_att_getCurrentMTUSize()`）。
   - 记录队列溢出、CCCD 未订阅、notify 重试次数等。
2. **手机抓包**：
   - Android：启用“开发者选项 → 启用蓝牙 HCI snoop log”，用 `BTSnoop` 查看；或使用 nRF Connect 记录 Notification 序列。
   - iOS：Xcode 连接设备，使用 PacketLogger 记录 BLE 抓包。
3. **复现步骤**：
   - 选择固定寄存器（如 0xD000），手机端每 50ms 发读命令 100 次；记录设备 `tx_seq` 连续性，与手机端抓包对比。
   - 切换 iOS/Android 分别执行，确认 Android 是否出现 seq 缺口；验证节流/ACK 后缺口消除。

## 推荐修复路线
1. **第一优先**：实现发送节流 + notify 返回值重试（成本低、收益高），同时检查 CCCD 状态，避免“未订阅就发”。
2. **第二优先**：扩展命令队列 + 写入粘包处理，防止 Android WriteCmd 连续发送导致命令丢失；该项需配合手机端节流策略。
3. **第三优先**：若仍有缺帧，再考虑增加 `seq/ACK` 机制或将协议升级为流控模式（代价较大，但收益可靠）。

综上，iOS 正常而 Android 缺帧的主要原因是 Android 使用 Write Without Response + 不同 MTU/连接参数，设备端没有节流和粘包处理，导致命令与 notify 与 BLE 堆栈拥塞。先在设备侧实现承载层增强，即可显著改善 Android 兼容性。进一步的可靠性机制（seq/ACK）可视需要实施。
