#include "st_flash_port.h"
#include "flash.h"
#include "watchdog.h"

void st_flash_read(u32 addr, u32 len, u8 *buf)
{
    flash_read_page(addr, len, buf);
}

void st_flash_write(u32 addr, u32 len, const u8 *buf)
{
    // Telink 的 flash_write_page 参数是 (addr, len, u8*)
    // 注意：buf 不能在 flash 常量区（你 flash 驱动注释也说了），最好是 RAM。
    flash_write_page(addr, len, (u8*)buf);
}

void st_flash_erase_sector(u32 addr)
{
    wd_clear();
    flash_erase_sector(addr);
}

int st_flash_is_voltage_safe(void)
{
    // 默认认为安全；你如果有电压检测，在这里做判断
    return 1;
}
