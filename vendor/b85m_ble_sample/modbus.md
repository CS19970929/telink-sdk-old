
#define MB_RX_BUF_MAX 256
static u8 g_mb_rx[MB_RX_BUF_MAX];
static u16 g_mb_rx_len = 0;

#define MB_SLAVE_ADDR 0x01

// 你现在用 notify_big_packet 发回去
extern ble_sts_t notify_big_packet(u16 conn, u16 handle, u8 *data, u16 len);
#define APP_NOTIFY_HANDLE SPP_CLIENT_TO_SERVER_DP_H

static int mb_req_expected_len(const u8 *buf, u16 len)
{
    if (len < 2)
        return 0;
    u8 fc = buf[1];
    switch (fc)
    {
    case 0x03:
    case 0x04:
    case 0x06:
        return 8;
    case 0x10:
        if (len < 7)
            return 0; // 需要到 byteCount 字段
        return 9 + buf[6] + 2;
    default:
        // 其他功能码你后续再扩展
        return 0;
    }
}

static int mb_try_extract_one(u8 *out_frame, u16 *out_len)
{
    // 返回：1=成功取出一帧；0=还不够；<0=丢弃了一些字节后继续
    if (g_mb_rx_len < 4)
        return 0;

    // 寻找起始位置（这里你也可以只接受自己从站地址，比如 0x01）
    for (u16 start = 0; start < g_mb_rx_len; start++)
    {
        // 至少要有 addr+fc+...+crc(2)
        if (g_mb_rx_len - start < 4)
            break;

        // 计算期望长度
        int need = mb_req_expected_len(&g_mb_rx[start], g_mb_rx_len - start);
        if (need == 0)
        {
            // 无法判定长度：继续往后找
            continue;
        }

        if ((g_mb_rx_len - start) < (u16)need)
        {
            // 数据不够，等下次
            // 但如果 start>0，说明前面是垃圾，先丢掉
            if (start > 0)
            {
                memmove(g_mb_rx, g_mb_rx + start, g_mb_rx_len - start);
                g_mb_rx_len -= start;
                return -1;
            }
            return 0;
        }

        // CRC 校验
        u16 crc_rx = (u16)g_mb_rx[start + need - 2] | ((u16)g_mb_rx[start + need - 1] << 8);
        u16 crc_calc = modbus_crc16(&g_mb_rx[start], need - 2);
        if (crc_rx != crc_calc)
        {
            // 这不是有效帧，丢掉 start 位置的 1 字节，继续
            memmove(g_mb_rx, g_mb_rx + start + 1, g_mb_rx_len - (start + 1));
            g_mb_rx_len -= (start + 1);
            return -2;
        }

        // 有效帧
        memcpy(out_frame, &g_mb_rx[start], need);
        *out_len = (u16)need;

        // 从 buffer 移除该帧（以及前面垃圾）
        memmove(g_mb_rx, g_mb_rx + start + need, g_mb_rx_len - (start + need));
        g_mb_rx_len -= (start + need);
        return 1;
    }

    // 没找到有效帧：为了避免永远堆垃圾，丢弃一个字节
    memmove(g_mb_rx, g_mb_rx + 1, g_mb_rx_len - 1);
    g_mb_rx_len -= 1;
    return -3;
}


// ---- 你的寄存器读写接口（你需要接到真实BMS数据/KV/LOG）----
static int mb_read_holding(u16 reg, u16 num, u16 *out) // 0=OK，<0=异常
{
    // 示例：reg=0x0000.. 你自己映射
    // out[i] 是16-bit寄存器
    for (u16 i = 0; i < num; i++)
    {
        u16 r = reg + i;
        switch (r)
        {
        case 0x0001:
        { // SOC x1 (0..100)
            extern int simulate_soc(void);
            out[i] = (u16)simulate_soc();
        }
        break;

        case 0x0020:
        { // pack voltage mV (示例)
            out[i] = 3500;
        }
        break;

        default:
            return -2; // ILLEGAL DATA ADDRESS
        }
    }
    return 0;
}

static int mb_write_single(u16 reg, u16 val)
{
    // TODO: 写入你的参数系统（KV + 生效）
    (void)reg;
    (void)val;
    return 0;
}

static int mb_write_multi(u16 reg, u16 num, const u16 *vals)
{
    // TODO: 写入多个寄存器
    (void)reg;
    (void)num;
    (void)vals;
    return 0;
}

// ---- 组 Modbus 异常响应 ----
static u16 mb_build_exception(u8 *rsp, u8 addr, u8 fc, u8 excode)
{
    rsp[0] = addr;
    rsp[1] = fc | 0x80;
    rsp[2] = excode;
    u16 crc = modbus_crc16(rsp, 3);
    rsp[3] = (u8)(crc & 0xFF);
    rsp[4] = (u8)(crc >> 8);
    return 5;
}

// ---- 处理一帧 Modbus 请求，生成响应 ----
static u16 mb_handle_request_build_response(const u8 *req, u16 req_len, u8 *rsp, u16 rsp_max)
{
    (void)req_len;
    u8 addr = req[0];
    u8 fc = req[1];

    // addr 不匹配可忽略（或广播地址0也可支持写命令）
    if (addr != MB_SLAVE_ADDR && addr != 0x00)
    {
        return 0; // 不回应
    }

    if (fc == 0x03 || fc == 0x04)
    {
        if (rsp_max < 5)
            return 0;
        u16 reg = ((u16)req[2] << 8) | req[3];
        u16 num = ((u16)req[4] << 8) | req[5];
        if (num == 0 || num > 64)
        {
            return mb_build_exception(rsp, addr, fc, 0x03); // ILLEGAL DATA VALUE
        }

        u16 tmp[64];
        int r = mb_read_holding(reg, num, tmp);
        if (r < 0)
        {
            return mb_build_exception(rsp, addr, fc, 0x02); // ILLEGAL DATA ADDRESS
        }

        u16 need = (u16)(3 + num * 2 + 2);
        if (need > rsp_max)
        {
            return mb_build_exception(rsp, addr, fc, 0x04); // SLAVE DEVICE FAILURE
        }

        rsp[0] = addr;
        rsp[1] = fc;
        rsp[2] = (u8)(num * 2);
        for (u16 i = 0; i < num; i++)
        {
            rsp[3 + i * 2] = (u8)(tmp[i] >> 8);
            rsp[4 + i * 2] = (u8)(tmp[i] & 0xFF);
        }
        u16 crc = modbus_crc16(rsp, 3 + num * 2);
        rsp[3 + num * 2] = (u8)(crc & 0xFF);
        rsp[4 + num * 2] = (u8)(crc >> 8);
        return (u16)(5 + num * 2);
    }
    else if (fc == 0x06)
    {
        u16 reg = ((u16)req[2] << 8) | req[3];
        u16 val = ((u16)req[4] << 8) | req[5];
        int r = mb_write_single(reg, val);
        if (r < 0)
        {
            return mb_build_exception(rsp, addr, fc, 0x02);
        }
        // 正常响应=回显请求前6字节+crc
        if (rsp_max < 8)
            return 0;
        memcpy(rsp, req, 6);
        u16 crc = modbus_crc16(rsp, 6);
        rsp[6] = (u8)(crc & 0xFF);
        rsp[7] = (u8)(crc >> 8);
        return 8;
    }
    else if (fc == 0x10)
    {
        u16 reg = ((u16)req[2] << 8) | req[3];
        u16 num = ((u16)req[4] << 8) | req[5];
        u8 bc = req[6];
        if (num == 0 || num > 64 || bc != num * 2)
        {
            return mb_build_exception(rsp, addr, fc, 0x03);
        }
        const u8 *p = &req[7];
        u16 vals[64];
        for (u16 i = 0; i < num; i++)
        {
            vals[i] = ((u16)p[i * 2] << 8) | p[i * 2 + 1];
        }
        int r = mb_write_multi(reg, num, vals);
        if (r < 0)
        {
            return mb_build_exception(rsp, addr, fc, 0x02);
        }
        // 正常响应：addr fc regHi regLo numHi numLo + crc
        if (rsp_max < 8)
            return 0;
        rsp[0] = addr;
        rsp[1] = fc;
        rsp[2] = req[2];
        rsp[3] = req[3];
        rsp[4] = req[4];
        rsp[5] = req[5];
        u16 crc = modbus_crc16(rsp, 6);
        rsp[6] = (u8)(crc & 0xFF);
        rsp[7] = (u8)(crc >> 8);
        return 8;
    }

    return mb_build_exception(rsp, addr, fc, 0x01); // ILLEGAL FUNCTION
}

int module_onReceiveData(void *para)
{
    rf_packet_att_write_t *p = (rf_packet_att_write_t *)para;
    u16 len = p->l2capLen - 3;
    if (len == 0)
        return 0;

    // 1) 追加到 Modbus RX buffer
    if (g_mb_rx_len + len > MB_RX_BUF_MAX)
    {
        g_mb_rx_len = 0; // 溢出直接清
        return 0;
    }
    memcpy(&g_mb_rx[g_mb_rx_len], (const u8 *)&p->value, len);
    g_mb_rx_len += len;

    // 2) 解出一帧就处理一帧（可能一次write里有多帧）
    u8 req[MB_RX_BUF_MAX];
    u16 req_len = 0;

    while (1)
    {
        int r = mb_try_extract_one(req, &req_len);
        if (r == 0)
            break; // 数据还不够
        if (r < 0)
            continue; // 丢了一些字节，继续找

        // 3) 处理并生成响应
        static u8 rsp[256];
        u16 rsp_len = mb_handle_request_build_response(req, req_len, rsp, sizeof(rsp));
        if (rsp_len > 0 && device_in_connection_state)
        {
            notify_big_packet(BLS_CONN_HANDLE, APP_NOTIFY_HANDLE, rsp, rsp_len);
        }

        // 你原来的 rev_master 标志可以不用了（除非你还想触发别的）
        rev_master = true;
    }

    return 0;
}
