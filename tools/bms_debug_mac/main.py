from __future__ import annotations

import argparse
import asyncio
import contextlib
import csv
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Deque, List, Optional, Tuple

from bleak import BleakClient, BleakScanner
from bleak.backends.device import BLEDevice

from protocol import (
    NotifyFrame,
    SPP_NOTIFY_UUID,
    SPP_WRITE_UUID,
    build_read_request,
    build_write_single,
    chunk_payload,
    extract_frames_crc_sync,
    parse_notify_strict,
)
from ui import ConsoleUI


@dataclass
class PendingReq:
    addr: int
    words: int
    ts: float


@dataclass
class CsvLogger:
    path: Path
    enabled: bool = False

    def __post_init__(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        if not self.path.exists():
            with self.path.open("w", newline="") as f:
                w = csv.writer(f)
                w.writerow(["timestamp", "req_addr", "req_words", "byte_len", "regs", "raw_hex"])

    def log(self, frame: NotifyFrame, req: Optional[PendingReq]) -> None:
        if not self.enabled:
            return
        with self.path.open("a", newline="") as f:
            w = csv.writer(f)
            w.writerow([
                f"{time.time():.3f}",
                f"0x{req.addr:04X}" if req else "",
                f"{req.words}" if req else "",
                f"{frame.byte_len}",
                "[" + ",".join(str(v) for v in frame.registers) + "]",
                frame.raw.hex(),
            ])


class BmsBleClient:
    def __init__(self, ui: ConsoleUI, auto_reconnect: bool) -> None:
        self.ui = ui
        self.auto_reconnect = auto_reconnect

        self.client: Optional[BleakClient] = None
        self.device: Optional[BLEDevice] = None
        self.scan_results: List[BLEDevice] = []

        # 强健切帧 buffer
        self._rx_buf = bytearray()
        self._rx_buf_max = 65536

        # 写入队列 + 节流
        self._tx_q: asyncio.Queue[bytes] = asyncio.Queue()
        self._tx_task: Optional[asyncio.Task] = None
        self.tx_interval_s: float = 0.01  # 默认10ms

        # 连接互斥，避免重连叠娃
        self._conn_lock = asyncio.Lock()
        self.reconnect_task: Optional[asyncio.Task] = None

        # pending 请求队列：以“你输入的read”为准，决定这帧属于谁
        self.pending: Deque[PendingReq] = deque()

        # 轮询表（可动态管理）
        self.poll_list: List[Tuple[int, int]] = [
            (0xD000, 0x26),
            (0xD026, 0x19),
            (0xD115, 0x0C),
            (0xD100, 0x15),
        ]
        self.poll_task: Optional[asyncio.Task] = None
        self.poll_gap_each_cmd = 0.2

        # CSV
        self.logger = CsvLogger(Path(__file__).parent / "output" / "notify_log.csv")

    async def scan(self, prefix: str, seconds: int = 5) -> List[BLEDevice]:
        self.ui.info(f"开始扫描（{seconds}s）...")
        devices = await BleakScanner.discover(timeout=seconds)
        if prefix:
            devices = [d for d in devices if (d.name or "").startswith(prefix)]
        self.scan_results = devices
        self.ui.show_devices(devices)
        return devices

    async def _cleanup_client(self) -> None:
        # 停 tx task
        if self._tx_task:
            self._tx_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._tx_task
            self._tx_task = None

        # 清 tx 队列
        while not self._tx_q.empty():
            with contextlib.suppress(asyncio.QueueEmpty):
                self._tx_q.get_nowait()
                self._tx_q.task_done()

        # 断开
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
        self.pending.clear()

    async def connect(self, index: int) -> None:
        if index >= len(self.scan_results):
            raise RuntimeError("序号超出范围，请先 scan")
        await self._connect_device(self.scan_results[index])

    async def _connect_device(self, device: BLEDevice) -> None:
        async with self._conn_lock:
            await self._cleanup_client()

            self.ui.info(f"正在连接 {device.name or device.address} ...")

            def _handle_disconnect(_: BleakClient) -> None:
                self.ui.warn("连接已断开")
                # 停止轮询（防止刷屏）
                if self.poll_task and not self.poll_task.done():
                    self.poll_task.cancel()

                if self.auto_reconnect and self.device is not None:
                    if self.reconnect_task is None or self.reconnect_task.done():
                        self.reconnect_task = asyncio.create_task(self._attempt_reconnect())

            self.device = device
            self.client = BleakClient(device, disconnected_callback=_handle_disconnect)

            await self.client.connect()
            await self.client.start_notify(SPP_NOTIFY_UUID, self._notification_handler)

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
                    await asyncio.sleep(self.tx_interval_s)
            except Exception as exc:
                self.ui.error(f"发送失败：{type(exc).__name__}: {exc}")
            finally:
                self._tx_q.task_done()

    async def write_request(self, payload: bytes) -> None:
        if not self.client or not self.client.is_connected:
            raise RuntimeError("尚未连接")
        await self._tx_q.put(payload)

    async def read_registers(self, addr: int, words: int) -> None:
        # 先入队 pending，再发
        self.pending.append(PendingReq(addr=addr, words=words, ts=time.time()))
        await self.write_request(build_read_request(addr, words))
        self.ui.info(f"已发送读命令 addr=0x{addr:04X} words={words}")

    async def write_register(self, addr: int, value: int) -> None:
        await self.write_request(build_write_single(addr, value))
        self.ui.info(f"已发送写命令 addr=0x{addr:04X} value=0x{value:04X}")

    # -------- poll 管理 --------
    def poll_show(self) -> None:
        if not self.poll_list:
            self.ui.info("轮询表为空")
            return
        self.ui.info("轮询表：")
        for i, (a, w) in enumerate(self.poll_list):
            print(f"  [{i}] addr=0x{a:04X} words={w} (0x{w:X}) byte_len={w*2} (0x{w*2:X})")

    def poll_add(self, addr: int, words: int) -> None:
        self.poll_list.append((addr, words))
        self.ui.info(f"已添加轮询项 addr=0x{addr:04X} words={words}")

    def poll_del(self, idx: int) -> None:
        if idx < 0 or idx >= len(self.poll_list):
            raise RuntimeError("index 超出范围")
        a, w = self.poll_list.pop(idx)
        self.ui.info(f"已删除轮询项 addr=0x{a:04X} words={w}")

    def poll_clear(self) -> None:
        self.poll_list.clear()
        self.ui.info("已清空轮询表")

    async def start_poll(self, interval: float) -> None:
        if self.poll_task and not self.poll_task.done():
            self.ui.warn("轮询已在运行")
            return
        self.poll_task = asyncio.create_task(self._poll_loop(interval))
        self.ui.info(f"轮询已启动 interval={interval}s")

    async def _poll_loop(self, interval: float) -> None:
        while True:
            try:
                for addr, words in list(self.poll_list):
                    await self.read_registers(addr, words)
                    await asyncio.sleep(self.poll_gap_each_cmd)
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

    # -------- log --------
    def toggle_log(self, enabled: bool) -> None:
        self.logger.enabled = enabled
        self.ui.info("日志抓取：" + ("开启" if enabled else "关闭"))
        if enabled:
            self.ui.info(f"CSV 输出：{self.logger.path}")

    # -------- notify handler（同步回调）--------
    def _notification_handler(self, _handle: int, data: bytearray) -> None:
        try:
            if self.ui.opt.show_chunk:
                self.ui.info(f"notify chunk: {bytes(data).hex()}")

            self._rx_buf.extend(data)
            if len(self._rx_buf) > self._rx_buf_max:
                self.ui.warn("RX buffer 过大，丢弃前半段（疑似大量丢包/不同步）")
                self._rx_buf = self._rx_buf[-2048:]

            raws = extract_frames_crc_sync(self._rx_buf)
            for raw in raws:
                try:
                    frame = parse_notify_strict(raw)
                except Exception as exc:
                    self.ui.error(f"Notify 解析失败：{type(exc).__name__}: {exc} raw={raw.hex()}")
                    continue

                # 取 pending 队头作为本帧对应的请求
                req = self.pending.popleft() if self.pending else None

                # 按标准校验 byte_len = words*2（如果有 req）
                req_label = None
                if req:
                    req_label = f"0x{req.addr:04X}/0x{req.words:X}"
                    expect = req.words * 2
                    if frame.byte_len != expect:
                        self.ui.warn(f"回包长度与请求不一致：req={req_label} expect_byte_len={expect} got={frame.byte_len}")

                self.logger.log(frame, req)
                self.ui.show_notify(frame, req_label=req_label)

        except Exception as exc:
            self.ui.error(f"Notify 处理异常：{type(exc).__name__}: {exc}")


async def command_loop(client: BmsBleClient, ui: ConsoleUI, prefix: str) -> None:
    while True:
        cmdline = (await ui.ainput()).strip()
        if not cmdline:
            continue

        if cmdline.isdigit():
            cmdline = f"connect {cmdline}"

        parts = cmdline.split()
        cmd = parts[0].lower()

        try:
            if cmd in {"help", "h", "?"}:
                ui.help()

            elif cmd == "scan":
                sec = int(parts[1]) if len(parts) > 1 else 5
                await client.scan(prefix, sec)

            elif cmd == "connect":
                if len(parts) < 2:
                    ui.warn("用法：connect <idx>")
                    continue
                await client.connect(int(parts[1]))

            elif cmd == "disconnect":
                await client.disconnect()

            elif cmd == "read":
                if len(parts) < 3:
                    ui.warn("用法：read <addr> <words> 例：read 0xD000 0x26")
                    continue
                addr = int(parts[1], 0)
                words = int(parts[2], 0)
                await client.read_registers(addr, words)

            elif cmd == "write":
                if len(parts) < 3:
                    ui.warn("用法：write <addr> <value>")
                    continue
                addr = int(parts[1], 0)
                val = int(parts[2], 0)
                await client.write_register(addr, val)

            elif cmd == "poll":
                if len(parts) < 2:
                    ui.warn("用法：poll list|add|del|clear|start|stop")
                    continue
                sub = parts[1].lower()
                if sub == "list":
                    client.poll_show()
                elif sub == "add":
                    if len(parts) < 4:
                        ui.warn("用法：poll add <addr> <words>")
                        continue
                    addr = int(parts[2], 0)
                    words = int(parts[3], 0)
                    client.poll_add(addr, words)
                elif sub == "del":
                    if len(parts) < 3:
                        ui.warn("用法：poll del <index>")
                        continue
                    client.poll_del(int(parts[2], 0))
                elif sub == "clear":
                    client.poll_clear()
                elif sub == "start":
                    interval = float(parts[2]) if len(parts) > 2 else 5.0
                    await client.start_poll(interval)
                elif sub == "stop":
                    await client.stop_poll()
                else:
                    ui.warn("用法：poll list|add|del|clear|start|stop")

            elif cmd == "log":
                if len(parts) < 2:
                    ui.warn("用法：log on|off")
                    continue
                client.toggle_log(parts[1].lower() == "on")

            elif cmd == "show":
                if len(parts) < 3:
                    ui.warn("用法：show regs <n> | show raw on|off | show chunk on|off")
                    continue
                what = parts[1].lower()
                val = parts[2].lower()
                if what == "regs":
                    ui.opt.regs_preview = max(1, int(val, 0))
                    ui.info(f"regs 预览个数 = {ui.opt.regs_preview}")
                elif what == "raw":
                    ui.opt.show_raw = (val == "on")
                    ui.info(f"raw 显示 = {ui.opt.show_raw}")
                elif what == "chunk":
                    ui.opt.show_chunk = (val == "on")
                    ui.info(f"chunk 显示 = {ui.opt.show_chunk}")
                else:
                    ui.warn("用法：show regs <n> | show raw on|off | show chunk on|off")

            elif cmd == "tx":
                # tx rate <ms>
                if len(parts) >= 3 and parts[1].lower() == "rate":
                    ms = float(parts[2])
                    client.tx_interval_s = max(0.0, ms / 1000.0)
                    ui.info(f"tx 节流 = {ms} ms")
                else:
                    ui.warn("用法：tx rate <ms>")

            elif cmd in {"quit", "exit"}:
                await client.disconnect()
                return

            else:
                ui.warn("未知指令，输入 help 查看用法")

        except Exception as exc:
            ui.error(f"{type(exc).__name__}: {exc}")


async def async_main(args: argparse.Namespace) -> None:
    ui = ConsoleUI()
    ui.banner()
    ui.help()

    client = BmsBleClient(ui, auto_reconnect=args.auto_reconnect)
    await command_loop(client, ui, prefix=args.prefix or "")


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Telink 8251 BMS BLE 调试工具(macOS)")
    p.add_argument("--prefix", default="", help="设备名称前缀过滤，例如 BT_")
    p.add_argument("--auto-reconnect", action="store_true", help="断开后自动重连")
    return p.parse_args()


if __name__ == "__main__":
    try:
        asyncio.run(async_main(parse_args()))
    except KeyboardInterrupt:
        pass
