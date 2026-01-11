# Android BLE 调试 App

## 功能
- 扫描并按名称前缀过滤 BMS 设备。
- 连接/断开、自动订阅 Notify，并在界面底部显示实时 Hex。
- 发送读寄存器命令（功能码0x03），支持自定义地址/数量。
- 轮询开关：每 5 秒发送一次读命令。
- 可选择是否将 Notify 写入沙盒 `files/notify_log.csv`。

## 打开方式
1. 安装 Android Studio Giraffe 以上版本。
2. `File -> Open` 选择 `tools/bms_debug_android`（根目录包含 settings.gradle）。
3. 在 Gradle Sync 完成后，运行 `app` 模块即可。

## 权限
- Android 12+ 需要 `BLUETOOTH_SCAN` 与 `BLUETOOTH_CONNECT`；Manifest 中已声明，运行时会弹框。
- Android 10 及以下仍需定位权限以扫描 BLE。

## 使用说明
1. 允许蓝牙与定位权限。
2. 在顶部输入名称前缀（如 BT），点击“扫描”。
3. 从列表中选择设备并点击“连接”。
4. 在“读寄存器”区域设置地址/数量后点击“读寄存器”。
5. 开启“轮询”即可定期读取。
6. 打开“保存日志”后会在 `Android/data/com.example.bmsdebug/files/notify_log.csv` 中记录数据。

## 核心代码
- `BleViewModel.kt`：使用 `BluetoothLeScanner` + `BluetoothGatt` 实现扫描/连接/读写。
- `ProtocolHelper.kt`：CRC16、封包以及 Notify 解析。
- `MainActivity.kt`：Jetpack Compose UI，展示设备列表与日志。

## 扩展建议
- 增加写寄存器、SOC/电流等快捷按钮。
- 将日志导出为 ShareSheet 以便发送给 PC。
- 在 `BleViewModel` 中根据 `datasetHint` 做进一步字段解析，展示结构化数据。
