"""快速轮询示例（Windows PowerShell 可运行）。"""
import asyncio
import os
import sys

sys.path.append(os.path.dirname(os.path.dirname(__file__)))

from bleak import BleakClient  # type: ignore
from protocol import SPP_NOTIFY_UUID, SPP_WRITE_UUID, build_read_request


ADDR = os.environ.get("BMS_BLE_ADDR")


async def main() -> None:
    if not ADDR:
        print("请设置环境变量 BMS_BLE_ADDR=xx:xx 后再运行")
        return
    async with BleakClient(ADDR) as client:
        await client.start_notify(SPP_NOTIFY_UUID, lambda _, data: print(bytes(data).hex()))
        await client.write_gatt_char(SPP_WRITE_UUID, build_read_request(0xD000, 0x0038), response=False)
        await asyncio.sleep(3)


if __name__ == "__main__":
    asyncio.run(main())
