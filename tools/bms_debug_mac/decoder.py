from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple


FILL_NA = {61001, 0xEE49}  # 文档里常见“无此串/填充”值（你实际也见到 EE49）


def _mv_or_none(v: int) -> Optional[int]:
    return None if v in FILL_NA else v


def _temp01c_to_c(v: int) -> Optional[float]:
    # 文档规则：raw = 10*(T+40) => T = raw/10 - 40
    if v in FILL_NA:
        return None
    return v / 10.0 - 40.0


def _a10_to_a(v: int) -> float:
    # 文档：电流单位 A*10
    return v / 10.0


def _ah100_to_ah(v: int) -> float:
    # 常见：Ah*100
    return v / 100.0


def _u16_to_bits(v: int, n: int = 16) -> List[int]:
    return [i for i in range(n) if (v >> i) & 1]


@dataclass
class Decoded:
    name: str
    data: Dict[str, Any]


def decode_by_req(req_addr: int, req_words: int, regs: List[int]) -> Optional[Decoded]:
    key = (req_addr, req_words)
    if key == (0xD000, 0x26):
        return decode_d000(regs)
    if key == (0xD026, 0x19):
        return decode_d026(regs)
    if key == (0xD115, 0x0C):
        return decode_d115(regs)
    if key == (0xD100, 0x15):
        return decode_d100(regs)
    return None


def decode_d000(regs: List[int]) -> Decoded:
    # words=0x26 => 38 regs
    # 约定：0..31 cell1..32 mV
    cells = [_mv_or_none(v) for v in regs[0:32]]

    # 后续字段：给出“常见/可用”的默认解释；如果你文档确定不同，我们只要改索引即可
    tail = regs[32:]
    out: Dict[str, Any] = {
        "cells_mv": cells,
        "cells_valid_count": sum(1 for v in cells if v is not None),
        "tail_raw": tail,  # 保留原始尾部，便于你对照文档修索引
    }

    # 尝试按常见布局解析（不影响你后续修正）
    if len(tail) >= 6:
        vmax = _mv_or_none(tail[0])
        vmin = _mv_or_none(tail[1])
        vmax_pos = tail[2]
        vmin_pos = tail[3]
        delta = _mv_or_none(tail[4])
        pack_v_0p01v = tail[5]  # 常见：总压 V*100
        out.update({
            "vmax_mv": vmax,
            "vmin_mv": vmin,
            "vmax_pos_1based": vmax_pos,
            "vmin_pos_1based": vmin_pos,
            "delta_mv": delta,
            "pack_v": pack_v_0p01v / 100.0,
        })

    return Decoded("0xD000 单体电压/总压", out)


def decode_d026(regs: List[int]) -> Decoded:
    # words=0x19 => 25 regs
    # 先按文档常见：D026..D02B 触点温度1..6；D02C..D02E 环境1..3；D02F 散热片；D030 max；D031 min
    out: Dict[str, Any] = {"raw_regs": regs}

    if len(regs) >= 12:
        touch = [_temp01c_to_c(v) for v in regs[0:6]]
        env = [_temp01c_to_c(v) for v in regs[6:9]]
        heatsink = _temp01c_to_c(regs[9])
        tmax = _temp01c_to_c(regs[10])
        tmin = _temp01c_to_c(regs[11])
        out.update({
            "temp_touch_c": touch,
            "temp_env_c": env,
            "temp_heatsink_c": heatsink,
            "temp_max_c": tmax,
            "temp_min_c": tmin,
        })

    # 后续寄存器：不同厂家会放电流/SOC/SOH/容量/保护位/均衡位等
    # 我这里给“可用输出”：把剩余部分按索引输出，并对常见字段做猜测位（你以后用文档精确校正）
    rest = regs[12:]
    out["rest_regs_from_index12"] = rest

    # 常见猜测（不保证，但很实用：你调试时能快速看变化）
    # idx12: 电流(A*10)；idx13: SOC%；idx14: SOH%；idx15.. 容量等
    if len(rest) >= 1:
        out["current_a_guess"] = _a10_to_a(rest[0])
    if len(rest) >= 2:
        out["soc_percent_guess"] = rest[1]
    if len(rest) >= 3:
        out["soh_percent_guess"] = rest[2]
    if len(rest) >= 5:
        out["cap_now_ah_guess"] = _ah100_to_ah(rest[3])
        out["cap_full_ah_guess"] = _ah100_to_ah(rest[4])

    return Decoded("0xD026 SOC/温度/电流等", out)


def decode_d115(regs: List[int]) -> Decoded:
    # words=0x0C => 12 regs
    out: Dict[str, Any] = {"raw_regs": regs}

    # 常见：第0个是状态位图
    if regs:
        bits = _u16_to_bits(regs[0], 16)
        out["status_bits_set"] = bits
        # 给出常用 bit 名（你后续可按文档精确改）
        names = {
            0: "预充MOS/继电器(猜)",
            1: "充电MOS/继电器(猜)",
            2: "放电MOS/继电器(猜)",
            3: "分口CHG继电器(猜)",
            4: "分口DSG继电器(猜)",
            5: "同口主继电器(猜)",
            6: "加热继电器(猜)",
            7: "冷凝继电器(猜)",
        }
        out["status_bits_named_guess"] = [names.get(b, f"bit{b}") for b in bits]

    return Decoded("0xD115 运行状态(MOS/继电器等)", out)


_PROT_CODE_MAP = {
    0: "无",
    1: "一级单节过压(示例)",
    2: "二级单节过压(示例)",
    3: "一级单节欠压(示例)",
    4: "二级单节欠压(示例)",
    # 后续你把文档里的 1..39 全部映射进来就完美
}


def decode_d100(regs: List[int]) -> Decoded:
    # words=0x15 => 21 regs
    out: Dict[str, Any] = {"raw_regs": regs}

    # 常见：前面几个字节/寄存器包含 RTC（year-2000, mon, day, hour, min, sec）
    # 由于这里是 16bit 寄存器，很多协议会打包成：year|month, day|hour, min|sec（每个寄存器两字节）
    # 我这里用“尽可能解析 + 保留原始”策略
    if len(regs) >= 3:
        y_m = regs[0]
        d_h = regs[1]
        mi_s = regs[2]
        year = (y_m >> 8) & 0xFF
        month = y_m & 0xFF
        day = (d_h >> 8) & 0xFF
        hour = d_h & 0xFF
        minute = (mi_s >> 8) & 0xFF
        sec = mi_s & 0xFF
        out["rtc_guess"] = {
            "year": 2000 + year,
            "month": month,
            "day": day,
            "hour": hour,
            "minute": minute,
            "second": sec,
        }

    # 保护记录码：文档有 0..39，这里先把后面一段当作“记录区”，你后续按文档精确拆
    if len(regs) >= 8:
        codes = regs[3:7]
        out["prot_codes_guess"] = [{
            "code": c,
            "meaning": _PROT_CODE_MAP.get(c, f"未知({c})"),
        } for c in codes]

    return Decoded("0xD100 RTC/保护记录/异常标志", out)
