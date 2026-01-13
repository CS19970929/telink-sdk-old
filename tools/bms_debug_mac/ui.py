from __future__ import annotations

import asyncio
from dataclasses import dataclass
from typing import Any, Dict, Optional

from protocol import NotifyFrame
from decoder import Decoded


@dataclass
class UiOptions:
    regs_preview: int = 16
    show_raw: bool = False
    show_chunk: bool = False
    decode: bool = True
    danger_write: bool = False


class ConsoleUI:
    def __init__(self) -> None:
        self.opt = UiOptions()

    def banner(self) -> None:
        print("========================================")
        print(" Telink 8251 BMS BLE 调试工具 (macOS)     ")
        print(" 目标：无在线调试下的“证据链+回放+解码”    ")
        print(" 输入 help 查看指令")
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
            "\n"
            "  read <addr> <words>           读寄存器(0x03) 例：read 0xD000 0x26\n"
            "  write <addr> <value>          写单寄存器(0x06)\n"
            "  writev <addr> <val> <raddr> <words>  写后验证读\n"
            "\n"
            "  poll list                     查看轮询表\n"
            "  poll add <addr> <words>        添加轮询项\n"
            "  poll del <index>              删除轮询项\n"
            "  poll clear                    清空轮询表\n"
            "  poll start [sec]              启动轮询（默认5秒一轮）\n"
            "  poll stop                     停止轮询\n"
            "\n"
            "  show regs <n>                 每条notify预览寄存器个数\n"
            "  show raw on|off               是否打印raw hex\n"
            "  show chunk on|off             是否打印notify分片(hex)\n"
            "  decode on|off                 是否输出解码字段\n"
            "  dump last                     打印上一帧所有寄存器(带索引)\n"
            "\n"
            "  log on|off                    CSV日志开关（output/notify_log.csv）\n"
            "  cap on|off                    抓包(jsonl)开关（output/capture.jsonl）\n"
            "  stat                          链路统计（CRC/重同步/RTT等）\n"
            "  tx rate <ms>                  写入节流间隔(毫秒)\n"
            "  danger on|off                 写保护：默认禁止 write，danger on 才允许\n"
            "\n"
            "  replay <capture.jsonl>         离线回放抓包文件（不连设备）\n"
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

    def show_notify(self, frame: NotifyFrame, req_label: Optional[str]) -> None:
        label = f" req={req_label}" if req_label else ""
        regs = frame.registers
        n = self.opt.regs_preview
        preview = regs[:n]
        more = f" ... (total={len(regs)})" if len(regs) > n else ""
        print(f"[Notify]{label} addr=0x{frame.addr:02X} func=0x{frame.func:02X} len={frame.byte_len}")
        print(f"  regs={preview}{more}")
        if self.opt.show_raw:
            print(f"  raw={frame.raw.hex()}")

    def show_decoded(self, dec: Decoded) -> None:
        print(f"[Decode] {dec.name}")
        # 精简打印：大对象（cells）只打印统计和头尾
        d = dec.data
        if "cells_mv" in d:
            cells = d["cells_mv"]
            print(f"  cells_valid={d.get('cells_valid_count')}  head={cells[:6]}  tail={cells[-6:]}")
            for k in ("pack_v", "vmax_mv", "vmin_mv", "delta_mv", "vmax_pos_1based", "vmin_pos_1based"):
                if k in d:
                    print(f"  {k}: {d[k]}")
        else:
            # 其它数据块：直接打印关键字段
            for k, v in d.items():
                if k == "raw_regs":
                    continue
                print(f"  {k}: {v}")

    def dump_regs(self, regs) -> None:
        for i, v in enumerate(regs):
            print(f"  [{i:02d}] {v} (0x{v:04X})")
