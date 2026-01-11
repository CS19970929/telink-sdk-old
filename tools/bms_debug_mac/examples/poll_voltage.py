"""示例：以脚本方式轮询 0xD000 电压寄存器。"""
import asyncio
import os
import sys
from bleak import BleakClient

sys.path.append(os.path.dirname(os.path.dirname(__file__)))
from protocol import SPP_NOTIFY_UUID, SPP_WRITE_UUID, build_read_request, parse_notify  # noqa: E402


DEVICE_ADDRESS = os.environ.get("BMS_BLE_ADDR", "")


async def run() -> None:
    if not DEVICE_ADDRESS:
        print("请通过环境变量 BMS_BLE_ADDR 指定设备地址，例如 BMS_BLE_ADDR=xx python poll_voltage.py")
        return

    async with BleakClient(DEVICE_ADDRESS) as client:
        await client.start_notify(SPP_NOTIFY_UUID, lambda _, data: print(parse_notify(bytes(data))))
        await client.write_gatt_char(SPP_WRITE_UUID, build_read_request(0xD000, 0x0038), response=False)
        await asyncio.sleep(5)


if __name__ == "__main__":
    asyncio.run(run())
