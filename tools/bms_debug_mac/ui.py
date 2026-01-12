from __future__ import annotations

import asyncio
from dataclasses import dataclass

from protocol import NotifyFrame


@dataclass
class UiOptions:
    regs_preview: int = 16
    show_raw: bool = False
    show_chunk: bool = False


class ConsoleUI:
    def __init__(self) -> None:
        self.opt = UiOptions()

    def banner(self) -> None:
        print("========================================")
        print(" Telink 8251 BMS BLE 调试工具 (macOS)     ")
        print(" 输入 help 查看所有指令")
        print("========================================")

    async def ainput(self, prompt: str = "指令> ") -> str:
        return await asyncio.to_thread(input, prompt)

    def info(self, msg: str) -> None:
        print(f"[信息] {msg}")

    def warn(self, msg: str) -> None:
        print(f"[警告] {msg}")

    def error(self, msg: str) -> None:
        print(f"[错误] {msg}")

    def help(self) -> None:
        print(
            "\n命令列表：\n"
            "  scan [sec]                    扫描设备\n"
            "  <idx> | connect <idx>         连接扫描列表中的设备\n"
            "  disconnect                    断开连接\n"
            "  read <addr> <words>           读寄存器(0x03)，例：read 0xD000 0x26\n"
            "  write <addr> <value>          写单寄存器(0x06)\n"
            "\n"
            "  poll list                     查看轮询表\n"
            "  poll add <addr> <words>        添加轮询项\n"
            "  poll del <index>              删除轮询项\n"
            "  poll clear                    清空轮询表\n"
            "  poll start [sec]              启动轮询（默认5秒一轮）\n"
            "  poll stop                     停止轮询\n"
            "\n"
            "  log on|off                    CSV日志开关（output/notify_log.csv）\n"
            "  show regs <n>                 每条notify预览寄存器个数\n"
            "  show raw on|off               是否打印raw hex\n"
            "  show chunk on|off             是否打印每个notify分片(用于抓分包/粘包证据)\n"
            "  tx rate <ms>                  写入节流间隔(毫秒)\n"
            "\n"
            "  help                          显示本帮助\n"
            "  quit|exit                     退出\n"
        )

    def show_devices(self, devices) -> None:
        if not devices:
            self.warn("未扫描到设备")
            return
        print("扫描到的设备：")
        for i, d in enumerate(devices):
            print(f"  [{i}]  name={d.name!r}  addr={d.address}")

    def show_notify(self, frame: NotifyFrame, req_label: str | None = None) -> None:
        label = f" req={req_label}" if req_label else ""
        regs = frame.registers
        n = self.opt.regs_preview
        preview = regs[:n]
        more = f" ... (total={len(regs)})" if len(regs) > n else ""
        print(f"[Notify]{label} addr=0x{frame.addr:02X} func=0x{frame.func:02X} len={frame.byte_len}")
        print(f"  regs={preview}{more}")
        if self.opt.show_raw:
            print(f"  raw={frame.raw.hex()}")
