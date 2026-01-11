# 项目简报（BMS on Telink TLSR8251）

## 1. 基础信息
- SDK：Telink TLSR825x（本仓库 telink-sdk-old）
- 我的工程位置：vendor/b85m_ble_sample （基于 vendor/b85m_ble_sample 修改）
- 芯片：TLSR8251（无FPU；尽量不用float；避免64位乘除）
- AFE：SH367309（I2C采集）
- 电池：16S；分口 CHG/DSG（未来可能支持同口）
- 通讯：
  - BLE：NUS风格/notify发送 + write接收（上层协议为 Modbus 寄存器映射）
  - UART：Modbus从机（上位机/调试）

## 2. 安全与鲁棒性要求
- 任何 AFE I2C 异常（NACK/超时/总线卡死/连续失败）必须触发安全降级策略：
  - 至少：暂停均衡 + 上报告警
  - 严重：关闭CHG/DSG MOS 或禁止输出（按策略）
- 不允许继续使用旧数据假装正常。
- Flash 存储必须掉电安全（CRC/版本/半写入可恢复）。

## 3. 关键业务链路（必须保持正确）
AFE采样 -> 数据换算/滤波 -> 保护判断 -> MOS控制 -> 上报(BLE/UART Modbus) -> 存储(KV/日志) -> 低功耗策略

## 4. 低功耗策略（目标）
- BLE未连接时：主循环任务降频（例如每 5s 采样一次 AFE）
- BLE连接/OTA/调试时：恢复高频采样
- suspend/唤醒后：外设状态必须恢复；首次采样必须“新鲜有效”

## 5. 构建说明（请补充）
- 编译入口工程：vendor/________（填你的工程目录名，如果就是 b85m_ble_sample 改的请写明）
- 编译方式：Telink IDE / make / 其他
- 关键宏：________（例如工程选择宏、是否开启PM、是否开启UART、是否开启log等）
