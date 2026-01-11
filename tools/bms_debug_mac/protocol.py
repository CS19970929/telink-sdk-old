"""协议封装层：负责CRC、封包/解包与notify解析。"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, List, Optional, Tuple


# Telink SPP UUID（低功耗蓝牙 128bit UUID）
SPP_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
SPP_WRITE_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # 手机→设备（Write）
SPP_NOTIFY_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # 设备→手机（Notify）

DEFAULT_SLAVE_ID = 0x01
FUNC_READ = 0x03
FUNC_WRITE_SINGLE = 0x06

# 依据 docs/codex/07_BMS_BLE_PROTOCOL_SPEC.md，按 payload 字节数推断数据集
DATASET_BY_LENGTH = {
    0x4C: "0xD000 单体电压/总压",
    0x82: "0x2100 保护参数",
    0x32: "0xD026 SOC/温度",
    0x18: "0xD115 运行状态",
    0x2A: "0xD100 保护记录",
}


def _crc16(data: bytes) -> int:
    """Modbus RTU CRC16，低字节在前。"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def _append_crc(data: bytes) -> bytes:
    crc = _crc16(data)
    return data + bytes((crc & 0xFF, crc >> 8))


def build_read_request(addr: int, words: int, slave: int = DEFAULT_SLAVE_ID) -> bytes:
    """构造功能码0x03的读寄存器帧。"""
    payload = bytes((slave, FUNC_READ, addr >> 8, addr & 0xFF, words >> 8, words & 0xFF))
    return _append_crc(payload)


def build_write_single(addr: int, value: int, slave: int = DEFAULT_SLAVE_ID) -> bytes:
    """构造功能码0x06的写单寄存器帧。"""
    payload = bytes((slave, FUNC_WRITE_SINGLE, addr >> 8, addr & 0xFF, value >> 8, value & 0xFF))
    return _append_crc(payload)


def chunk_payload(data: bytes, mtu_payload: int = 20) -> List[bytes]:
    """按 MTU 拆包，Telink 默认 20 字节。"""
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


def _decode_registers(payload: bytes) -> List[int]:
    regs: List[int] = []
    for i in range(0, len(payload), 2):
        if i + 1 < len(payload):
            regs.append((payload[i] << 8) | payload[i + 1])
    return regs


def parse_notify(raw: bytes) -> NotifyFrame:
    if len(raw) < 5:
        raise ValueError("数据长度不足")
    crc_calc = _crc16(raw[:-2])
    crc_recv = raw[-2] | (raw[-1] << 8)
    if crc_calc != crc_recv:
        raise ValueError("CRC 校验失败")
    addr = raw[0]
    func = raw[1]
    byte_len = raw[2]
    payload = raw[3 : 3 + byte_len]
    regs = _decode_registers(payload)
    hint = DATASET_BY_LENGTH.get(byte_len, f"未知数据集(0x{byte_len:02X})")
    return NotifyFrame(raw=raw, addr=addr, func=func, payload=payload, registers=regs, dataset_hint=hint)


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
