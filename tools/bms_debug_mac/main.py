"""macOS BLE 调试主程序。"""
from __future__ import annotations

import argparse
import asyncio
import contextlib
import csv
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice

from protocol import (
    NotifyFrame,
    SPP_NOTIFY_UUID,
    SPP_WRITE_UUID,
    build_read_request,
    build_write_single,
    chunk_payload,
    parse_notify,
)
from ui import ConsoleUI


POLL_ADDRS = [
    (0xD000, 0x0038),
    (0xD026, 0x0019),
    (0xD115, 0x000C),
]


@dataclass
class CsvLogger:
    path: Path
    enabled: bool = False

    def __post_init__(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if not self.path.exists():
            with self.path.open("w", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(["timestamp", "dataset", "length", "registers", "raw_hex"])

    def log(self, frame: NotifyFrame) -> None:
        if not self.enabled:
            return
        with self.path.open("a", newline="") as f:
            csv.writer(f).writerow(frame.to_csv_row(time.time()))


class BmsBleClient:
    def __init__(self, ui: ConsoleUI, auto_reconnect: bool) -> None:
        self.ui = ui
        self.auto_reconnect = auto_reconnect
        self.client: Optional[BleakClient] = None
        self.device: Optional[BLEDevice] = None
        self.scan_results: List[BLEDevice] = []
        self.logger = CsvLogger(Path(__file__).parent / "output" / "notify_log.csv")
        self.poll_task: Optional[asyncio.Task] = None
        self.reconnect_task: Optional[asyncio.Task] = None

    async def scan(self, prefix: str, seconds: int = 5) -> List[BLEDevice]:
        self.ui.info(f"开始扫描（{seconds}s）...")
        devices = await BleakScanner.discover(timeout=seconds)
        if prefix:
            devices = [d for d in devices if (d.name or "").startswith(prefix)]
        self.scan_results = devices
        self.ui.show_devices(devices)
        return devices

    async def connect(self, index: int) -> None:
        if index >= len(self.scan_results):
            raise RuntimeError("序号超出范围，请先执行 scan")
        await self._connect_device(self.scan_results[index])

    async def _connect_device(self, device: BLEDevice) -> None:
        self.ui.info(f"正在连接 {device.name or device.address} ...")
        client = BleakClient(device)

        def _handle_disconnect(_: BleakClient) -> None:
            self.ui.warn("连接已断开")
            if self.auto_reconnect and self.device is not None:
                if self.reconnect_task is None or self.reconnect_task.done():
                    self.reconnect_task = asyncio.create_task(self._attempt_reconnect())

        client.set_disconnected_callback(_handle_disconnect)
        await client.connect()
        await client.start_notify(SPP_NOTIFY_UUID, self._notification_handler)
        self.ui.info("已连接并完成订阅 Notify")
        self.client = client
        self.device = device

    async def _attempt_reconnect(self) -> None:
        device = self.device
        while self.auto_reconnect and device is not None:
            try:
                self.ui.info("尝试自动重连...")
                await self._connect_device(device)
                return
            except Exception as exc:  # pylint: disable=broad-except
                self.ui.warn(f"重连失败：{exc}")
                await asyncio.sleep(5)

    async def disconnect(self) -> None:
        if self.client and self.client.is_connected:
            await self.client.stop_notify(SPP_NOTIFY_UUID)
            await self.client.disconnect()
        self.client = None
        self.device = None
        self.ui.info("已断开")

    async def write_request(self, payload: bytes) -> None:
        if not self.client or not self.client.is_connected:
            raise RuntimeError("尚未连接")
        chunks = chunk_payload(payload)
        for part in chunks:
            await self.client.write_gatt_char(SPP_WRITE_UUID, part, response=False)

    async def read_registers(self, addr: int, words: int) -> None:
        await self.write_request(build_read_request(addr, words))
        self.ui.info(f"已发送读命令 addr=0x{addr:04X} words={words}")

    async def write_register(self, addr: int, value: int) -> None:
        await self.write_request(build_write_single(addr, value))
        self.ui.info(f"已发送写命令 addr=0x{addr:04X} value=0x{value:04X}")

    async def start_poll(self, interval: float) -> None:
        if self.poll_task and not self.poll_task.done():
            self.ui.warn("轮询已在运行")
            return
        self.poll_task = asyncio.create_task(self._poll_loop(interval))
        self.ui.info("轮询已启动")

    async def _poll_loop(self, interval: float) -> None:
        while True:
            try:
                for addr, words in POLL_ADDRS:
                    await self.read_registers(addr, words)
                    await asyncio.sleep(0.2)
                await asyncio.sleep(interval)
            except Exception as exc:  # pylint: disable=broad-except
                self.ui.error(f"轮询出错：{exc}")
                await asyncio.sleep(interval)

    async def stop_poll(self) -> None:
        if self.poll_task:
            self.poll_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self.poll_task
            self.poll_task = None
            self.ui.info("已停止轮询")

    def toggle_log(self, enabled: bool) -> None:
        self.logger.enabled = enabled
        self.ui.info("日志抓取：" + ("开启" if enabled else "关闭"))

    def _notification_handler(self, _handle: int, data: bytearray) -> None:
        try:
            frame = parse_notify(bytes(data))
        except Exception as exc:  # pylint: disable=broad-except
            self.ui.error(f"Notify 解析失败：{exc}")
            return
        self.logger.log(frame)
        self.ui.show_notify(frame)


async def command_loop(client: BmsBleClient, ui: ConsoleUI, prefix: str) -> None:
    while True:
        cmdline = (await ui.ainput()).strip()
        if not cmdline:
            continue
        parts = cmdline.split()
        cmd = parts[0].lower()
        try:
            if cmd == "scan":
                seconds = int(parts[1]) if len(parts) > 1 else 5
                await client.scan(prefix, seconds)
            elif cmd == "connect":
                await client.connect(int(parts[1]))
            elif cmd == "disconnect":
                await client.disconnect()
            elif cmd == "read":
                await client.read_registers(int(parts[1], 0), int(parts[2], 0))
            elif cmd == "write":
                await client.write_register(int(parts[1], 0), int(parts[2], 0))
            elif cmd == "poll" and len(parts) >= 2 and parts[1] == "start":
                interval = float(parts[2]) if len(parts) > 2 else 5.0
                await client.start_poll(interval)
            elif cmd == "poll" and len(parts) >= 2 and parts[1] == "stop":
                await client.stop_poll()
            elif cmd == "log" and len(parts) >= 2:
                client.toggle_log(parts[1].lower() == "on")
            elif cmd in {"quit", "exit"}:
                await client.disconnect()
                return
            else:
                ui.warn("未知指令，请输入 scan/connect/read/write/poll/log/quit")
        except Exception as exc:  # pylint: disable=broad-except
            ui.error(str(exc))


async def async_main(args: argparse.Namespace) -> None:
    ui = ConsoleUI()
    ui.banner()
    client = BmsBleClient(ui, auto_reconnect=args.auto_reconnect)
    await command_loop(client, ui, prefix=args.prefix or "")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Telink BMS BLE 调试工具")
    parser.add_argument("--prefix", default="", help="设备名称前缀过滤")
    parser.add_argument("--auto-reconnect", action="store_true", help="断开后自动重连")
    return parser.parse_args()


if __name__ == "__main__":
    try:
        asyncio.run(async_main(parse_args()))
    except KeyboardInterrupt:
        pass
