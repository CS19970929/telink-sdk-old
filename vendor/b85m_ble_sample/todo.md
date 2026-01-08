
todo 
- 蓝牙app？？？ 快速实现，不需要ui，有debug页面？？？
- flash、log、soc 测试
- 309驱动，参数配置
- h7tool i2c测试
- 低功耗测试是否会有问题
- deep sleep功耗测试


**********************
- adc 温度
- flash soc
- 休眠


8.4.1.2 Flash API 对 BLE timing 的影响
前⾯介绍了使⽤ Flash API 关中断保护来解决软件和硬件 MCU 访问 Flash 时序冲突的问题。由于关中断会使得
所有的中断⽆法实时响应，排队等待中断恢复后延时执⾏，需要考虑被延时时间可能带来的副作⽤。
(1) 关中断对 BLE timing 的影响
结合 BLE timing 的特点来介绍。这个 SDK 中 BLE 连接态的 BTX、BRX 状态机都是由中断任务来完成的。BTX
和 BRX 是类似的实现，以 slave role 的 BRX 为例来说明。
BRX timing 的处理⽐较复杂，以 BLE slave BRX 出现 more data 时 RX IRQ 的处理为例，如下图所⽰。SDK 设
计中要求软件对每⼀个 RX IRQ 都要响应，可以被延迟响应，但不能丢掉。如果某个 RX IRQ 被丢掉，触发这个
RX IRQ 的 RX packet 也会丢掉，造成 Linklayer 丢包的错误。
图中 RX1 在 t1 触发 RX IRQ 1，RX2 在 t2 触发 RX IRQ 2。如果没有关中断发⽣，中断会在 t1 和 t2 实时响应，
软件正确处理 RX packet。
t1 和 t2 时间差为 T_rx_irq，关中断持续时间为 T_irq_dis，T_irq_dis > T_rx_irq。
三种情况 IRQ disable case 1、IRQ disable case 和 IRQ disable case 3 的关中断持续时间都是 T_irq_dis，但关
中断的起点和 t1 的相对时间不⼀样。
IRQ disable case 1，t3 关中断，t4 恢复中断。t3 < t1；t4 > t2。RX IRQ 1 在 t1 ⽆法响应，中断排队等待。RX
IRQ 2 在 t2 触发，覆盖 RX IRQ1（因为中断等待队列⾥只能有⼀个 RX IRQ），RX IRQ 2 排队等待，在 t4 被正确
执⾏。RX IRQ1 对应的 RX1 丢失，如果 RX1 是⼀个有效的数据包，Linklayer 就会出错。
IRQ disable case 2 和 IRQ disable case 3，RX IRQ 1 和 RX IRQ 2 虽然被延迟执⾏，但没有丢掉，不会发⽣错
误。
由以上的例⼦分析可以得到⼀个重要结论：
当中断关闭持续时间⼤于某个安全阈值，可能会发⽣ Linklayer 错误的⻛险。
这个安全阈值，跟 SDK 中 Linklayer 时序设计、BLE Spec 时序特点都相关，⽐例⼦中的 T_rx_irq 复杂得多。具
体细节不详细介绍，这⾥直接给出安全阈值是 220us。
同样是关中断持续时间 T_irq_dis，上⾯例⼦中 IRQ disable case 2 和 IRQ disable case 3，由于关中断发⽣的
时间点不⼀样，RX IRQ 1 或 RX IRQ2 被延时响应，不会发⽣ RX IRQ 2 覆盖 RX IRQ1 导致的丢包。即便是 IRQ
disable case 1，如果 RX1 和 RX2 是⽆关紧要的空包，发⽣丢包也不会造成任何错误。
中断关闭持续时间⼤于 220us 时，不是⼀定会出现错误，必须多个条件同时满⾜，才有可能触发错误，这些条
件包括：关中断的时间较⻓、关中断的时间点和 RX IRQ 发⽣的时间点符合某种特定的关系、BTX 或 BRX 中出
现 more data；连续触发 RX IRQ 的两个 RX packet 都是有效数据包⽽不是空包等等。所以最终结论是：
中断关闭持续时间⼤于 220us 时，会出现 linlayer 出错的⻛险，概率⾮常低。
BLE SDK Linklayer 的设计，以零⻛险为⽬标，即中断关闭持续时间永远要⼩于 220us 的情况，不给任何出错
的机会。
这⾥额外介绍上⾯例⼦ RX packet 丢失的问题。在 Telink BLE SDK 使⽤⽣产中，经常遇到客⼾反馈碰到这个问
题：在加密开启的前提下，看到 device 发送⼀个 reason 为 0x3D（MIC_FAILURE）的 terminate 包，导致断
连。
以上分析可知，中断关闭时间过⻓会导致 RX IRQ 被延迟太⻓时间进⽽被覆盖，最终丢包。但 SDK 会正确处理
好中断关闭时间的问题，⽂档后⾯会详细介绍。更有可能的原因是 user ⽤到了其他的中断（⽐如 Uart、USB
等），这些中断响应时的软件执⾏时间如果过⻓，跟中断关闭的效果时⼀样的，也会让 RX IRQ 延迟。这⾥我们
限定⼀个 user 中断执⾏的最⼤安全时间为 100us。
(2) Flash API 关中断保护对 BLE timing 的影响
为了规避软件访问 Flash 和 MCU 硬件访问 Flash 的时序冲突，Flash API 使⽤了关中断的⽅法。当中断关闭持
续时间⼤于 220us 时，Linklayer 可能发⽣出错的⻛险。为了解决这⼆者的⽭盾，需要关注 Flash API 关中断的
最⼤时间。
受影响的 BLE timing 是 connection state slave role 和 master role。系统初始化和 mainloop 中的 Advertiisng state 不受影响。在 mainloop connection state 中，主要关注以下三个 Flash API：flash_read_page、
flash_write_page、flash_erase_sector。其他 Flash API ⼀般不使⽤或者在初始化的时候才会⽤到。
a) flash_read_page
经测试验证，flash_read_page ⼀次性读取的 byte 数量不超过 64 时，时间⾮常安全，在 220us 以内。超过这
个值后会有⼀定的⻛险。
强烈建议 user 使⽤ flash_read_page 读 Flash 时最多读 64 byte，如果超过 64 byte，需要拆成多次调⽤
flash_read_page 来实现。
b) flash_erase_sector
flash_erase_sector 的时间⼀般在 10ms ~ 100ms 这个量级，远远超过 220us。所以这个 SDK 要求 user 在 BLE
connection state 不要调⽤ flash_erase_sector。直接调⽤这个 API，connection ⼀定会出错。
我们建议 user 使⽤其他⽅式来取代 flash_erase_sector 的设计。⽐如⼀些应⽤是为了反复更新在 Flash 上存储
的⼀些关键信息，设计上可以考虑选取⼀块较⼤的区域，使⽤ flash_write_page 不断往后延伸的⽅法。
BLE slave 应⽤，对于⽆法避免的 flash_erase_sector，如果只是偶尔会发⽣，可以使⽤ Conn state Slave role
时序保护机制来规避，请参考本⽂档的详细介绍。
注意，由于时序保护机制⾮常复杂，对于⾼频率的 flash_erase_sector，不建议这么⽤，⽆法保证在 BLE slave
连接态时反复连接调⽤这套机制的稳定性。建议 user 尽量从设计上去避开这种情况。
c) flash_write_page
flash_write_page 时间收到多个关键因素的影响，包括：Flash 种类、Flash ⼯艺、write byte number、⾼低温
等。下⾯从内置 Flash 的⼏个种类来详细说明。



2pin开关逻辑
电池开机情况下，短接开关3s，电池休眠
电池休眠状态下，短接一下，电池唤醒，但不会输出，输出由灯板逻辑控制