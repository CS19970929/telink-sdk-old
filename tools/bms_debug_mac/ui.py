"""简单控制台 UI（无第三方依赖）。"""

from __future__ import annotations

import asyncio
from typing import List

from bleak.backends.device import BLEDevice

from protocol import NotifyFrame


class ConsoleUI:
    def banner(self) -> None:
        print("========================================")
        print(" Telink 8251 BMS BLE 调试工具 (macOS)     ")
        print(" 命令: scan [sec] | <idx> | connect <idx>")
        print("       read <addr> <words> | write <addr> <val>")
        print("       poll start [sec] | poll stop")
        print("       log on|off | disconnect | quit")
        print("========================================")

    async def ainput(self, prompt: str = "指令> ") -> str:
        # 避免阻塞事件循环
        return await asyncio.to_thread(input, prompt)

    def info(self, msg: str) -> None:
        print(f"[信息] {msg}")

    def warn(self, msg: str) -> None:
        print(f"[警告] {msg}")

    def error(self, msg: str) -> None:
        print(f"[错误] {msg}")

    def show_devices(self, devices: List[BLEDevice]) -> None:
        if not devices:
            self.warn("未扫描到设备")
            return
        print("扫描到的设备：")
        for i, d in enumerate(devices):
            print(f"  [{i}]  name={d.name!r}  addr={d.address}")

    def show_notify(self, frame: NotifyFrame) -> None:
        # 给你调试用：打印最关键字段 + 前几个寄存器
        regs_preview = frame.registers[:12]
        regs_more = " ..." if len(frame.registers) > 12 else ""
        print(
            f"[Notify] addr=0x{frame.addr:02X} func=0x{frame.func:02X} "
            f"len={len(frame.payload)} dataset={frame.dataset_hint} "
            f"regs={regs_preview}{regs_more}"
        )
