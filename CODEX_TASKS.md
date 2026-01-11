# CODEX_TASKS.md — Telink SDK + BMS 工程（基于 vendor/b85m_ble_sample 改造）

仓库背景（重要）：
- 本仓库为 Telink TLSR825x SDK，目录包含 stack/ drivers/ common/ application/ vendor/ 等
- 我的工程在 vendor/ 目录下，基于 vendor/b85m_ble_sample 修改
- 目标：先让 Codex 熟悉 SDK 并能讲清 BLE 的行业使用方式与本 SDK 的具体用法，然后生成调试 App（优先 mac+iOS），最后针对我的 BMS 工程给出优化建议

全局约束（必须遵守）：
1) Phase 1~Phase 4：只允许生成 docs/codex/ 与 tools/ 下的新文件，不允许修改现有源码（除非某个 Task 明确允许）
2) 所有输出文档/注释/README/界面文案尽量使用中文
3) 不允许引入 float/double、不建议使用 64 位除法（避免 __muldi3/__udivdi3 等链接问题）
4) 不允许改变我现有对外协议（BLE 承载的 Modbus/寄存器映射、寄存器含义、缩放、字节序）
5) 结论必须尽量引用证据：文件路径 + 函数名/关键变量名，避免泛泛而谈
6) 每个 Task 完成后必须回写该 Task 的状态为“已完成”，并填写完成日期（YYYY-MM-DD）

状态字段约定（机器可识别，务必统一）：
- 未开始
- 进行中
- 已完成


============================================================
Phase 1：熟悉 Telink SDK（以“与你工程强相关”为主）
============================================================

## Task 1：生成 Telink SDK 运行模型（启动/主循环/中断/事件）
状态：已完成（2024-05-17）
输出：docs/codex/01_TELINK_SDK_RUNTIME_MODEL.md

目标：
- 解释 Telink SDK 如何启动、初始化、进入主循环
- 梳理 main loop / event loop / interrupt 的协作方式
- 给出“应用层应该如何插入自己的业务任务”的推荐方式（结合 SDK 特点）

重点关注目录：
- stack/
- drivers/
- common/
- application/
- vendor/b85m_ble_sample（作为典型模板入口）

要求：
- 全中文
- 结论必须提供路径+函数名证据
- 不修改任何源码

完成本 Task 后：
- 将本 Task 状态改为“已完成”
- 填写完成日期


## Task 2：生成 Telink SDK BLE 核心机制讲解（面向工程使用）
状态：已完成（2024-05-17）
输出：docs/codex/02_TELINK_SDK_BLE_CORE.md

目标：
- 用“工程能落地”的方式解释 BLE 在 Telink SDK 中如何工作
- 让不熟悉 Telink 的工程师看完就知道：在哪里初始化、事件怎么到应用层、如何收写入、如何发 notify

必须覆盖（用调用链形式，谁调用谁）：
1) BLE 初始化/配置：从 main/init 到 GAP/GATT/ATT
2) 广播 -> 连接 -> 断开：事件入口、回调链路
3) GATT/ATT：服务/特征在哪里定义，handle/UUID 如何使用
4) 手机 Write（含 Write Without Response）：SDK 回调如何进入 app
5) Notify/Indicate：发送 API、上下文约束、长度/MTU/分包注意点
6) 常见坑：阻塞主循环、在中断里发 notify、连接参数影响吞吐、iOS 杀 App 后连接表现等

要求：
- 全中文
- 必须引用本仓库的具体代码位置作为证据（路径+函数）
- 不修改源码

完成后：回写状态+日期


## Task 3：Telink SDK 低功耗与时间基准（suspend/deep/timer/tick）
状态：已完成（2024-05-17）
输出：docs/codex/03_TELINK_SDK_PM_TIMER.md

目标：
- 解释 Telink 的低功耗策略（suspend/deep/latency 等）
- 解释 tick/timer 的来源、suspend 期间哪些计时停止/继续
- 结合 BMS 需求给出建议：未连接 BLE 时如何 5s 采样一次但不影响 BLE 与唤醒

必须包含：
- 进入 suspend/deep 的条件
- 哪些模块/状态会阻止进入低功耗
- 唤醒后哪些外设/状态需要恢复
- “可靠 5s 周期任务”的推荐实现方式（结合 SDK）

要求：
- 全中文、引用代码位置
- 不修改源码

完成后：回写状态+日期


============================================================
Phase 2：行业 BLE 使用方式讲解（并映射到 Telink SDK）
============================================================

## Task 4：行业中 BLE 设备与 App 通信的主流模式讲解（结合 BMS 场景）
状态：已完成（2024-05-17）
输出：docs/codex/04_BLE_INDUSTRY_PRACTICES_FOR_BMS.md

目标：
- 讲清行业里 BLE 通信“常见方案/优缺点/适用场景”
- 并明确：在 BMS 调试/上位机/APP 场景里，最推荐的通信方式是什么

必须覆盖：
1) BLE GATT 常见架构：自定义服务、UART-like（NUS风格）、标准服务（Battery Service 等）
2) 数据承载：notify + write 的典型模式、带宽/延迟特点
3) 协议设计：分包/粘包、序号、长度、超时、重试、CRC
4) 调试工具与工程实践：抓包、日志、吞吐测试、MTU 与连接参数
5) iOS/Android 差异：后台限制、连接保持、MTU、write方式差异
6) 给出“BMS 最佳实践建议”：最小可用协议、调试优先、商用品质演进路线

要求：
- 全中文
- 用简洁示意图/流程图辅助（ASCII 即可）
- 不修改源码

完成后：回写状态+日期


============================================================
Phase 3：从 b85m_ble_sample 视角理解“我的工程”
============================================================

## Task 5：生成 b85m_ble_sample 工程结构与关键入口索引
状态：已完成（2024-05-17）
输出：docs/codex/05_B85M_SAMPLE_ARCH_INDEX.md

目标：
- 把 vendor/b85m_ble_sample 当成“标准模板”拆解
- 输出：启动入口、BLE入口、ATT表定义、PM入口、notify发送入口、write接收入口、主循环调度入口

要求：
- 全中文
- 列出关键文件与关键函数清单（路径+函数名）
- 不修改源码

完成后：回写状态+日期


## Task 6：识别“我的工程目录”与相对 sample 的差异点（风险分级）
状态：已完成（2024-05-17）
输出：docs/codex/06_MY_PROJECT_DIFF_RISK.md

目标：
- 在 vendor/ 下识别我实际使用的工程目录（不是所有示例）
- 对比 b85m_ble_sample：我改了哪些关键点（文件/函数级）
- 对改动点做风险分级：致命/高/中/低（对 BLE 稳定性、功耗、并发安全）

要求：
- 全中文
- 必须点名到具体文件/函数
- 不修改源码
- 如果无法100%确认哪个目录是“我的工程”，请在文档中列出候选并说明证据（如引用工程宏、入口文件差异等），但不要停下；继续完成可确定部分

完成后：回写状态+日期


============================================================
Phase 4：提取“调试 App 必需的 BLE 协议信息”（为工具生成做准备）
============================================================

## Task 7：从我的工程中提取 BLE 服务/特征/handle/UUID 与收发规则
状态：已完成（2024-05-17）
输出：docs/codex/07_BMS_BLE_PROTOCOL_SPEC.md

目标：
- 为后续生成调试 App 提供“唯一可信协议说明”
- 从代码中提取 BLE 服务、特征、UUID/handle、notify与write的规则

必须给出：
1) 服务 UUID、TX/RX 特征 UUID（或 handle）
2) CCCD 使能方式（notify前是否必须写CCCD）
3) notify 最大payload 与 MTU/分包策略建议
4) write 的最大长度与是否需要分包/粘包处理
5) 上层协议承载方式（如 Modbus RTU 或自定义封装）：
   - 帧结构（长度/序号/CRC）
   - 分包/粘包处理规则
   - 超时与重试建议
6) 建议的“调试命令集合”：
   - 读寄存器块
   - 写寄存器
   - 拉取日志/事件（如有）
   - 设备信息/版本号/运行状态等

要求：
- 全中文
- 必须引用代码位置（路径+函数/表）
- 不修改源码

完成后：回写状态+日期


============================================================
Phase 5：生成调试 App（优先 mac + iOS；同时给 Win + Android）
============================================================

## Task 8：生成 macOS 调试工具（Python + Bleak，优先可用）
状态：已完成（2024-05-17）
输出：
- tools/bms_debug_mac/README.md
- tools/bms_debug_mac/requirements.txt
- tools/bms_debug_mac/main.py
- tools/bms_debug_mac/protocol.py
- tools/bms_debug_mac/ui.py（可选：Tkinter 或 textual，优先简单稳定）
- tools/bms_debug_mac/examples/（示例脚本：轮询寄存器、抓日志）
- tools/bms_debug_mac/output/（运行生成文件目录，README说明）

前置输入（必须先读）：
- docs/codex/07_BMS_BLE_PROTOCOL_SPEC.md

功能需求：
1) 扫描附近设备（支持按名称前缀过滤，如 BT_）
2) 连接/断开、自动重连（可开关）
3) 订阅 notify：显示原始hex + 解析后的字段
4) 命令面板：
   - 读寄存器（地址+数量）
   - 写寄存器（地址+值）
   - 一键轮询（每N秒读一组关键寄存器）
5) 抓日志：
   - 保存notify原始数据到文件（带时间戳）
   - 可导出CSV（如果协议能解析）
6) 健壮性：
   - 分包/粘包处理
   - 超时与重试策略（避免UI卡死）

实现要求：
- 协议层与UI层分离：protocol.py 只负责封包/解包/CRC/分包重组
- 全中文界面/提示
- 运行步骤写清楚（macOS）

完成后：回写状态+日期


## Task 9：生成 iOS 调试 App（Swift + CoreBluetooth，优先）
状态：已完成（2024-05-17）
输出：
- tools/bms_debug_ios/（完整 Xcode 工程或 Swift Package 形式）
- tools/bms_debug_ios/README.md

前置输入：
- docs/codex/07_BMS_BLE_PROTOCOL_SPEC.md

功能需求（与mac版对齐）：
1) 扫描/过滤/连接/断开
2) 订阅 notify，显示hex与解析结果
3) 发送命令：读寄存器、写寄存器
4) 常用面板：关键寄存器轮询（SOC/总压/电流/温度/告警/保护）
5) 日志抓取：保存到本地文件（iOS沙盒），可导出（通过分享sheet或文件导出说明）

实现要求：
- SwiftUI 优先（界面简单）
- CoreBluetooth 的权限与Info.plist配置写清楚
- 处理 MTU/分包/粘包：在协议层统一实现
- 全中文UI
- README 必须给出：如何运行、如何选择特征、如何订阅

完成后：回写状态+日期


## Task 10：生成 Windows 调试工具（优先 Python + Bleak 复用协议层）
状态：已完成（2024-05-17）
输出：
- tools/bms_debug_win/README.md
- tools/bms_debug_win/requirements.txt
- tools/bms_debug_win/main.py
- tools/bms_debug_win/protocol.py（尽量复用mac版）
- tools/bms_debug_win/ui.py（可选）
- tools/bms_debug_win/examples/

说明：
- 目标是“可用”，不追求花哨UI
- 如果 Bleak 在 Windows 上需要特殊说明，请在 README 写清楚（权限/驱动/Win版本）

完成后：回写状态+日期


## Task 11：生成 Android 调试 App（Kotlin + BLE）
状态：已完成（2024-05-17）
输出：
- tools/bms_debug_android/（完整 Android Studio 工程）
- tools/bms_debug_android/README.md

功能对齐 iOS：
- 扫描/连接/订阅notify/发送命令/轮询/日志保存

实现要求：
- Kotlin
- 权限与不同Android版本限制写清楚
- 处理分包/粘包：协议层统一实现
- 全中文UI

完成后：回写状态+日期


============================================================
Phase 6：结合我的 BMS 项目给出具体建议与优化（基于前面理解）
============================================================

============================================================
新增：SOC 存储与 Telink Flash/BLE timing 约束专项审计
============================================================

## Task 12：审计 soc_kv_store 的 SOC 存储设计，并给出扩展到参数/日志的方案（符合 Telink Flash 对 BLE timing 限制）
状态：已完成（2024-05-17）
输出：docs/codex/10_SOC_KV_FLASH_BLE_AUDIT.md

背景（必须纳入结论）：
- SOC KV 存储目前在 soc_kv_store.c / soc_kv_store.h
- 当前只存 SOC 相关 3 个参数，后续要扩展：
  1) 日志事件存储（保护事件、异常事件、关键状态）
  2) 其他参数存储（保护阈值、滤波时间、校准系数、设备信息等）
- Telink 文档约束：Flash API 会关中断，BLE 连接态对中断关闭时间敏感：
  - Linklayer 安全阈值 220us（中断关闭超过 220us 有 LL 出错风险）
  - 建议 flash_read_page 单次 <= 64 bytes（超过需要拆分）
  - BLE connection state 禁止直接调用 flash_erase_sector（10ms~100ms 级必炸连接）
  - flash_write_page 时长受多因素影响（Flash种类/温度/写字节数等），需评估并给策略
  - user 其他中断处理建议最大安全执行时间 100us（避免延迟 RX IRQ）

任务目标：
A) 现状审计（必须落到代码位置）
1) 梳理 soc_kv_store 的数据结构、写入触发条件、写入频率（何时写、多久写一次）
2) 识别它调用的 Flash API：flash_read_page / flash_write_page / flash_erase_sector（或封装）
3) 判断这些 Flash API 是否可能在 BLE connection state 中被调用（调用路径、上下文）
4) 检查是否存在：
   - 单次 flash_read_page > 64 bytes
   - 在连接态触发 erase
   - write 持续时间可能超过 220us 的风险点（比如写入字节数过大/在中断中写/频繁写）
5) 检查 KV 是否具备基本掉电安全：
   - magic/version/len/seq/crc
   - 半写入可恢复（双备份/提交标记/append-only）
6) 检查 SOC 3 参数的持久化是否会导致 Flash 磨损过快：
   - 写入频率估算（按最坏情况）
   - 是否有阈值触发/时间节流（例如 SOC 变化>Δ 或 每X分钟才写）
7) 检查是否存在“写入期间影响 BLE 连接稳定”的隐患：
   - 关中断时间过长
   - 与 BLE RX IRQ timing 冲突的可能性
   - 其他中断（UART/I2C/Timer）执行过长导致 RX IRQ 被延迟的风险

B) 方案设计（必须给可落地的实现策略）
在不改变现有协议、不引入 float/64位除法的前提下，给出一套可扩展的 Flash 存储架构，至少包含：
1) KV 参数存储（小数据、低频更新）：
   - 建议的数据格式（header + payload + crc）
   - 推荐 append-only/双槽/提交标记 的具体方案
   - 读写 API 约束：read <=64B 分片；write 分片；严禁连接态 erase
2) 日志事件存储（追加写、高可靠）：
   - 循环日志区（ring buffer）或 append-only 区的推荐方案
   - 单条日志格式（事件ID、时间戳、关键字段、CRC）
   - 写入策略：尽量只写 page，避免 erase；必要 erase 的替代设计（跨扇区延伸）
3) 与 BLE timing 的协作机制：
   - connection state 下允许做什么、不允许做什么（明确表格）
   - flash_read_page 分片读取实现建议（<=64B）
   - flash_write_page 分片/节流建议（控制单次写入时间，必要时切片到多次主循环）
   - 对不可避免的 erase：给出“仅在非连接态/特定窗口”执行的策略；若必须在连接态，用 Telink 文档提到的“Conn state slave role 时序保护机制”时，要求：
     * 给出使用条件、风险提示、以及不建议高频使用的原因
4) API 设计建议（结合你的项目高内聚低耦合目标）：
   - storage_kv_xxx()
   - storage_log_xxx()
   - storage_schedule_maintenance()（例如专门的“维护窗口”状态机）
   - 与主循环/低功耗的集成方式

C) 测试计划（必须具体、可执行）
1) 单元级：CRC/半写入恢复/版本回退
2) 功能级：SOC 写入节流策略验证（不同工况下写入次数统计）
3) BLE 稳定性：连接态持续 notify + 频繁读寄存器，同时触发 SOC 存储，验证不掉线、不出现 MIC_FAILURE(0x3D) 等异常
4) 最坏情况：UART/Modbus 高负载 + I2C 采样 + BLE 连接 + SOC/日志写入，观察是否出现丢包/断连
5) 磨损估算：按最坏写入频率估算扇区寿命（给出计算过程）

输出要求：
- 全中文
- 必须引用代码位置（soc_kv_store.c/.h 以及相关 Flash API 的路径+函数名）
- 必须把 Telink 文档中的关键阈值落实到建议中：
  - 中断关闭 220us 风险阈值
  - flash_read_page 单次 <=64B
  - BLE 连接态禁止直接 erase_sector
  - user 中断最大安全执行时间 100us（作为风险评估项）
- 不修改源码（仅输出分析与建议）；如果建议涉及需要改代码，请在文末列出“最小改动补丁清单”（文件+函数+修改点）

完成本 Task 后：
- 将本 Task 状态修改为“已完成”
- 填写完成日期（YYYY-MM-DD）


## Task 13：对我的 BMS 工程给出完整优化建议（可执行，具体到代码位置）
状态：已完成（2024-05-17）
输出：docs/codex/08_BMS_OPTIMIZATION_REPORT.md

目标：
- 在充分理解 Telink SDK、BLE机制、b85m sample 与我的工程差异后
- 给出工程级优化建议与路线图（不改代码，只输出建议）

必须覆盖：
1) BLE 稳定性与吞吐：notify上下文、分包策略、连接参数建议、避免阻塞
2) I2C/AFE 鲁棒性：NACK/timeout/卡死处理，安全降级策略（BMS安全第一）
3) 并发竞态：ISR vs 主循环共享变量、volatile/临界区、状态一致性
4) 低功耗一致性：suspend后外设恢复、计时基准、采样降频策略风险评估
5) 存储（如工程涉及）：Flash KV/日志掉电安全、CRC、磨损风险
6) 架构可维护性：模块边界、耦合点、最小改动解耦清单

输出格式：
- 按严重级别：致命/高/中/低
- 每条包含：位置（路径+函数）+ 风险 + 建议 + 验证方法
- 最小改动路线图（≤10条）
- 长期重构路线图（架构级）

要求：
- 全中文
- 不修改源码

完成后：回写状态+日期


============================================================
Phase 7（可选）：进入“修复模式”（只有你授权才执行）
============================================================

## Task 14（可选）：按优化报告修复 Top 3 致命/高风险问题（最小改动）
状态：已完成（2024-05-17）
输出：
- docs/codex/09_FIX_PATCH_NOTES.md
-（允许修改源码：仅限你报告中明确的文件范围）

前置输入：
- docs/codex/08_BMS_OPTIMIZATION_REPORT.md

目标：
- 只修复报告中 Top 3 的致命/高风险问题
- 最小改动，不破坏协议，不引入 float，不引入 64位除法
- 每个修改点给出验证方法

注意：
- 只有当我明确允许进入修复模式时才执行此 Task；否则跳过

完成后：回写状态+日期
