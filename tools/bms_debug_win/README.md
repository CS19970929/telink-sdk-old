# Windows BLE 调试工具

## 功能
- 扫描并过滤 BMS 设备（名称前缀可配置）。
- 连接/断开，自动订阅 notify。
- 命令行界面：读/写寄存器、一键轮询常用地址。
- 日志保存（`notify_log.csv`），含时间戳、数据集名称、原始 Hex。

## 环境准备
1. Windows 10 及以上，安装 Python 3.10（确保启用 `Add python.exe to PATH`）。
2. 运行 PowerShell：
   ```powershell
   cd tools\bms_debug_win
   py -m venv .venv
   .\.venv\Scripts\activate
   pip install -r requirements.txt
   ```
3. 第一次运行需在“设置 → 隐私 → 蓝牙”授予终端访问权限。

## 启动
```powershell
cd tools\bms_debug_win
.\.venv\Scripts\activate
python main.py --prefix BT_
```

常用指令：

| 指令 | 作用 |
| --- | --- |
| `scan [秒]` | 扫描附近设备 |
| `connect <序号>` | 连接扫描结果中的指定设备 |
| `disconnect` | 断开连接 |
| `read <addr> <words>` | 发送 Modbus 读命令（函数码 0x03）|
| `write <addr> <value>` | 写单寄存器（0x06）|
| `poll start <秒>` | 轮询 0xD000/0xD026/0xD115 等地址 |
| `poll stop` | 停止轮询 |
| `log on/off` | 打开/关闭 CSV 日志 |
| `quit` | 退出程序 |

## 注意事项
- Bleak 在 Windows 上使用系统蓝牙堆栈，仅支持 BLE 适配器；如使用 USB 加密狗，请确保已安装官方驱动。
- 请避免同时启动多个 BLE 调试工具，否则会造成连接冲突。
- 若 CLI 长时间停在“连接中”，可按 `ctrl+c` 退出后重新运行。

更多例子可见 `examples/` 目录。
