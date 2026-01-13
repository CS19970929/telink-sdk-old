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
from decoder import decode_by_req
from capture import Capturer, replay_file
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
                w.writerow(["ts", "req_addr", "req_words", "byte_len", "regs", "raw_hex"])

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


@dataclass
class LinkStat:
    chunks: int = 0
    frames_ok: int = 0
    frames_crc_fail: int = 0
    resync_drops: int = 0
    pending_max: int = 0
    rtt_count: int = 0
    rtt_sum_ms: float = 0.0
    last_frame_ts: float = 0.0

    def add_rtt(self, ms: float) -> None:
        self.rtt_count += 1
        self.rtt_sum_ms += ms

    def rtt_avg_ms(self) -> Optional[float]:
        if self.rtt_count == 0:
            return None
        return self.rtt_sum_ms / self.rtt_count


class BmsBleClient:
    def __init__(self, ui: ConsoleUI, auto_reconnect: bool) -> None:
        self.ui = ui
        self.auto_reconnect = auto_reconnect

        self.client: Optional[BleakClient] = None
        self.device: Optional[BLEDevice] = None
        self.scan_results: List[BLEDevice] = []

        self._rx_buf = bytearray()
        self._rx_buf_max = 65536

        self._tx_q: asyncio.Queue[bytes] = asyncio.Queue()
        self._tx_task: Optional[asyncio.Task] = None
        self.tx_interval_s: float = 0.01  # 默认10ms

        self._conn_lock = asyncio.Lock()
        self.reconnect_task: Optional[asyncio.Task] = None

        self.pending: Deque[PendingReq] = deque()

        # 默认轮询表：按你确认的真实指令
        self.poll_list: List[Tuple[int, int]] = [
            (0xD000, 0x26),
            (0xD026, 0x19),
            (0xD115, 0x0C),
            (0xD100, 0x15),
        ]
        self.poll_task: Optional[asyncio.Task] = None
        self.poll_gap_each_cmd = 0.6

        self.csv_logger = CsvLogger(Path(__file__).parent / "output" / "notify_log.csv")
        self.capturer = Capturer(Path(__file__).parent / "output" / "capture.jsonl")

        self.stat = LinkStat()

        self.last_frame: Optional[NotifyFrame] = None
        self.last_req: Optional[PendingReq] = None

    async def scan(self, prefix: str, seconds: int = 5) -> List[BLEDevice]:
        self.ui.info(f"开始扫描（{seconds}s）...")
        devices = await BleakScanner.discover(timeout=seconds)
        if prefix:
            devices = [d for d in devices if (d.name or "").startswith(prefix)]
        self.scan_results = devices
        self.ui.show_devices(devices)
        return devices

    async def _cleanup_client(self) -> None:
        if self._tx_task:
            self._tx_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._tx_task
            self._tx_task = None

        while not self._tx_q.empty():
            with contextlib.suppress(asyncio.QueueEmpty):
                self._tx_q.get_nowait()
                self._tx_q.task_done()

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
                self.capturer.write("tx_req", {"hex": payload.hex()})
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
        req = PendingReq(addr=addr, words=words, ts=time.time())
        self.pending.append(req)

        # ✅ 新增：pending 堆太多说明设备回得慢/没回，旧请求没意义，直接丢
        while len(self.pending) > 8:
            self.pending.popleft()

        self.stat.pending_max = max(self.stat.pending_max, len(self.pending))
        await self.write_request(build_read_request(addr, words))
        self.ui.info(f"已发送读命令 addr=0x{addr:04X} words=0x{words:X}({words})")

    
    def _pop_pending_by_len(self, byte_len: int):
        """
        从 pending 队列中按期望回包长度匹配请求，而不是按 FIFO 直接 popleft().
        期望长度 = words * 2
        - 找到匹配：丢弃更早的 pending（认为已经过期/乱序），返回匹配项
        - 找不到：返回 None，不动 pending
        """
        if not self.pending:
            return None

        for i, req in enumerate(self.pending):
            if req.words * 2 == byte_len:
                # 把匹配项之前的全部丢弃（这些大概率已超时或乱序）
                for _ in range(i):
                    self.pending.popleft()
                return self.pending.popleft()

        return None


    async def write_register(self, addr: int, value: int) -> None:
        await self.write_request(build_write_single(addr, value))
        self.ui.info(f"已发送写命令 addr=0x{addr:04X} value=0x{value:04X}")

    async def write_verify(self, addr: int, value: int, raddr: int, words: int) -> None:
        await self.write_register(addr, value)
        await asyncio.sleep(0.2)
        await self.read_registers(raddr, words)

    # poll 管理
    def poll_show(self) -> None:
        if not self.poll_list:
            self.ui.info("轮询表为空")
            return
        self.ui.info("轮询表：")
        for i, (a, w) in enumerate(self.poll_list):
            print(f"  [{i}] addr=0x{a:04X} words=0x{w:X}({w}) byte_len={w*2} (0x{w*2:X})")

    def poll_add(self, addr: int, words: int) -> None:
        self.poll_list.append((addr, words))
        self.ui.info(f"已添加轮询项 addr=0x{addr:04X} words=0x{words:X}({words})")

    def poll_del(self, idx: int) -> None:
        if idx < 0 or idx >= len(self.poll_list):
            raise RuntimeError("index 超出范围")
        a, w = self.poll_list.pop(idx)
        self.ui.info(f"已删除轮询项 addr=0x{a:04X} words=0x{w:X}({w})")

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

    def toggle_csv(self, enabled: bool) -> None:
        self.csv_logger.enabled = enabled
        self.ui.info("CSV日志：" + ("开启" if enabled else "关闭"))
        if enabled:
            self.ui.info(f"输出：{self.csv_logger.path}")

    def toggle_capture(self, enabled: bool) -> None:
        self.capturer.enabled = enabled
        self.ui.info("抓包：" + ("开启" if enabled else "关闭"))
        if enabled:
            self.ui.info(f"输出：{self.capturer.path}")

    def print_stat(self) -> None:
        avg = self.stat.rtt_avg_ms()
        self.ui.info("链路统计：")
        print(f"  chunks={self.stat.chunks}")
        print(f"  frames_ok={self.stat.frames_ok}")
        print(f"  frames_crc_fail={self.stat.frames_crc_fail}")
        print(f"  pending_max={self.stat.pending_max}")
        if avg is not None:
            print(f"  rtt_avg_ms={avg:.1f} (count={self.stat.rtt_count})")

    # notify handler（同步回调）
    def _notification_handler(self, _handle: int, data: bytearray) -> None:
        try:
            self.stat.chunks += 1
            if self.ui.opt.show_chunk:
                self.ui.info(f"notify chunk: {bytes(data).hex()}")
            self.capturer.write("rx_chunk", {"hex": bytes(data).hex()})

            self._rx_buf.extend(data)
            if len(self._rx_buf) > self._rx_buf_max:
                self.ui.warn("RX buffer 过大，丢弃前半段（疑似大量丢包/不同步）")
                self._rx_buf = self._rx_buf[-2048:]

            raws = extract_frames_crc_sync(self._rx_buf)
            for raw in raws:
                self.capturer.write("rx_frame", {"hex": raw.hex()})

                try:
                    frame = parse_notify_strict(raw)
                except Exception as exc:
                    self.stat.frames_crc_fail += 1
                    self.ui.error(f"Notify解析失败：{type(exc).__name__}: {exc} raw={raw.hex()}")
                    continue

                # ✅ 改：不要 FIFO popleft()，按回包 byte_len 匹配 pending
                req = self._pop_pending_by_len(frame.byte_len)
                self.last_frame = frame
                self.last_req = req

                req_label = None
                if req:
                    req_label = f"0x{req.addr:04X}/0x{req.words:X}"
                    # 这里理论上已匹配上，不需要再“长度不一致”警告
                    rtt_ms = (time.time() - req.ts) * 1000.0
                    self.stat.add_rtt(rtt_ms)
                else:
                    # ✅ 新增：没匹配到请求，不要强行解码成某个数据集
                    # 这类帧通常是：重复回包、积压回包、或设备主动上报
                    pass

                self.stat.frames_ok += 1
                self.csv_logger.log(frame, req)

                self.ui.show_notify(frame, req_label)

                # ✅ 只有 req 匹配到，才做“按请求块解码”
                if self.ui.opt.decode and req:
                    dec = decode_by_req(req.addr, req.words, frame.registers)
                    if dec:
                        self.ui.show_decoded(dec)


        except Exception as exc:
            self.ui.error(f"Notify处理异常：{type(exc).__name__}: {exc}")


async def run_replay(ui: ConsoleUI, client: BmsBleClient, file_path: str) -> None:
    p = Path(file_path).expanduser()
    if not p.exists():
        ui.error(f"文件不存在：{p}")
        return
    ui.info(f"开始回放：{p}")
    # 回放：只处理 rx_frame（逻辑帧），走同样的 parse+decode
    for rec in replay_file(p):
        typ = rec.get("type")
        data = rec.get("data", {})
        if typ != "rx_frame":
            continue
        raw = bytes.fromhex(data.get("hex", ""))
        try:
            frame = parse_notify_strict(raw)
        except Exception as exc:
            ui.error(f"[replay] 解析失败：{type(exc).__name__}: {exc}")
            continue
        # 回放时没有 pending，这里只展示 raw regs
        ui.show_notify(frame, req_label="replay")
    ui.info("回放结束")


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
                if not ui.opt.danger_write:
                    ui.warn("写操作已被保护禁止。请输入：danger on  开启写权限")
                    continue
                if len(parts) < 3:
                    ui.warn("用法：write <addr> <value>")
                    continue
                addr = int(parts[1], 0)
                val = int(parts[2], 0)
                await client.write_register(addr, val)

            elif cmd == "writev":
                if not ui.opt.danger_write:
                    ui.warn("写操作已被保护禁止。请输入：danger on  开启写权限")
                    continue
                if len(parts) < 5:
                    ui.warn("用法：writev <addr> <val> <raddr> <words>")
                    continue
                addr = int(parts[1], 0)
                val = int(parts[2], 0)
                raddr = int(parts[3], 0)
                words = int(parts[4], 0)
                await client.write_verify(addr, val, raddr, words)

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

            elif cmd == "show":
                if len(parts) < 3:
                    ui.warn("用法：show regs <n> | show raw on|off | show chunk on|off")
                    continue
                what = parts[1].lower()
                val = parts[2].lower()
                if what == "regs":
                    ui.opt.regs_preview = max(1, int(val, 0))
                    ui.info(f"regs预览个数={ui.opt.regs_preview}")
                elif what == "raw":
                    ui.opt.show_raw = (val == "on")
                    ui.info(f"raw显示={ui.opt.show_raw}")
                elif what == "chunk":
                    ui.opt.show_chunk = (val == "on")
                    ui.info(f"chunk显示={ui.opt.show_chunk}")
                else:
                    ui.warn("用法：show regs <n> | show raw on|off | show chunk on|off")

            elif cmd == "decode":
                if len(parts) < 2:
                    ui.warn("用法：decode on|off")
                    continue
                ui.opt.decode = (parts[1].lower() == "on")
                ui.info(f"decode={ui.opt.decode}")

            elif cmd == "dump":
                if len(parts) < 2 or parts[1].lower() != "last":
                    ui.warn("用法：dump last")
                    continue
                if not client.last_frame:
                    ui.warn("还没有收到任何帧")
                    continue
                ui.info("dump last regs：")
                ui.dump_regs(client.last_frame.registers)

            elif cmd == "log":
                if len(parts) < 2:
                    ui.warn("用法：log on|off")
                    continue
                client.toggle_csv(parts[1].lower() == "on")

            elif cmd == "cap":
                if len(parts) < 2:
                    ui.warn("用法：cap on|off")
                    continue
                client.toggle_capture(parts[1].lower() == "on")

            elif cmd == "stat":
                client.print_stat()

            elif cmd == "tx":
                if len(parts) >= 3 and parts[1].lower() == "rate":
                    ms = float(parts[2])
                    client.tx_interval_s = max(0.0, ms / 1000.0)
                    ui.info(f"tx节流={ms}ms")
                else:
                    ui.warn("用法：tx rate <ms>")

            elif cmd == "danger":
                if len(parts) < 2:
                    ui.warn("用法：danger on|off")
                    continue
                ui.opt.danger_write = (parts[1].lower() == "on")
                ui.info(f"danger_write={ui.opt.danger_write}")

            elif cmd == "replay":
                if len(parts) < 2:
                    ui.warn("用法：replay <capture.jsonl>")
                    continue
                await run_replay(ui, client, parts[1])

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
