"""macOS BLE 调试主程序（针对 Telink 8251 + NUS/SPP UUID + Modbus CRC）。"""

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
    extract_frames_from_stream,
    parse_notify,
)
from ui import ConsoleUI


# 你常用的轮询地址（按你 BMS 需求）
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

        # 连接/清理互斥，避免重连叠娃
        self._conn_lock = asyncio.Lock()

        # 写入流控：队列 + 单线程发送（避免 Telink RX 溢出）
        self._tx_q: asyncio.Queue[bytes] = asyncio.Queue()
        self._tx_task: Optional[asyncio.Task] = None
        self._tx_min_interval = 0.01  # 10ms，稳定优先。你要更快可调到 0.005/0.002

        # Notify 字节流重组（解决分包/粘包）
        self._rx_buf = bytearray()
        self._rx_buf_max = 8192

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

    async def _cleanup_client(self) -> None:
        # 先停发送任务
        if self._tx_task:
            self._tx_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._tx_task
            self._tx_task = None

        # 清空队列，避免重连后把旧命令发出去
        while not self._tx_q.empty():
            with contextlib.suppress(asyncio.QueueEmpty):
                self._tx_q.get_nowait()
                self._tx_q.task_done()

        # 停 notify + 断开
        if self.client:
            try:
                if self.client.is_connected:
                    with contextlib.suppress(Exception):
                        await self.client.stop_notify(SPP_NOTIFY_UUID)
                    await self.client.disconnect()
            except Exception:
                pass

        self.client = None
        self._rx_buf.clear()

    async def _connect_device(self, device: BLEDevice) -> None:
        async with self._conn_lock:
            await self._cleanup_client()

            self.ui.info(f"正在连接 {device.name or device.address} ...")

            def _handle_disconnect(_: BleakClient) -> None:
                self.ui.warn("连接已断开")
                # 断开后停止轮询，避免无限报错刷屏
                if self.poll_task and not self.poll_task.done():
                    self.poll_task.cancel()

                if self.auto_reconnect and self.device is not None:
                    if self.reconnect_task is None or self.reconnect_task.done():
                        self.reconnect_task = asyncio.create_task(self._attempt_reconnect())

            # 关键：兼容你的 bleak 版本（不用 set_disconnected_callback）
            client = BleakClient(device, disconnected_callback=_handle_disconnect)

            await client.connect()
            await client.start_notify(SPP_NOTIFY_UUID, self._notification_handler)

            self.client = client
            self.device = device

            self._tx_task = asyncio.create_task(self._tx_worker())

            self.ui.info("已连接并完成订阅 Notify")

    async def _attempt_reconnect(self) -> None:
        device = self.device
        while self.auto_reconnect and device is not None:
            try:
                self.ui.info("尝试自动重连...")
                await self._connect_device(device)
                return
            except Exception as exc:
                self.ui.warn(f"重连失败：{type(exc).__name__}: {exc}")
                await asyncio.sleep(5)

    async def disconnect(self) -> None:
        async with self._conn_lock:
            await self._cleanup_client()
            self.device = None
            self.ui.info("已断开")

    async def _tx_worker(self) -> None:
        while True:
            payload = await self._tx_q.get()
            try:
                if not self.client or not self.client.is_connected:
                    raise RuntimeError("尚未连接")

                for part in chunk_payload(payload):
                    await self.client.write_gatt_char(SPP_WRITE_UUID, part, response=False)
                    await asyncio.sleep(self._tx_min_interval)

            except Exception as exc:
                self.ui.error(f"发送失败：{type(exc).__name__}: {exc}")
            finally:
                self._tx_q.task_done()

    async def write_request(self, payload: bytes) -> None:
        if not self.client or not self.client.is_connected:
            raise RuntimeError("尚未连接")
        await self._tx_q.put(payload)

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
        self.ui.info(f"轮询已启动 interval={interval}s")

    async def _poll_loop(self, interval: float) -> None:
        while True:
            try:
                for addr, words in POLL_ADDRS:
                    await self.read_registers(addr, words)
                    # 给 Telink 一点喘息时间（按你设备响应调）
                    await asyncio.sleep(0.2)
                await asyncio.sleep(interval)
            except asyncio.CancelledError:
                return
            except Exception as exc:
                self.ui.error(f"轮询出错：{type(exc).__name__}: {exc}")
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
        if enabled:
            self.ui.info(f"CSV 输出: {self.logger.path}")

    # ---- Notify 回调（同步）----
    def _notification_handler(self, _handle: int, data: bytearray) -> None:
        try:
            self._rx_buf.extend(data)

            if len(self._rx_buf) > self._rx_buf_max:
                self.ui.warn("RX buffer 过大，已清空（疑似协议不同步/丢包）")
                self._rx_buf.clear()
                return

            raws = extract_frames_from_stream(self._rx_buf)
            for raw in raws:
                try:
                    frame = parse_notify(raw)
                except Exception as exc:
                    self.ui.error(f"Notify 解析失败：{type(exc).__name__}: {exc} raw={raw.hex()}")
                    continue

                self.logger.log(frame)
                self.ui.show_notify(frame)

        except Exception as exc:
            self.ui.error(f"Notify 处理异常：{type(exc).__name__}: {exc}")


async def command_loop(client: BmsBleClient, ui: ConsoleUI, prefix: str) -> None:
    while True:
        cmdline = (await ui.ainput()).strip()
        if not cmdline:
            continue

        # 你扫完直接输入 0/1/2 就连接
        if cmdline.isdigit():
            cmdline = f"connect {cmdline}"

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
                # 支持 0xD000 或 53248
                addr = int(parts[1], 0)
                words = int(parts[2], 0)
                await client.read_registers(addr, words)

            elif cmd == "write":
                addr = int(parts[1], 0)
                value = int(parts[2], 0)
                await client.write_register(addr, value)

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
                ui.warn("未知指令，请输入 scan/connect/read/write/poll/log/quit（或直接输入序号连接）")

        except Exception as exc:
            ui.error(f"{type(exc).__name__}: {exc}")


async def async_main(args: argparse.Namespace) -> None:
    ui = ConsoleUI()
    ui.banner()

    client = BmsBleClient(ui, auto_reconnect=args.auto_reconnect)

    await command_loop(client, ui, prefix=args.prefix or "")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Telink 8251 BMS BLE 调试工具（macOS）")
    parser.add_argument("--prefix", default="", help="设备名称前缀过滤，例如 BT_")
    parser.add_argument("--auto-reconnect", action="store_true", help="断开后自动重连")
    return parser.parse_args()


if __name__ == "__main__":
    try:
        asyncio.run(async_main(parse_args()))
    except KeyboardInterrupt:
        pass
