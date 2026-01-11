"""协议封装，与 mac 版保持一致。"""
from __future__ import annotations

from dataclasses import dataclass
from typing import List


SPP_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
SPP_WRITE_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
SPP_NOTIFY_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


def _crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def build_read_request(addr: int, words: int, slave: int = 1) -> bytes:
    payload = bytes((slave, 0x03, addr >> 8, addr & 0xFF, words >> 8, words & 0xFF))
    crc = _crc16(payload)
    return payload + bytes((crc & 0xFF, crc >> 8))


def build_write_single(addr: int, value: int, slave: int = 1) -> bytes:
    payload = bytes((slave, 0x06, addr >> 8, addr & 0xFF, value >> 8, value & 0xFF))
    crc = _crc16(payload)
    return payload + bytes((crc & 0xFF, crc >> 8))


def chunk_payload(data: bytes, mtu_payload: int = 20) -> List[bytes]:
    return [data[i : i + mtu_payload] for i in range(0, len(data), mtu_payload)]


DATASET_BY_LENGTH = {
    0x4C: "0xD000 单体电压",
    0x82: "0x2100 保护参数",
    0x32: "0xD026 SOC",
    0x18: "0xD115 状态",
    0x2A: "0xD100 保护记录",
}


@dataclass
class NotifyFrame:
    raw: bytes
    dataset_hint: str

    def csv_row(self, timestamp: float) -> List[str]:
        return [f"{timestamp:.3f}", self.dataset_hint, self.raw.hex()]


def parse_notify(raw: bytes) -> NotifyFrame:
    if len(raw) < 5:
        raise ValueError("数据过短")
    crc_calc = _crc16(raw[:-2])
    crc_recv = raw[-2] | (raw[-1] << 8)
    if crc_calc != crc_recv:
        raise ValueError("CRC 错误")
    byte_len = raw[2]
    hint = DATASET_BY_LENGTH.get(byte_len, f"未知数据集(0x{byte_len:02X})")
    return NotifyFrame(raw=raw, dataset_hint=hint)


__all__ = [
    "SPP_SERVICE_UUID",
    "SPP_WRITE_UUID",
    "SPP_NOTIFY_UUID",
    "build_read_request",
    "build_write_single",
    "chunk_payload",
    "parse_notify",
    "NotifyFrame",
]
