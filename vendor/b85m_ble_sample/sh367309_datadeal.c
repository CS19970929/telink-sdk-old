#include "sh367309_datadeal.h"
#include "tl_common.h"
#include "drivers.h"
#include "conf.h"

int AFE_PARAM_WRITE_Flag = 1;
int AFE_ResetFlag = 0;

u16 iSheldTemp_10K_NTC[141] = {20375, 19204, 18115, 17100, 16152, 15266, 14437, 13661, 12934, 12251,
                               11611, 11008, 10442, 9909, 9407, 8935, 8489, 8068, 7672, 7297,
                               6943, 6608, 6292, 5993, 5710, 5442, 5188, 4948, 4720, 4504,
                               4300, 4105, 3921, 3746, 3580, 3422, 3272, 3130, 2994, 2866,
                               2751, 2627, 2516, 2410, 2310, 2214, 2123, 2036, 1953, 1874,
                               1801, 1726, 1658, 1592, 1530, 1470, 1413, 1358, 1306, 1256,
                               1209, 1163, 1119, 1078, 1038, 1000, 963, 928, 894, 862,
                               831, 801, 773, 746, 719, 694, 670, 647, 625, 604,
                               583, 563, 544, 526, 509, 492, 476, 460, 445, 431,
                               416, 403, 390, 378, 366, 355, 343, 333, 322, 312,
                               303, 294, 285, 276, 268, 260, 252, 244, 237, 230,
                               224, 217, 211, 205, 199, 193, 188, 182, 177, 172,
                               167, 163, 158, 154, 150, 146, 142, 138, 134, 131,
                               127, 124, 120, 117, 114, 111, 108, 106, 103, 100,
                               98};

// 鍓�26涓瘎瀛樺櫒榛樿鍙傛暟
u8 ucMTPBuffer[26] = {
    BYTE_00H_SCONF1, BYTE_01H_SCONF2, BYTE_02H_OVT_LDRT_OVH, BYTE_03H_OVL, BYTE_04H_UVT_OVRH,
    BYTE_05H_OVRL, BYTE_06H_UV, BYTE_07H_UVR, BYTE_08H_BALV, BYTE_09H_PREV,
    BYTE_0AH_L0V, BYTE_0BH_PFV, BYTE_0CH_OCD1V_OCD1T, BYTE_0DH_OCD2V_OCD2T, BYTE_0EH_SCV_SCT,
    BYTE_0FH_OCCV_OCCT, BYTE_10H_MOST_OCRT_PFT, BYTE_11H_OTC, BYTE_12H_OTCR, BYTE_13H_UTC,
    BYTE_14H_UTCR, BYTE_15H_OTD, BYTE_16H_OTDR, BYTE_17H_UTD, BYTE_18H_UTDR,
    BYTE_19H_TR};

const u16 AFE_OCD1V_OCCV[16] = {20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140, 160, 180, 200};                    // 涓�绾ф斁鐢佃繃娴佸拰鍏呯數杩囨祦锛屽崟浣峬v
const u16 AFE_SCV[16] = {50, 80, 110, 140, 170, 200, 230, 260, 290, 320, 350, 400, 500, 600, 800, 1000};                    // 鐭矾淇濇姢鐢靛帇锛屽崟浣峬v
const u16 AFE_OVT_UVT[16] = {100, 200, 300, 400, 600, 800, 1000, 2000, 3000, 4000, 6000, 8000, 10000, 20000, 30000, 40000}; // 杩囧帇浣庡帇寤舵椂鏃堕棿銆傚崟浣峬s
const u16 AFE_SCT[16] = {0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960};                      // 鐭矾寤舵椂,鍗曚綅us銆�
const u16 AFE_OCD1T[16] = {50, 100, 200, 400, 600, 800, 1000, 2000, 4000, 6000, 8000, 10000, 15000, 20000, 30000, 40000};   // 鏀剧數杩囨祦1寤舵椂銆傚崟浣峬s
const u16 AFE_OCCT_OCD2T[16] = {10, 20, 40, 60, 80, 100, 200, 400, 600, 800, 1000, 2000, 4000, 8000, 10000, 20000};         // 鏀剧數杩囨祦2鍜屽厖鐢佃繃娴佸欢鏃躲�傚崟浣峬s

const u8 CRC8Table[] = { // 120424-1			CRC Table
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3};

AFE_ROM_PARAMETERS_TypeDef AFE_ROM_PARAMETERS_Struction = {0};
AFE_Parameters_RS485_Typedef AFE_Parameters_RS485_Struction = AFE_PARAMETERS_RS485_STRUCTION_DEFAULT;
SH367309_REG_STORE SH367309_Reg_Store;

// u8 CRC8cal(u8 *p, u8 Length)
// { // look-up table calculte CRC
//     u8 crc8 = 0;

//     for (; Length > 0; Length--)
//     {
//         crc8 = CRC8Table[crc8 ^ *p];
//         p++;
//     }

//     return (crc8);
// }
u8 CRC8cal(const u8 *data, u32 len)
{
    u8 crc = 0x00;
    for (u32 i = 0; i < len; i++) {
        crc ^= data[i];
        for (u8 b = 0; b < 8; b++) {
            if (crc & 0x80) crc = (u8)((crc << 1) ^ 0x07);
            else           crc = (u8)(crc << 1);
        }
    }
    return crc;
}
void Delay1ms(u8 delaycnt)
{
    WaitMs(delaycnt);
}
u8 TwiWrite(u8 SlaveID, u16 WrAddr, u8 Length, u8 *WrBuf)
{
    u8 i;
    u8 TempBuf[4];
    u8 result = 0;

    TempBuf[0] = SlaveID;
    TempBuf[1] = (u8)WrAddr;
    TempBuf[2] = *WrBuf;
    TempBuf[3] = CRC8cal(TempBuf, 3);

    // i2c_write_series(((u16)WrAddr << 8) | Length, 2, (unsigned char *)WrBuf, Length + 1);
    // i2c_write_series(((u16)WrAddr << 8) | Length, 2, (unsigned char *)TempBuf[2], 2);
    // i2c_write_series(((u16)WrAddr << 8), 1, (unsigned char *)TempBuf[2], 2);
    i2c_write_series(WrAddr, 1, (unsigned char *)&TempBuf[2], 2);
}

int Choose_Right_Value(u16 cur_Value, const u16 *AFE_list)
{
    int i = 0;
    for (i = 0; i < 15; i++)
    {
        if (cur_Value <= AFE_list[i])
        {
            break;
        }
    }
    return i;
}

u8 System_ERROR_UserCallback(enum SYSTEM_ERROR_COMMAND errorCode)
{
    printf("system error code: %d", errorCode);
    return 0;
}

u8 MTPWrite(u8 WrAddr, u8 Length, u8 *WrBuf)
{
    u8 result;
    u8 i;
    Feed_IWatchDog;

    for (i = 0; i < Length; i++)
    {
        // result = TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
        TwiWrite(AFE_ID, WrAddr, 1, WrBuf);

        WrAddr++;
        WrBuf++;
        Delay1ms(1);
    }
}
#if 0
u8 MTPWrite(u8 WrAddr, u8 Length, u8 *WrBuf)
{
	u8 result;
	u8 i;
	Feed_IWatchDog;

	for (i = 0; i < Length; i++)
	{
		result = TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
		if (!result)
		{
			Delay1ms(1);
			result = TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
			if (!result)
			{
				break;
			}
		}
		WrAddr++;
		WrBuf++;
		Delay1ms(1);
	}

	if (!result)
	{
		System_ERROR_UserCallback(ERROR_AFE1);
	}

	return result;
}
#endif

#if 0
u8 TwiWrite(u8 SlaveID, u16 WrAddr, u8 Length, u8 *WrBuf)
{
    u8 i;
    u8 TempBuf[4];
    u8 result = 0;

    TempBuf[0] = SlaveID;
    TempBuf[1] = (u8)WrAddr;
    TempBuf[2] = *WrBuf;
    TempBuf[3] = CRC8cal(TempBuf, 3);

    if (Length > 0)
    {
        TwiStart();

        if (!TwiSendData(SlaveID, 1))
        { // Send Slave ID
            goto WrErr;
        }

        if (TwiSendData(WrAddr, 0))
        { // Send Write Address(Low 8bit)
            result = 1;
            for (i = 0; i < Length; i++)
            {
                if (TwiSendData(*WrBuf, 0))
                { // Send Write Data
                    WrBuf++;
                }
                else
                {
                    result = 0;
                    break;
                }
            }
            if (!TwiSendData(TempBuf[3], 0))
            { // write CRC
                result = 0;
            }
        }
    WrErr:
        TwiStop();
    }

    return result;
}
#endif
/*******************************************************************************
Function: TwiRead()
Description:  read multi bytes
Input: SlaveID--Slave Address
          RdAddr--register addr
          Length--read data length
          *RdBuf--data buffer
Output: result:1--OK
               0--Error
Others:
********************************************************************************/
u8 TwiRead(u8 SlaveID, u16 RdAddr, u8 Length, u8 *RdBuf)
{
    // todo 鍚庨潰鍔燾rc,rdbuf澶氬姞涓�涓�
    i2c_read_series(((u16)RdAddr << 8) | Length, 2, (unsigned char *)RdBuf, Length + 1);
    // printf("TwiRead\n");
    // array_printf(RdBuf, Length);
}
#if 0
u8 TwiRead(u8 SlaveID, u16 RdAddr, u8 Length, u8 *RdBuf)
{
    u8 i;
    u8 result = 0;
    u8 TempBuf[46];
    u8 RdCrc = 0;

    TempBuf[0] = SlaveID;
    TempBuf[1] = (u8)RdAddr;
    TempBuf[2] = Length;
    TempBuf[3] = SlaveID | 0x01;

    if (Length > 0)
    {
        TwiStart();

        if (!TwiSendData(SlaveID, 1))
        { // Send Slave ID
            goto RdErr;
        }

        if (!TwiSendData(RdAddr, 0))
        { // Send Read Address(Low 8bit)
            goto RdErr;
        }

        if (!TwiSendData(Length, 0))
        {
            goto RdErr;
        }

        TwiReStart();

        if (TwiSendData(SlaveID | 0x1, 0))
        { // Send Slave ID
            result = 1;
            for (i = 0; i < Length + 1; i++)
            {
                if (i == Length)
                {
                    RdCrc = TwiGetData(0); // Get Data
                }
                else
                {
                    TempBuf[4 + i] = TwiGetData(1); // Get Data
                }
            }

            if (RdCrc != CRC8cal(TempBuf, 4 + Length))
            {
                result = 0;
            }
            else
            {
                for (i = 0; i < Length; i++)
                {
                    *RdBuf = TempBuf[4 + i];
                    RdBuf++;
// 涓嬮潰鐨勯棶棰樺湪浜庯紝濡傛灉浼犺繘鏉ョ殑鏁板�间笉鏄�16浣嶏紝鏄�8浣嶏紝鍙堟湁闂銆�
// 杩樻槸澶栭儴鑷繁鍐欎竴涓ぇ灏忕杞崲鍑芥暟鑷繁鐪嬫儏鍐垫槸鍚﹀鐞�
#if 0
                    //闂鍦ㄤ簬030鐨勫皬绔瓨鍌�
					if(i == Length - 1 && Length%2) {
						*(RdBuf) = TempBuf[4+i];		//褰撳彇濂囨暟涓暟鎹紝鏈�鍚庝竴涓紝娌″舰鎴愬鐨勯偅涓闆堕浂鐨勬暟鎹�
					}
					else {
	                    if(i%2) {
							*(--RdBuf) = TempBuf[4+i];
							RdBuf += 2;
	                    }
						else {
							*(++RdBuf) = TempBuf[4+i];
						}
					}
#endif
                }
            }
        }

    RdErr:
        TwiStop();
    }

    return result;
}
#endif

u8 MTPRead(u8 RdAddr, u8 Length, u8 *RdBuf)
{
    TwiRead(AFE_ID, RdAddr, Length, RdBuf);
    return 1;
}
#if 0
u8 MTPRead(u8 RdAddr, u8 Length, u8 *RdBuf)
{
    u8 result = 1;

    Feed_IWatchDog;

    /*
    if(System_ErrFlag.u8ErrFlag_Com_AFE1) {
        result = 0;
    }
    else {
        result = TwiRead(AFE_ID, RdAddr, Length, RdBuf);
        if(!result) {
            result = TwiRead(AFE_ID, RdAddr, Length, RdBuf);
        }
    }
    */

    result = TwiRead(AFE_ID, RdAddr, Length, RdBuf);
    if (!result)
    {
        result = TwiRead(AFE_ID, RdAddr, Length, RdBuf);
    }

    if (!result)
    {
        System_ERROR_UserCallback(ERROR_AFE1);
    }
    return result;
}
#endif

u8 MTPWriteROM(u8 WrAddr, u8 Length, u8 *WrBuf)
{
    u8 result;
    u8 i;

    TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
    // for (i = 0; i < Length; i++)
    // {
    //     Feed_IWatchDog;
    //     // result = TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
    //     TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
    //     WrAddr++;
    //     WrBuf++;
    //     Delay1ms(40);
    // }
    // todo 怎么确认硬件i2c成功
}
#if 0
u8 MTPWriteROM(u8 WrAddr, u8 Length, u8 *WrBuf)
{
    u8 result;
    u8 i;

    for (i = 0; i < Length; i++)
    {
        Feed_IWatchDog;
        result = TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
        if (!result)
        {
            Delay1ms(40);
            result = TwiWrite(AFE_ID, WrAddr, 1, WrBuf);
            if (!result)
            {
                break;
            }
        }
        WrAddr++;
        WrBuf++;
        Delay1ms(40);
    }

    if (!result)
    {
        System_ERROR_UserCallback(ERROR_AFE1);
    }

    return result;
}
#endif

int g_u32CS_Res_AFE;
void Refresh_Parameters(void)
{
    int i = 0;
    int temp = 0;
    u8 TR = 0;
    u16 AFE_TEMPERATURE[8] = {0}; // 娓╁害锛屾憚姘忓害+40锛岋紙0搴︾殑鍊间负40锛�

    // 璇�309鐨凾R锛岄『渚挎妸AFE榛樿鍊奸厤缃紶鍒癆FE_ROM_PARAMETERS_Struction缁撴瀯浣�(#define绫诲瀷)銆�
    if (MTPRead(0x19, 1, &TR))
    {
        SH367309_Reg_Store.TR_ResRef = 680 + 5 * (TR & 0x7F);
        ucMTPBuffer[25] = TR & 0x7F;

/* 鎶婇粯璁ょ殑鏁版嵁鏀惧湪鍙傛暟缁撴瀯浣撻噷 */
#ifdef _SLEEP_WITH_CURRENT
        // 浼戠湢甯︾數鏆備笖涓嶉渶瑕侀鍏呭姛鑳�
        // 瀹忓畾涔夊皯涓嫭鍙凤紝鍑轰簨浜嗭紝璁＄畻浼樺厛绾ч棶棰�
        ucMTPBuffer[1] = (BYTE_01H_SCONF2) & 0xF3;
#endif
        memcpy((u8 *)&AFE_ROM_PARAMETERS_Struction, ucMTPBuffer, 26);
    }
    g_u32CS_Res_AFE = 2 * 1000 / 2;
    // g_u32CS_Res_AFE = ((u32)g_tParam.other.u16Sys_CS_Res_Num * 1000) / g_tParam.other.u16Sys_CS_Res;

    /* 涓叉暟 */
    AFE_ROM_PARAMETERS_Struction.m00H_01H.CN = 10 % 16;

#define __CTLC__
#ifdef __CTLC__
    AFE_ROM_PARAMETERS_Struction.m00H_01H.CTLC = (0xff >> 6);
#else
    AFE_ROM_PARAMETERS_Struction.m00H_01H.CTLC = (0x00 >> 6);
#endif
    // AFE_ROM_PARAMETERS_Struction.m00H_01H = AFE_ROM_PARAMETERS_Struction.m00H_01H | 0x0c;
    // todo ctlc閰嶇疆 OCD2閰嶇疆鍐欐

    /* 鍏呯數杩囧帇 */
    AFE_ROM_PARAMETERS_Struction.m02H_03H.OVH = ((AFE_Parameters_RS485_Struction.u16VcellOvp.curValue / 5) >> 8) & 0x3;
    AFE_ROM_PARAMETERS_Struction.m02H_03H.OVL = (AFE_Parameters_RS485_Struction.u16VcellOvp.curValue / 5) & 0x00FF;
    /* 鍏呯數杩囧帇寤舵椂鏃堕棿 */
    temp = AFE_Parameters_RS485_Struction.u16VcellOvp_Filter.curValue * 10;
    AFE_ROM_PARAMETERS_Struction.m02H_03H.OVT = Choose_Right_Value(temp, AFE_OVT_UVT);
    /* 鍏呯數杩囧帇鎭㈠ */
    AFE_ROM_PARAMETERS_Struction.m04H_05H.OVRH = ((AFE_Parameters_RS485_Struction.u16VcellOvp_Rcv.curValue / 5) >> 8) & 0x3;
    AFE_ROM_PARAMETERS_Struction.m04H_05H.OVRL = (AFE_Parameters_RS485_Struction.u16VcellOvp_Rcv.curValue / 5) & 0x00FF;

    /*鏀剧數浣庡帇寤舵椂鏃堕棿 */
    temp = AFE_Parameters_RS485_Struction.u16VcellUvp_Filter.curValue * 10;
    AFE_ROM_PARAMETERS_Struction.m04H_05H.UVT = Choose_Right_Value(temp, AFE_OVT_UVT);
    /* 鏀剧數浣庡帇 */
    AFE_ROM_PARAMETERS_Struction.m06H_07H.UV = (AFE_Parameters_RS485_Struction.u16VcellUvp.curValue / 20) & 0x00FF;
    /* 鏀剧數浣庡帇鎭㈠ */
    AFE_ROM_PARAMETERS_Struction.m06H_07H.UVR = (AFE_Parameters_RS485_Struction.u16VcellUvp_Rcv.curValue / 20) & 0x00FF;

    // temp = AFE_Parameters_RS485_Struction.u16IdsgOcp_Second.curValue * 100 / g_u32CS_Res_AFE; // 褰撳墠瀵瑰簲澶氬皯mv
    // AFE_ROM_PARAMETERS_Struction.m0CH_0DH.OCD1V = Choose_Right_Value(temp, AFE_OCD1V_OCCV);
    // temp = AFE_Parameters_RS485_Struction.u16IdsgOcp_Filter_Second.curValue * 10; // 褰撳墠瀵瑰簲澶氬皯ms
    // AFE_ROM_PARAMETERS_Struction.m0CH_0DH.OCD1T = Choose_Right_Value(temp, AFE_OCD1T);
    AFE_ROM_PARAMETERS_Struction.m0CH_0DH.OCD1V = 0;
    AFE_ROM_PARAMETERS_Struction.m0CH_0DH.OCD1T = 0;

    /* 鍏呯數杩囨祦 */
    temp = AFE_Parameters_RS485_Struction.u16IchgOcp_Second.curValue * 100 / g_u32CS_Res_AFE; // 褰撳墠瀵瑰簲澶氬皯mv
    AFE_ROM_PARAMETERS_Struction.m0EH_0FH.OCCV = Choose_Right_Value(temp, AFE_OCD1V_OCCV);
    /* 鍏呯數杩囨祦婊ゆ尝鏃堕棿 */
    temp = AFE_Parameters_RS485_Struction.u16IchgOcp_Filter_Second.curValue * 10; // 褰撳墠瀵瑰簲澶氬皯ms
    AFE_ROM_PARAMETERS_Struction.m0EH_0FH.OCCT = Choose_Right_Value(temp, AFE_OCCT_OCD2T);

    /* 鐭矾寤舵椂 */
    temp = AFE_Parameters_RS485_Struction.u16CBC_DelayT.curValue;
    AFE_ROM_PARAMETERS_Struction.m0EH_0FH.SCT = Choose_Right_Value(temp, AFE_SCT);
    /* 鐭矾鐢靛帇 */
    temp = AFE_Parameters_RS485_Struction.u16CBC_Cur_DSG.curValue * 1000 / g_u32CS_Res_AFE; // 褰撳墠瀵瑰簲澶氬皯mv
    AFE_ROM_PARAMETERS_Struction.m0EH_0FH.SCV = Choose_Right_Value(temp, AFE_SCV);

    /* 鎵�鏈夌殑娓╁害淇濇姢 */
    AFE_TEMPERATURE[0] = AFE_Parameters_RS485_Struction.u16TChgOTp.curValue / 10;        /* 鍏呯數楂樻俯淇濇姢 */
    AFE_TEMPERATURE[1] = AFE_Parameters_RS485_Struction.u16TChgOTp_Rcv.curValue / 10;    /* 鍏呯數楂樻俯淇濇姢鎭㈠ */
    AFE_TEMPERATURE[2] = AFE_Parameters_RS485_Struction.u16TchgUTp.curValue / 10;        /* 鍏呯數浣庢俯淇濇姢 */
    AFE_TEMPERATURE[3] = AFE_Parameters_RS485_Struction.u16TchgUTp_Rcv.curValue / 10;    /* 鍏呯數浣庢俯淇濇姢鎭㈠ */
    AFE_TEMPERATURE[4] = AFE_Parameters_RS485_Struction.u16TdischgOTp.curValue / 10;     /* 鏀剧數楂樻俯淇濇姢 */
    AFE_TEMPERATURE[5] = AFE_Parameters_RS485_Struction.u16TdischgOTp_Rcv.curValue / 10; /* 鏀剧數楂樻俯淇濇姢鎭㈠ */
    AFE_TEMPERATURE[6] = AFE_Parameters_RS485_Struction.u16TdischgUTp.curValue / 10;     /* 鏀剧數浣庢俯淇濇姢 */
    AFE_TEMPERATURE[7] = AFE_Parameters_RS485_Struction.u16TdischgUTp_Rcv.curValue / 10; /* 鏀剧數浣庢俯淇濇姢鎭㈠ */

    for (i = 0; i < 8; i++)
    {
        temp = iSheldTemp_10K_NTC[AFE_TEMPERATURE[i]];
        *(((u8 *)&AFE_ROM_PARAMETERS_Struction.m11H_19H) + i) = (u8)(((u32)temp << 9) / ((u32)SH367309_Reg_Store.TR_ResRef + temp));
    }
}

/* 姣忔鏁版嵁鏀瑰彉閮借鍙杛om鍙傛暟姣旇緝涓�涓嬶紝閭ｄ釜鍙傛暟鏀瑰彉灏卞啓鍏ラ偅=鍝釜 */
void Write_Parameters(void)
{
    int i = 0;
    u8 temp[26] = {0};
    u8 *P = (u8 *)&AFE_ROM_PARAMETERS_Struction;

    if (MTPRead(0x00, 25, temp))
    {
        for (i = 0; i < 25; i++)
        { // 鏈�鍚庝竴涓猅R涓嶅仛瀵规瘮
            if (temp[i] != P[i])
            {
                MTPWriteROM(i, 1, P + i); // 閲嶅啓EEPROM鐨勫瘎瀛樺櫒锛屼袱娆�
                Delay1ms(40);
            }
        }
    }
}

void AFE_Reset(void)
{
    u8 WrBuf[2];

    WrBuf[0] = 0xC0;
    WrBuf[1] = 0xA5;

    /*
    if(!System_ErrFlag.u8ErrFlag_Com_AFE1) {
        if(!MTPWrite(AFE_ID, 0xEA, 1, WrBuf)) {              //0xEA, 0xC0?A CRC
            MTPWrite(AFE_ID, 0xEA, 1, WrBuf);
        }
        //MTPWrite(0xEA, 1, WrBuf);
    }
    */

    if (!System_ERROR_UserCallback(ERROR_STATUS_AFE1))
    {
        // if (!MTPWrite(0xEA, 1, WrBuf))
        // { // 0xEA, 0xC0?A CRC
        //     MTPWrite(0xEA, 1, WrBuf);
        // }
        MTPWrite(0xEA, 1, WrBuf);
    }
}

u8 AFE_IsReady(void)
{
    u8 TempCnt = 0, result = 0;
    u8 TempVar;

    while (1)
    {
        Feed_IWatchDog;

        TempVar = 0;
        if (MTPRead(MTP_BFLAG2, 1, &TempVar))
        { // 璇诲彇BLFG2锛屾煡鐪媀ADC鏄惁杞崲瀹屾垚
            if ((TempVar & 0x10) == 0x10)
            {
                break;
            }
        }

        Delay1ms(20);
        if (++TempCnt >= 50)
        {
            // System_ERROR_UserCallback(ERROR_AFE1);
            result = 1;
            break;
        }
    }
    return result;
}
void SH367309_Enable_AFE_Wdt_Cadc_Drivers(void)
{
    // ucMTP_CONF |= 0x04;						//寮�鍚湅闂ㄧ嫍锛屼笉寮�鐪嬮棬鐙楄涓嶈
    // 缁撹锛屽彲浠ヤ笉寮�鍚�傜湅闂ㄧ嫍婧㈠嚭锛屾搷浣滄槸
    // 1锛屽叧闂厖鏀剧數MOS鍜岄鍏匨OS
    // 2锛屾竻闄ゅ潎琛�
    // 涓よ�呭浜庣洰鍓嶄娇鐢ㄦ儏鍐垫剰涔変笉澶э紝浼戠湢甯︾數涓嶅厑璁稿紑锛孧CU鎺ч┍鍔ㄦ病鎰忎箟
    // 30x鏄渶瑕佸紑鐨勶紝鍥犱负鏄嚜宸辩殑淇濇姢浣撶郴锛岃繖涓�309鐢ㄧ殑鏄粬鑷繁鐨勪綋绯伙紝鎵�浠ュ氨绠楀嚭闂
    // 鐪嬮棬鐙椾笉鍏筹紝浠栬嚜宸辩殑淇濇姢浣撶郴鍒ゆ柇鏄惁鍏矼OS锛岄闄╀篃涓嶅ぇ銆�
    SH367309_Reg_Store.REG_MTP_CONF.bits.CADCON = 1; // 寮�鍚疌ADC
    SH367309_Reg_Store.REG_MTP_CONF.bits.CHGMOS = 1; // 鍏呯數MOS鐢盇FE纭欢鎺у埗
    SH367309_Reg_Store.REG_MTP_CONF.bits.DSGMOS = 1; // 鏀剧數MOS鐢盇FE纭欢鎺у埗
    MTPWrite(MTP_CONF, 1, &SH367309_Reg_Store.REG_MTP_CONF.all);
}

void SH367309_UpdataAfeConfig(void)
{
    u8 isdiff = 0;

    if (AFE_PARAM_WRITE_Flag)
    {
        AFE_PARAM_WRITE_Flag = 0;
        // load_protectParam();
        Refresh_Parameters();
        {
            int i = 0;
            u8 temp[26] = {0};
            u8 *P = (u8 *)&AFE_ROM_PARAMETERS_Struction;
            printf("[!!!]flash afe param111");
            array_printf((unsigned char *)&AFE_ROM_PARAMETERS_Struction, sizeof(AFE_ROM_PARAMETERS_Struction));
            // array_printf((unsigned char*)&AFE_ROM_PARAMETERS_Struction, sizeof(AFE_ROM_PARAMETERS_Struction));

            if (MTPRead(0x00, 25, temp))
            {
                array_printf(temp, sizeof(temp));
                for (i = 0; i < 25; i++)
                { // 鏈�鍚庝竴涓猅R涓嶅仛瀵规瘮
                    if (temp[i] != P[i])
                    {
                        isdiff = 1;
                        break;
                    }
                }
            }
        }

        if (isdiff)
        {
            printf("[!!!]flash afe param222");
            // MCUO_AFE_VPRO = 1; // 杩涘叆鐑у啓妯″紡
            gpio_write(GPIO_PD7, 1);
            Delay1ms(20);
            Feed_IWatchDog;

            Write_Parameters();

            Feed_IWatchDog;
            // MCUO_AFE_VPRO = 0; // 閫�鍑虹儳鍐欐ā寮�
            gpio_write(GPIO_PD7, 0);
            Delay1ms(1);

            /* 姣忔鍐欏畬濡傛灉涓嶆姤閿欓兘瑕佸浣嶄竴涓嬨�傝繖鏍峰啓杩涘幓鐨勫弬鏁版墠鏈夋晥 */
            if (!System_ERROR_UserCallback(ERROR_STATUS_AFE1))
            {
                AFE_Reset(); // Reset IC
                Delay1ms(5);
                AFE_IsReady();
                AFE_ResetFlag = 1;
            }
            SH367309_Enable_AFE_Wdt_Cadc_Drivers();
        }
        else{
            printf("[!!!] no need flash");
        }
    }
}
