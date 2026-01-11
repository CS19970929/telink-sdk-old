# iOS BLE 调试 App（SwiftUI + CoreBluetooth）

## 功能概述
- BLE 扫描：支持名称前缀过滤（默认 BT），列表显示 RSSI。
- 连接/断开、自动重连（可切换）。
- 订阅 `SPP_CLIENT_TO_SERVER` Notify，实时在界面上方输出解析结果。
- 读寄存器：指定地址/数量后发送 Modbus 功能码 0x03 请求。
- 轮询模式：每 5 秒自动读取 0xD000 等数据集。
- 错误提示：通过 SwiftUI Alert 显示蓝牙权限或连接错误。

## 工程结构
- `Package.swift`：Swift Package（iOS 应用产品），最低 iOS 15。
- `Sources/BmsDebugApp/`
  - `BmsDebugApp.swift`：应用入口。
  - `ContentView.swift`：SwiftUI 主界面。
  - `BleViewModel.swift`：CoreBluetooth 状态机，实现扫描/连接/订阅/发包。
  - `ProtocolHelper.swift`：CRC16、封包与 Notify 解析代码，与 docs/codex/07_BMS_BLE_PROTOCOL_SPEC.md 保持一致。

## 使用步骤
1. 打开 Xcode 14+，菜单 `File -> Open...` 选择 `tools/bms_debug_ios/Package.swift`。
2. 选择目标 `BmsDebug`，在 Signing 中填入有效的 Team Identifier。
3. 允许 App 使用蓝牙（首次运行时系统会弹框）。
4. 界面说明：
   - 顶部输入名称前缀并点击“扫描”。
   - 在设备列表中点击“连接”。
   - 勾选“自动重连”即可在信号抖动后自动恢复连接。
   - 在“读寄存器”区域输入十六进制地址/数量，点击“发送读命令”。
   - 列表底部实时显示最近 100 条 Notify（含数据集提示与原始 Hex）。

## 扩展建议
- 增加 CSV 导出：在 `BleViewModel.appendLog` 中写入 `FileManager.default.urls(for:.documentDirectory...)`。
- 支持写寄存器：复用 `ProtocolHelper.buildWriteSingle`（可自行扩展）。
- 轮询数据集可参考 Python 工具中的 `POLL_ADDRS`，将多个地址顺序发送。

## 依赖说明
- 仅依赖系统框架（CoreBluetooth、SwiftUI），无需第三方库。

如需定制 UI 或协议字段，可直接修改 `ContentView` 与 `ProtocolHelper`，其结构与 Python 版调试工具保持一致，便于跨平台维护。
