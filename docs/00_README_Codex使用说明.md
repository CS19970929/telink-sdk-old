# Codex 使用说明（Telink SDK + BMS工程）

## 仓库结构要点
- 本仓库为 Telink SDK（TLSR825x 系列），核心目录：
  - stack/        BLE 协议栈与相关库
  - drivers/      通用驱动（GPIO/UART/I2C/Timer 等）
  - common/       公共头文件/工具
  - application/  应用框架与示例相关
  - vendor/       各类示例工程（我的工程在 vendor/ 下）
- 我的工程基于 vendor/b85m_ble_sample 修改而来。

## Codex 工作规则
1. 每次任务开始必须先读：
   - docs/codex/01_项目简报_CODEX_BRIEF.md
   - docs/codex/02_约束与禁区_CONSTRAINTS.md
   - CODEX_TASKS.md
2. 所有任务必须写入 CODEX_TASKS.md
3. 每个 Task 必须有状态字段：未开始 / 进行中 / 已完成
4. 每次启动 Codex，必须说明：
   - 先阅读哪些 docs/codex/*.md
   - 从第一个“状态不是已完成”的 Task 开始
5. 重要结论必须落成文档，不允许只存在于聊天中
6. 输出必须全中文，且尽量提供“文件路径 + 函数名/变量名”定位证据。
7. 如果你需要搜索入口点，优先从 vendor/b85m_ble_sample 的 main/app/att/pm 相关文件入手，再追踪到 stack/ drivers/。

## 目标
- 先生成“SDK↔工程关联地图”和“工程地图”，再做完整优化建议。
- 优化建议必须可执行：给出修复优先级、风险说明与验证方式。
