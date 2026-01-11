# BMS 场景下的 BLE 通信主流模式

## 1. 常见 GATT 架构
| 架构 | 描述 | 优点 | 缺点 | 典型场景 |
| --- | --- | --- | --- | --- |
| **自定义服务** | 完全自定 UUID/特征，协议层由厂商设计 | 灵活、可按 BMS 寄存器映射 | 需维护文档/兼容性 | 大部分专有 BMS 调试口 |
| **UART-like（NUS）** | 1 条 Write/WriteWithoutRsp + 1 条 Notify，作为“虚拟串口” | 客户端实现简单，适合 Modbus/TLV | 没有结构化访问，需要额外帧头 | Telink 示例 (`SPP_CLIENT_TO_SERVER_DP_H`/`SPP_SERVER_TO_CLIENT_DP_H`) |
| **标准服务** | 如 Battery Service、Device Information | 兼容第三方调试工具、系统可自动识别 | 可表达的数据有限，扩展性差 | 上线商品化时展示 SoC、电压等摘要 |

**Telink 映射**：`vendor/b85m_ble_sample/app_att.c` 采用“标准 + NUS”混合：Battery/HID 服务提供基础信息，SPP 服务承载 Modbus 风格报文，手机 App 只需维护一套 Write/Notify 协议。

## 2. 数据承载方式
```
手机 (Write/WriteCmd) ---> [SPPCLIENT] ---> 设备解析 Modbus 寄存器
设备 (Notify) --------> [SPPSERVER] ---> 手机接收寄存器内容
```
- **Write vs Write Without Response**：Write 有 ACK，可靠但吞吐低；WriteCmd 无 ACK，吞吐高但需应用层重发。调试工具（例如 Telink sample）默认使用 WriteCmd，适合 Modbus 指令。
- **Notify vs Indicate**：Notify 无需确认，Telink 的 `notify_big_packet()` 默认使用 notify；Indicate 需对端确认，适合关键告警。
- **带宽**：BLE 1M PHY 默认 23 B MTU（20 B payload）。若在 `task_connect()` 中调用 `blc_att_requestMtuSizeExchange()` 并将 `notify_big_packet()` 的分片长度设置为 `MTU-3`，在连接参数 15 ms/Latency 0 情况下可达到 ~6–8 KB/s，足够覆盖 16S BMS 监控数据。

## 3. 协议设计关键点
```
+-----------+-----------+-----------+-----------+
| 0x01 cmd  | LEN (2B)  | Payload   | CRC16     |
+-----------+-----------+-----------+-----------+
```
- **分包**：Write 侧建议将一帧完整 Modbus 请求置于单个 BLE PDU（<= MTU-3），这样设备在 `module_onReceiveData()` 中可以一次解析。若请求超过 MTU，需加入片序号/总长度字段。
- **粘包处理**：Telink 的 L2CAP 会逐帧调用回调，因此只要 Write 端按帧发送即可。若使用 Notify 发送多条帧，应在 payload 加入帧头（如 0x55AA + Len）。
- **超时/重试**：手机应在 X ms 内等待 notify，如超时重新发送 Modbus 读；设备端可设置 `rev_master` 超时清零，防止重复响应。
- **CRC**：示例已实现 `Sci_CRC16RTU()`（`vendor/b85m_ble_sample/app.c:1265`），推荐保留，手机端按 Modbus CRC16 校验。

## 4. 调试工具与工程实践
- **抓包**：
  - iOS 使用 Xcode + PacketLogger；Android 使用 nRF Sniffer 或 Ellisys。
  - Telink dongle 配合 `8258_ble_remote` Demo 也可捕获空口。
- **日志**：在设备侧（Telink）可以通过 `UART_PRINT_DEBUG_ENABLE` 打开串口，或利用单线 SIF 输出关键事件；App 侧需保存每次指令/响应，便于复现。
- **吞吐测试**：
  - 步骤：连接 → 扩 MTU → 发送连续读命令 → 设备持续 Notify 大包 → 记录平均 RTT。
  - 关注点：连接参数、是否出现 `HCI_ERR_CONN_TERM_MIC_FAILURE (0x3D)`，检查 `notify_big_packet()` 返回值。
- **MTU/连接参数**：iOS 13+ 默认 185 MTU，Android 依据设备；Telink slave 需在连接后主动发起 `blc_att_requestMtuSizeExchange()` 并允许 15–30 ms interval。如果需要长连接且超低功耗，可把 Latency 拉到 4–15，并在 `task_connect()` 中根据场景动态配置。

## 5. iOS/Android 差异
| 项目 | iOS | Android |
| --- | --- | --- |
| 后台连接 | 后台限制严格，需申请 Background Modes | 大多数厂商允许后台扫描/连接，但会被系统节流 |
| MTU | 185（通常） | 23~517 取决于手机 | 
| Write 类型 | iOS 强制 Write req + WriteCmd 区分，WriteCmd 发送间隔由系统控制 | 可高速 WriteCmd，但需要处理 `GATT_WRITE_NOT_PERMITTED` 错误 |
| Notify 缓冲 | iOS 若 App 被杀，订阅会被清除；需在重连后再次订阅 CCC | Android 订阅通常保留，但不同厂商存在兼容性问题 |

**建议**：
- 与 iOS 通信时，设备在检测到 `BLT_EV_FLAG_CONNECT` 应等待手机完成 CCC 写入再发送 notify，避免首包丢失。
- Android 端多线程 Write 时需串行化，防止 GATT 层返回 133 错误。

## 6. BMS 最佳实践路线
1. **最小可用协议**（调试优先）：
   - 架构：1×WriteCmd（Modbus 请求）+1×Notify（Modbus 响应）。
   - 每帧格式：`[设备地址][功能码][寄存器][长度][Payload][CRC16]`。
   - 连接参数：Interval 15 ms、Latency 0、Timeout 4 s。
   - 应用：现场调试、OTA、车间配对。
2. **增强版本**（商用品质）：
   - 扩展 MTU 至 ≥185、启用分包序号和 Rolling Counter。
   - 引入“事件通知”特征：上报告警/状态，无需轮询。
   - 实现 Session 密钥/挑战应答，防止任意手机连接。
3. **演进路线**：
   - **阶段1（调试）**：沿用 Telink SPP/NUS 通道；保证 CRC/超时机制，添加基本日志。
   - **阶段2（量产）**：对接标准服务（Battery Service/Device Information）以便 App Store 审核，同时保留私有服务。
   - **阶段3（高可靠）**：实现链路探测（心跳/keepalive）、在 `blt_pm_proc()` 中根据连接状态动态调整采样与通知频率，确保 BLE 与低功耗策略协同。

ASCII 示意（阶段1）：
```
[手机APP]
   |
   | Write Cmd (Modbus帧)
   v
[Telink SLAVE]
   |-- module_onReceiveData() 解析
   |-- main_loop() 生成响应
   '-- notify_big_packet() -> Notify

返回路径：Notify -> 手机解析 -> 展示/存储
```

通过以上实践，工程团队可以将行业主流 BLE 通信模式套用到 Telink SDK，并针对 BMS 的安全/调试需求制定清晰的协议与演进路线。
