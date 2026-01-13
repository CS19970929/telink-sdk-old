from __future__ import annotations

from dataclasses import dataclass
from typing import List, Optional, Tuple

# Telink NUS/SPP UUID（你当前用的）
SPP_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
SPP_WRITE_UUID   = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
SPP_NOTIFY_UUID  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

DEFAULT_SLAVE_ID = 0x01
FUNC_READ = 0x03
FUNC_WRITE_SINGLE = 0x06


def crc16_modbus(data: bytes, init: int = 0xFFFF) -> int:
    """Modbus RTU CRC16：poly=0xA001, init=0xFFFF, 低位先移。"""
    crc = init & 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if (crc & 1) else (crc >> 1)
    return crc & 0xFFFF


def append_crc_le(data: bytes) -> bytes:
    """追加 CRC（低字节在前）"""
    crc = crc16_modbus(data)
    return data + bytes((crc & 0xFF, (crc >> 8) & 0xFF))


def build_read_request(addr: int, words: int, slave: int = DEFAULT_SLAVE_ID) -> bytes:
    """0x03 读寄存器请求"""
    p = bytes((slave, FUNC_READ, (addr >> 8) & 0xFF, addr & 0xFF, (words >> 8) & 0xFF, words & 0xFF))
    return append_crc_le(p)


def build_write_single(addr: int, value: int, slave: int = DEFAULT_SLAVE_ID) -> bytes:
    """0x06 写单寄存器请求"""
    p = bytes((slave, FUNC_WRITE_SINGLE, (addr >> 8) & 0xFF, addr & 0xFF, (value >> 8) & 0xFF, value & 0xFF))
    return append_crc_le(p)


def chunk_payload(data: bytes, mtu_payload: int = 20) -> List[bytes]:
    """按 MTU payload 拆包（常见 20）"""
    return [data[i:i + mtu_payload] for i in range(0, len(data), mtu_payload)]


@dataclass
class NotifyFrame:
    raw: bytes
    addr: int
    func: int
    byte_len: int
    payload: bytes
    registers: List[int]


def decode_registers_be(payload: bytes) -> List[int]:
    regs: List[int] = []
    for i in range(0, len(payload), 2):
        if i + 1 < len(payload):
            regs.append((payload[i] << 8) | payload[i + 1])
    return regs


def parse_notify_strict(raw: bytes) -> NotifyFrame:
    """
    严格按 Modbus 0x03 响应格式解析：
      [addr][func][byte_len][payload...][crc_lo][crc_hi]
    """
    if len(raw) < 5:
        raise ValueError("帧长度不足(<5)")

    crc_calc = crc16_modbus(raw[:-2])
    crc_recv = raw[-2] | (raw[-1] << 8)
    if crc_calc != crc_recv:
        raise ValueError(f"CRC失败 calc=0x{crc_calc:04X} recv=0x{crc_recv:04X}")

    addr = raw[0]
    func = raw[1]
    byte_len = raw[2]
    expect_len = 3 + byte_len + 2
    if len(raw) != expect_len:
        raise ValueError(f"长度不匹配 expect={expect_len} got={len(raw)} byte_len={byte_len}")

    payload = raw[3:3 + byte_len]
    regs = decode_registers_be(payload)
    return NotifyFrame(raw=raw, addr=addr, func=func, byte_len=byte_len, payload=payload, registers=regs)


def extract_frames_crc_sync(buf: bytearray, max_frame_len: int = 4096) -> List[bytes]:
    """
    强健切帧：滑窗 + CRC 同步（适配 BLE notify 分包/粘包/偶发丢字节）
    逻辑帧格式（不含 padding）：
      [addr][func][byte_len][payload...][crc_lo][crc_hi]
    但链路上可能出现尾随 0x00 padding（1~2字节）
    """
    out: List[bytes] = []

    def _try_at(start: int) -> Tuple[Optional[bytes], int]:
        if len(buf) - start < 5:
            return None, 0

        byte_len = buf[start + 2]
        base_len = 3 + byte_len + 2
        if base_len < 5 or base_len > max_frame_len:
            return None, 0

        for extra in (0, 1, 2):
            total_len = base_len + extra
            if len(buf) - start < total_len:
                continue
            seg = bytes(buf[start:start + total_len])

            # 剥尾部 0x00（最多 extra 个）
            seg2 = seg
            stripped = 0
            while stripped < extra and len(seg2) > 5 and seg2[-1] == 0x00:
                seg2 = seg2[:-1]
                stripped += 1

            if len(seg2) != base_len:
                continue

            crc_calc = crc16_modbus(seg2[:-2])
            crc_recv = seg2[-2] | (seg2[-1] << 8)
            if crc_calc == crc_recv:
                return seg2, total_len

        return None, 0

    i = 0
    while True:
        if len(buf) - i < 5:
            break

        frame, consume = _try_at(i)
        if frame is not None:
            if i > 0:
                del buf[:i]
            del buf[:consume]
            out.append(frame)
            i = 0
            continue

        i += 1
        if i > 512:
            del buf[:i]
            i = 0

    return out
