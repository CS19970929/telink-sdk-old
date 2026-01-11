# BMS BLE 协议规格（基于 vendor/b85m_ble_sample）

## 1. 服务/特征/Handle 总览
| 服务 | UUID | 特征 | Handle（`app_att.h`） | 属性 | 说明 |
| --- | --- | --- | --- | --- | --- |
| Telink SPP | `WRAPPING_BRACES(TELINK_SPP_UUID_SERVICE)` | `TELINK_SPP_DATA_CLIENT2SERVER` | `SPP_CLIENT_TO_SERVER_DP_H` | Read + Notify | **设备→手机**：所有 notify（电压/保护/SOC/状态）通过此 handle 发送 |
| Telink SPP | 同上 | `TELINK_SPP_DATA_SERVER2CLIENT` | `SPP_SERVER_TO_CLIENT_DP_H` | Read + WriteWithoutRsp + Write | **手机→设备**：Modbus 风格命令写入，回调 `module_onReceiveData()` |
| Battery Service | 0x180F | `CHARACTERISTIC_UUID_BATTERY_LEVEL` | `BATT_LEVEL_INPUT_DP_H` | Read + Notify | 可上报概览电量（`update_my_batVal()`） |
| OTA | `my_OtaServiceUUID` | `my_OtaUUID` | `OTA_CMD_OUT_DP_H` 等 | Write/Notify | Telink OTA 通道，流程遵循官方文档 |

> 其余 GAP/GATT/HID 服务保留官方模板，不影响 BMS 协议核心路径。

## 2. 手机 Write → 设备解析
1. 手机对 `SPP_SERVER_TO_CLIENT_DP_H` 执行 Write/Write Without Response；Telink Host 将帧送至 `vendor/b85m_ble_sample/app_att.c:module_onReceiveData`。
2. `module_onReceiveData()` 解析 `rf_packet_att_write_t`：
   ```c
   u8 len = p->l2capLen - 3;
   u8 slave = data[0];
   u8 cmd   = data[1];
   u16 addr = (data[2] << 8) | data[3];
   rev_master = true;
   ```
   - 默认第一字节为设备地址（0x01），第二字节为功能码（0x03）。
   - 第 3/4 字节为寄存器地址，决定主循环要回复的内容。
3. 主循环（`vendor/b85m_ble_sample/app.c:1757-1784 main_loop`）在检测到 `device_in_connection_state && rev_master` 后，根据 `addr` 调用不同的 notify：
   - `0xD000` → `notify_votage()`：单体电压/总压/最大最小值。
   - `0x2100` → `notify_protect_prarm()`：保护参数表。
   - `0xD026` → `notify_soc()`：温度、SOC、循环数据。
   - `0xD115` → `notify_other_status()`：`SystemStatus` 位图等。
   - `0xD100` → `notify_protect_status()`：最近保护记录。

## 3. 设备 Notify → 手机解析
### 帧格式（所有 notify）
```
[0] 设备地址(0x01)
[1] 功能码(0x03)
[2] 字节数 = N*2
[3..] 大端寄存器数据
[末-2] CRC16(低字节先)
```
- CRC 计算：`Sci_CRC16RTU()`（`vendor/b85m_ble_sample/app.c:1265-1298`），与 Modbus RTU 相同。
- 分包：`notify_big_packet()`（`app.c:1320-1355`）自动将 `test_buf` 拆成 20 B 块，依次通过 `SPP_CLIENT_TO_SERVER_DP_H` 发送。

### 主要寄存器定义
| 地址 | 函数 | 寄存器布局 | 数据来源 |
| --- | --- | --- | --- |
| `0xD000` | `notify_votage()`（`app.c:1618-1688`） | 38×16bit：前 14 条为 16S 电压，其余为 `VCellMax/Min/位置/Delta/总压` | `g_stCellInfoReport.u16VCell[]` 等（`sh367309_datadeal.c` 填充） |
| `0x2100` | `notify_protect_prarm()`（`app.c:1588-1616`） | 65×16bit：保护阈值、延时等 | 参数表 `g_tParam` |
| `0xD026` | `notify_soc()`（`app.c:1558-1586`） | 25×16bit：温度/NTC/SOC 数组 | `g_stCellInfoReport.u16Temperature[]`、`SOC_Calculate_Element` |
| `0xD115` | `notify_other_status()`（`app.c:1446-1484`） | 12×16bit：`SystemStatus` 分段及占位字段 | `SystemStatus.all` |
| `0xD100` | `notify_protect_status()`（`app.c:1490-1556`） | 21×16bit：Fault 缓存、错误标志等 | `Fault_record_*`、`System_ErrFlag` |

## 4. 收发流程示例
```
手机                                设备
----- Write(SPP_SERVER_TO_CLIENT) --> module_onReceiveData()
                                      rev_master=true, addr=0xD000
                                      main_loop() 检查 rev_master
                                      notify_votage() 组帧
<---- Notify(SPP_CLIENT_TO_SERVER) --- notify_big_packet() 分包发送

手机收到 notify → 校验 CRC → 根据寄存器地址解析 payload
```

## 5. 协议要点与约束
- **MTU / 分包**：默认 20 B payload；若上位机协商大 MTU，需同步调整 `TELINK_NOTIFY_PAYLOAD`（目前写死 20）。
- **CRC 必须校验**：设备不会对上行请求做 CRC 检查（`module_onReceiveData()` 仅置标志），因此 App 需保证请求帧正确；下行响应均包含 Modbus CRC，可用于判定数据完整性。
- **请求节流**：主循环一次只处理一个 `rev_master`，若连续发送多条命令，后发会覆盖先发。上位机需等待相应 `notify` 后再发下一条，或在 `notify_big_packet()` 返回后设置 ack。
- **连接参数**：`task_connect()`（`app.c:702-734`）请求 10 ms Interval、Latency 0；若需要更高吞吐可在 App 侧协商更大 MTU、并允许短期 Latency=0 保证响应实时性。

## 6. 对调试工具的建议
- 订阅 Handle：连接后写 `SPP_CLIENT_TO_SERVER_DATA_CCC`（`app_att.c` 中的 CCC handle）值为 `0x0001`，否则收不到 notify。
- 控制流程：
 1. 连接成功 → 延迟 100 ms 等待设备注册回调。
 2. 发送 Modbus 帧 `[0x01,0x03,addr_hi,addr_lo,len_hi,len_lo,crc_lo,crc_hi]` 至 `SPP_SERVER_TO_CLIENT_DP_H`。
 3. 解析 `SPP_CLIENT_TO_SERVER_DP_H` 的 notify，按寄存器表解码数据。
- iOS/Android 均建议使用 Write Without Response 发送指令，并在应用层保持 20–30 ms 的节奏，防止 `module_onReceiveData()` 被频繁覆盖。

该规格可直接驱动后续 Bleak/macOS 调试工具与移动 App，确保在 Telink SPP 服务上实现稳定的 Modbus 风格通信。
