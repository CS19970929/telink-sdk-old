"""协议封装层：CRC、封包/解包、notify解析、切帧规则。"""

from __future__ import annotations

from dataclasses import dataclass
from typing import List


# Telink NUS/SPP UUID（你现在用的这套）
SPP_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
SPP_WRITE_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # 手机/PC -> 设备
SPP_NOTIFY_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # 设备 -> 手机/PC

DEFAULT_SLAVE_ID = 0x01
FUNC_READ = 0x03
FUNC_WRITE_SINGLE = 0x06

# 依据你的 spec：按 payload 字节数推断数据集（可按你项目扩展）
DATASET_BY_LENGTH = {
    0x4C: "0xD000 单体电压/总压",
    0x82: "0x2100 保护参数",
    0x32: "0xD026 SOC/温度",
    0x18: "0xD115 运行状态",
    0x2A: "0xD100 保护记录",
}


def crc16_modbus(data: bytes) -> int:
    """Modbus RTU CRC16（低字节在前）。"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def append_crc(data: bytes) -> bytes:
    crc = crc16_modbus(data)
    return data + bytes((crc & 0xFF, (crc >> 8) & 0xFF))


def build_read_request(addr: int, words: int, slave: int = DEFAULT_SLAVE_ID) -> bytes:
    """功能码0x03 读寄存器请求（Modbus RTU 风格）。"""
    payload = bytes((slave, FUNC_READ, (addr >> 8) & 0xFF, addr & 0xFF, (words >> 8) & 0xFF, words & 0xFF))
    return append_crc(payload)


def build_write_single(addr: int, value: int, slave: int = DEFAULT_SLAVE_ID) -> bytes:
    """功能码0x06 写单寄存器请求。"""
    payload = bytes((slave, FUNC_WRITE_SINGLE, (addr >> 8) & 0xFF, addr & 0xFF, (value >> 8) & 0xFF, value & 0xFF))
    return append_crc(payload)


def chunk_payload(data: bytes, mtu_payload: int = 20) -> List[bytes]:
    """按 MTU 拆包。Telink 默认常见 20 字节（ATT_MTU=23）。"""
    return [data[i : i + mtu_payload] for i in range(0, len(data), mtu_payload)]


@dataclass
class NotifyFrame:
    raw: bytes
    addr: int
    func: int
    payload: bytes
    registers: List[int]
    dataset_hint: str

    def to_csv_row(self, timestamp: float) -> List[str]:
        return [
            f"{timestamp:.3f}",
            self.dataset_hint,
            str(len(self.payload)),
            "[" + ",".join(str(v) for v in self.registers) + "]",
            self.raw.hex(),
        ]


def decode_registers(payload: bytes) -> List[int]:
    regs: List[int] = []
    # 你的 payload 是按 16-bit big-endian 寄存器返回
    for i in range(0, len(payload), 2):
        if i + 1 < len(payload):
            regs.append((payload[i] << 8) | payload[i + 1])
    return regs


def parse_notify(raw: bytes) -> NotifyFrame:
    """
    你定义的 notify 格式（类似 Modbus 响应）：
      [addr][func][byte_len][payload...][crc_lo][crc_hi]
    """
    # 兼容：CRC 后带 0x00 尾随填充
    if len(raw) >= 6 and raw[-1] == 0x00:
        raw = raw[:-1]

    if len(raw) < 5:
        raise ValueError("数据长度不足(<5)")

    crc_calc = crc16_modbus(raw[:-2])
    crc_recv = raw[-2] | (raw[-1] << 8)
    if crc_calc != crc_recv:
        raise ValueError(f"CRC 校验失败 calc=0x{crc_calc:04X} recv=0x{crc_recv:04X}")

    addr = raw[0]
    func = raw[1]
    byte_len = raw[2]

    expect_len = 3 + byte_len + 2
    if len(raw) != expect_len:
        raise ValueError(f"长度不匹配 expect={expect_len} got={len(raw)} byte_len={byte_len}")

    payload = raw[3 : 3 + byte_len]
    regs = decode_registers(payload)
    hint = DATASET_BY_LENGTH.get(byte_len, f"未知数据集(0x{byte_len:02X})")

    return NotifyFrame(raw=raw, addr=addr, func=func, payload=payload, registers=regs, dataset_hint=hint)


def extract_frames_from_stream(buf: bytearray, max_frame_len: int = 1024) -> List[bytes]:
    """
    从字节流 buffer 切出完整帧。
    帧基本格式：[addr][func][byte_len][payload...][crc_lo][crc_hi]
    但部分 Telink 实现可能在 CRC 后额外追加 0x00 作为尾随字节（padding/terminator）。
    """
    out: List[bytes] = []
    while True:
        if len(buf) < 5:
            break

        byte_len = buf[2]
        base_len = 3 + byte_len + 2  # 不含尾随字节的标准长度

        if base_len < 5 or base_len > max_frame_len:
            del buf[0:1]
            continue

        if len(buf) < base_len:
            break

        # 检查是否存在 CRC 后的尾随 0x00（多 1 字节）
        frame_len = base_len
        if len(buf) >= base_len + 1 and buf[base_len] == 0x00:
            frame_len = base_len + 1

        raw = bytes(buf[:frame_len])
        del buf[:frame_len]
        out.append(raw)

    return out


__all__ = [
    "SPP_SERVICE_UUID",
    "SPP_WRITE_UUID",
    "SPP_NOTIFY_UUID",
    "build_read_request",
    "build_write_single",
    "chunk_payload",
    "parse_notify",
    "extract_frames_from_stream",
    "NotifyFrame",
]
