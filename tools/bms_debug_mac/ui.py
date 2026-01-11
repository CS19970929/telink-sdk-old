"""简单文本 UI：负责输入输出与表格展示。"""
import asyncio
from typing import Iterable, List

from rich.console import Console
from rich.table import Table

from protocol import NotifyFrame


class ConsoleUI:
    def __init__(self) -> None:
        self.console = Console()

    def banner(self) -> None:
        self.console.print("[bold cyan]Telink BMS BLE 调试器[/bold cyan]")

    def info(self, msg: str) -> None:
        self.console.print(f"[green][信息][/green] {msg}")

    def warn(self, msg: str) -> None:
        self.console.print(f"[yellow][警告][/yellow] {msg}")

    def error(self, msg: str) -> None:
        self.console.print(f"[red][错误][/red] {msg}")

    def show_devices(self, devices: List) -> None:
        table = Table(title="扫描到的设备")
        table.add_column("序号", justify="right")
        table.add_column("名称")
        table.add_column("地址")
        for i, dev in enumerate(devices):
            table.add_row(str(i), dev.name or "(无名)", dev.address)
        if devices:
            self.console.print(table)
        else:
            self.warn("未发现符合条件的设备")

    def show_notify(self, frame: NotifyFrame) -> None:
        regs = ",".join(f"{v}" for v in frame.registers[:8])
        if len(frame.registers) > 8:
            regs += "..."
        self.console.print(
            f"[blue]Notify[/blue] 集合={frame.dataset_hint} 长度={len(frame.payload)} Raw={frame.raw.hex()} 数据={regs}"
        )

    async def ainput(self, prompt: str = "指令> ") -> str:
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(None, lambda: input(prompt))


__all__ = ["ConsoleUI"]
