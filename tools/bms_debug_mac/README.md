# macOS BLE 调试工具（Python + Bleak）

## 功能概览
- 扫描、过滤并连接 BMS（可设定名称前缀）。
- 自动订阅 `SPP_CLIENT_TO_SERVER` Notify，实时显示原始 Hex 与解析结果。
- 命令面板：读/写寄存器、自定义 Modbus 帧、一键轮询关键寄存器。
- 自动重连选项、超时与失败重试提醒。
- 日志导出：将所有 Notify 带时间戳写入 `output/notify_log.csv`。

## 环境准备
1. macOS 12+，建议使用 Python 3.10。
2. 安装依赖：
   ```bash
   cd tools/bms_debug_mac
   python3 -m venv .venv
   source .venv/bin/activate
   pip install -r requirements.txt
   ```
3. 赋予终端 BLE 权限（系统偏好设置 → 隐私与安全 → 蓝牙）。

## 快速运行
```bash
cd tools/bms_debug_mac
source .venv/bin/activate
python -m main --prefix BT_ --auto-reconnect
```
- `--prefix`：扫描时的名称过滤（可留空）。
- `--auto-reconnect`：连接断开后自动尝试重新连接。

启动后出现交互提示，可输入以下命令：

| 命令 | 说明 |
| --- | --- |
| `scan` | 扫描 5 秒并列出所有设备 |
| `connect <index>` | 连接扫描结果中的指定序号 |
| `disconnect` | 主动断开 |
| `read <addr> <words>` | 发送读命令（默认功能码 0x03，单位 16bit）|
| `write <addr> <value>` | 写单寄存器（功能码 0x06）|
| `poll start <秒>` | 轮询常用寄存器（0xD000、0xD026 等）|
| `poll stop` | 停止轮询 |
| `log on/off` | 开关 CSV 抓包 |
| `quit` | 关闭程序 |

## 目录结构
- `main.py`：程序入口与 BLE 客户端逻辑。
- `protocol.py`：Modbus/CRC/分包封装与 Notify 解析。
- `ui.py`：文本 UI，负责命令输入与输出。
- `examples/`：示例脚本（如轮询电压）。
- `output/`：日志输出目录，默认包含 `README.md`，运行时生成 `notify_log.csv`。

## 注意事项
- 所有命令均在异步事件循环中执行，若界面长时间无响应，可按 `ctrl+c` 退出并重新运行。
- 若需要修改请求 handle/UUID，请同步更新 `protocol.py` 中的 `SPP_CLIENT_HANDLE` 与 `SPP_SERVER_HANDLE`。
- 默认按照 `docs/codex/07_BMS_BLE_PROTOCOL_SPEC.md` 中的 Modbus 帧格式（设备地址=0x01，功能码=0x03）。

## 常见问题
- **无法扫描到设备**：确认 BLE 权限，或使用 `scan --seconds 10` 增加扫描时间。
- **连接后收不到数据**：请先在 App 侧写入 CCC 订阅；程序会自动完成此操作，如仍失败可重新连接。
- **CRC 校验失败**：终端会提示“CRC 校验失败”，该帧仍会写入日志方便溯源。

更多高级用法请参考 `examples/` 目录中的脚本。
