"""Windows BLE 调试 CLI。"""
from __future__ import annotations

import argparse
import asyncio
import contextlib
import csv
import time
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
from ui import SimpleUI


class CsvLogger:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.enabled = False
        if not path.exists():
            path.parent.mkdir(parents=True, exist_ok=True)
            with path.open("w", newline="") as f:
                csv.writer(f).writerow(["timestamp", "dataset", "raw_hex"])

    def log(self, frame: NotifyFrame) -> None:
        if not self.enabled:
            return
        with self.path.open("a", newline="") as f:
            csv.writer(f).writerow(frame.csv_row(time.time()))


class WinBleClient:
    def __init__(self, ui: SimpleUI) -> None:
        self.ui = ui
        self.client: Optional[BleakClient] = None
        self.device: Optional[BLEDevice] = None
        self.scan_results: List[BLEDevice] = []
        self.logger = CsvLogger(Path("output") / "notify_log.csv")
        self.poll_task: Optional[asyncio.Task] = None

    async def scan(self, prefix: str, seconds: int) -> None:
        self.ui.info(f"扫描 {seconds}s ...")
        devices = await BleakScanner.discover(timeout=seconds)
        if prefix:
            devices = [d for d in devices if (d.name or "").startswith(prefix)]
        self.scan_results = devices
        for idx, dev in enumerate(devices):
            self.ui.print(f"[{idx}] {dev.name or '(无名)'} {dev.address}")

    async def connect(self, index: int) -> None:
        if index >= len(self.scan_results):
            raise RuntimeError("无效序号")
        device = self.scan_results[index]
        self.ui.info(f"连接 {device.name or device.address}")
        client = BleakClient(device)
        client.set_disconnected_callback(lambda _: self.ui.warn("已断开"))
        await client.connect()
        await client.start_notify(SPP_NOTIFY_UUID, self._handle_notify)
        self.client = client
        self.device = device
        self.ui.info("连接成功并已订阅")

    async def disconnect(self) -> None:
        if self.client and self.client.is_connected:
            await self.client.stop_notify(SPP_NOTIFY_UUID)
            await self.client.disconnect()
        self.client = None
        self.device = None
        self.ui.info("已断开")

    async def write(self, payload: bytes) -> None:
        if not self.client or not self.client.is_connected:
            raise RuntimeError("尚未连接")
        for part in chunk_payload(payload):
            await self.client.write_gatt_char(SPP_WRITE_UUID, part, response=False)

    async def read_registers(self, addr: int, words: int) -> None:
        await self.write(build_read_request(addr, words))

    async def write_register(self, addr: int, value: int) -> None:
        await self.write(build_write_single(addr, value))

    async def start_poll(self, interval: float) -> None:
        if self.poll_task and not self.poll_task.done():
            self.ui.warn("轮询已启用")
            return
        self.poll_task = asyncio.create_task(self._poll_loop(interval))

    async def _poll_loop(self, interval: float) -> None:
        while True:
            try:
                for addr, words in ((0xD000, 0x0038), (0xD026, 0x0019), (0xD115, 0x000C)):
                    await self.read_registers(addr, words)
                    await asyncio.sleep(0.2)
                await asyncio.sleep(interval)
            except Exception as exc:  # pylint: disable=broad-except
                self.ui.error(str(exc))
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
        self.ui.info("日志：" + ("开启" if enabled else "关闭"))

    def _handle_notify(self, _handle: int, data: bytearray) -> None:
        try:
            frame = parse_notify(bytes(data))
        except Exception as exc:  # pylint: disable=broad-except
            self.ui.error(f"解析失败：{exc}")
            return
        self.logger.log(frame)
        self.ui.print(f"Notify -> {frame.dataset_hint} {frame.raw.hex()}")


async def repl(client: WinBleClient, ui: SimpleUI, prefix: str) -> None:
    while True:
        cmdline = (await ui.ainput()).strip()
        if not cmdline:
            continue
        parts = cmdline.split()
        cmd = parts[0]
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
            elif cmd == "poll" and parts[1] == "start":
                interval = float(parts[2]) if len(parts) > 2 else 5.0
                await client.start_poll(interval)
            elif cmd == "poll" and parts[1] == "stop":
                await client.stop_poll()
            elif cmd == "log":
                client.toggle_log(parts[1].lower() == "on")
            elif cmd in {"quit", "exit"}:
                await client.disconnect()
                return
            else:
                ui.warn("未知指令")
        except Exception as exc:  # pylint: disable=broad-except
            ui.error(str(exc))


async def async_main(args: argparse.Namespace) -> None:
    ui = SimpleUI()
    client = WinBleClient(ui)
    await repl(client, ui, prefix=args.prefix)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Windows BMS BLE 调试工具")
    parser.add_argument("--prefix", default="", help="名称前缀过滤")
    return parser.parse_args()


if __name__ == "__main__":
    try:
        asyncio.run(async_main(parse_args()))
    except KeyboardInterrupt:
        pass
