#include "conf.h"
#include "Sci_Upper.h"

extern struct stCell_Info g_stCellInfoReport;

#define FAC_INIT_soc (60)
#define CapacityFactory (87)

#define SOC_100_VAL (4180)
#define SOC_0_VAL (3000)

typedef enum _CUR {
CurCHG = 0, CurDSG
}_Cur;

// 80%衰减 1.25倍
static const uint16_t dsg_rate_table[7][3] = {
	// 电流→  target_soc  <0.2C   0.2C    0.5C    0.8C    1.2C    >1.5C
	{3600, 45, 11},
	{3300, 25, 12},
	{3200, 15, 12},
	{3150, 8, 13},
	{3100, 4, 13},
	{3050, 2, 14},
	{SOC_0_VAL, 0, 20}};

uint32_t ModulusSub(uint32_t Data1, uint32_t Data2)
{
	return (uint32_t)(Data1 > Data2 ? Data1 - Data2 : Data2 - Data1);
}

#if 1

#define SOC_FAC_VALUE 60

#if 1
// #define SOC_100_VAL g_tParam.other.u16Soc_V_100
// #define SOC_0_VAL g_tParam.other.u16Soc_V_0

#define VCELLMAX g_stCellInfoReport.u16VCellMax
#define VCELLMIN g_stCellInfoReport.u16VCellMin

#define ICHG g_stCellInfoReport.u16Ichg
#define IDSG g_stCellInfoReport.u16IDischg

// #define COV_VAL g_tParam.protect.u16VcellOvp_Third
// #define CUV_VAL g_tParam.protect.u16VcellUvp_Third
// #define BOV_VAL g_tParam.protect.u16VbusOvp_Third
// #define BUV_VAL g_tParam.protect.u16VbusUvp_Third

#define COV_VAL (4200)
#define CUV_VAL (3000)
#define BOV_VAL (4200 * SNum)
#define BUV_VAL (2900 * SNum)

#else
#define SOC_100_VAL g_tParam.other.u16Soc_V_100
#define SOC_0_VAL g_tParam.other.u16Soc_V_0

#define VCELLMAX g_stCellInfoReport.u16VCellMax
#define VCELLMIN g_stCellInfoReport.u16VCellMin

#define ICHG g_stCellInfoReport.u16Ichg
#define IDSG g_stCellInfoReport.u16IDischg

#define COV_VAL PRT_E2ROMParas.u16VcellOvp_Third
#define CUV_VAL PRT_E2ROMParas.u16VcellUvp_Third
#define BOV_VAL PRT_E2ROMParas.u16VbusOvp_Third
#define BUV_VAL PRT_E2ROMParas.u16VbusUvp_Third
#endif

#define SOC_VIRTUAL_CURRENT_CHG (uint16_t)2 // A*10��1��2����Ϊ��0����=�ţ�0.2�Ϳ�ʼ����
#define SOC_VIRTUAL_CURRENT_DSG (uint16_t)2 // A*10��1��2����Ϊ��0���������Ϊ0��ͬʱ����=���ж���ȥ����Ȼ�ͻῨ��DSG��������������

// #define _CAL_SLOW_DOWN_CHG

enum SOC_CALI_STATE
{
	SOC_CALI_STATE_TRANSFER,
	SOC_CALI_CONT_CHG,
	SOC_CALI_CONT_DSG,
};

struct SOC_CALCULATE_ELEMENT
{
	uint32_t u32CapFactory;		// ��س�ʼ������(��������)As*10 =        Ah*3600*10
	uint32_t u32CapChange;		// ��������仯	   As*10����������
	uint8_t u8CHG_AHCalcu_Flag; // ��簲ʱ���ֿ�ʹ�ñ�־
	uint8_t u8DSG_AHCalcu_Flag; // �ŵ簲ʱ���ֿ�ʹ�ñ�־

	uint8_t u8SOC_Now;	   // ��ǰ���SOC     0��100 Ϊ��������ٷֱ�
	uint32_t u32CapNow;	   // ���ʣ��������As*10
	uint8_t u8DSG_SOC_Int; // ѭ������ֻ��ŵ������ѷŵ����������ٷֱȣ�90%��һ��ѭ��
	uint32_t u32Cycle_times; // ѭ������*100������ֻ��������һ������ֱ�ӵ���ȥ��������̫���EEPROM���ֲ���
	uint32_t u32CapFull;	   // ���˥����������As*10(SOH)���ҵ���ʾSOHҪ��һ�ģ������

	uint8_t u8SOC_Old;		  // ��ʼSOC    0-100 Ϊ��������ٷֱ�
	uint32_t u32CapFull_Cal_As; // �������У�����������As*10

	float delata_cap;
	float acc_cap_K;
	float silent_power;
};

struct SOC_CALCULATE_ELEMENT SOC_Calculate_Element;		 // �ڲ�����ṹ��
struct SOC_CALCULATE_ELEMENT back_SOC_Calculate_Element; // �ڲ�����ṹ��

enum SOC_CALI_STATE SOC_Cali_Flag = SOC_CALI_STATE_TRANSFER; // ��ģ����������		SOC����״̬�����ǵó�ʼ��

static uint8_t get_current_level(void)
{
	// uint32_t c_rate1000 = SOC_Enhance_Element.u16_Idsg * 10 / SOC_Enhance_Element.u16_SOC_Ah;
	// if (c_rate1000 <  200) return 0;
	return 2;
}

static uint8_t get_voltage_level(void)
{
	uint16_t v = VCELLMIN;

	if (v < SOC_0_VAL)
		return 6;
	else if (v < 3050)
		return 5;
	else if (v < 3100)
		return 4;
	else if (v < 3150)
		return 3;
	else if (v < 3200)
		return 2;
	else if (v < 3300)
		return 1;
	else if (v < 3600)
		return 0;
	//???
	else
		return 0xff;
}

#if 0
static float get_dsg_rate_permil(void)
{
	float cap_K = 1.0;
	uint8_t target_soc = 0xff;
	uint8_t voltage_level = 0xff;

	voltage_level = get_voltage_level();
	target_soc = dsg_rate_table[voltage_level][1];

	if (voltage_level != 0xff && SOC_Calculate_Element.u8SOC_Now > target_soc)
	{
		if (g_stCellInfoReport.u16IDischg >= 10)
			cap_K = (float)dsg_rate_table[voltage_level][get_current_level()] / 10;
		else
			cap_K = 1.5;

		return cap_K;
	}

	return 1.0;
}
#endif
static u16 get_dsg_rate_permil(void)
{
	return 1;
}

uint8_t isCHG(void)
{
	return g_stCellInfoReport.u16Ichg > SOC_VIRTUAL_CURRENT_CHG ? 1 : 0;
}
uint8_t isDSG(void)
{
	return g_stCellInfoReport.u16IDischg > SOC_VIRTUAL_CURRENT_DSG ? 1 : 0;
}
uint8_t get_soc_real(void)
{
	return SOC_Calculate_Element.u8SOC_Now;
}

void set_calsoc(uint8_t _soc)
{
	SOC_Calculate_Element.u8SOC_Now = _soc;
	SOC_Calculate_Element.u32CapNow = get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
}

static void Inc_real_soc(void)
{
	SOC_Calculate_Element.u8SOC_Now += 1;
	SOC_Calculate_Element.u32CapNow += SOC_Calculate_Element.u32CapFull / 100;
}
static void Dec_real_soc(void)
{
	SOC_Calculate_Element.u8SOC_Now -= 1;
	SOC_Calculate_Element.u32CapNow -= SOC_Calculate_Element.u32CapFull / 100;
}

void set_soc_param(uint8_t _soc_val, uint16_t _cap_factory, uint8_t disp_sync_updatae)
{
	{
		// soc_calculate.u32CapFactory = (uint32_t)g_tParam.other.u16Soc_Ah * 3600;
		// soc_calculate.u32Cycle_times = (uint32_t)g_tParam.other.u16Soc_Cycle_times * 100;
	}

	set_calsoc(_soc_val);
	// todo
	// if (disp_sync_updatae)

	{
		set_dispsoc(_soc_val);
	}
	SOC_Calculate_Element.u32CapNow = get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
}

void soc_factory_param_init_first(void)
{
#if 1
	SOC_Calculate_Element.u8SOC_Now = FAC_INIT_soc;
	SOC_Calculate_Element.u32CapFactory = (uint32_t)CapacityFactory * 3600; // ???*10;???��??????????��????????��????????
	SOC_Calculate_Element.u32Cycle_times = (uint32_t)1 * 100;
	SOC_Calculate_Element.u32CapFull = SOC_Calculate_Element.u32CapFactory;
	SOC_Calculate_Element.u8DSG_SOC_Int = 0;

	// {
	// 	nvm_param_set(NVM_KEY_SOC, SOC_Calculate_Element.u8SOC_Now);
	// 	nvm_param_set(NVM_KEY_DSGSOC_INT, 0);
	// 	nvm_param_set(NVM_KEY_CYCLES, SOC_Calculate_Element.u32Cycle_times);
	// 	nvm_param_set(NVM_KEY_CAPACITY, SOC_Calculate_Element.u32CapFactory);
	// }

	SOC_Calculate_Element.u32CapNow = get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
	back_SOC_Calculate_Element = SOC_Calculate_Element;

#endif
}

void soc_param_lib_init(uint8_t _soc)
{
	// nvm3_readCounter(nvm3_defaultHandle, NVM_KEY_SOC, &SOC_Calculate_Element.u8SOC_Now);
	// nvm3_readCounter(nvm3_defaultHandle, NVM_KEY_DSGSOC_INT, &SOC_Calculate_Element.u8DSG_SOC_Int);
	// nvm3_readCounter(nvm3_defaultHandle, NVM_KEY_CYCLES, &SOC_Calculate_Element.u32Cycle_times);
	// nvm3_readCounter(nvm3_defaultHandle, NVM_KEY_CAPACITY, &SOC_Calculate_Element.u32CapFull);
	set_calsoc(_soc);
	SOC_Calculate_Element.u32CapNow = get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
	SOC_Calculate_Element.u32CapFactory = SOC_Calculate_Element.u32CapFull;

	back_SOC_Calculate_Element = SOC_Calculate_Element;
	// SOC_Calculate_Element.silent_power = 0.05;
	SOC_Calculate_Element.silent_power = 0.1; // 和电流单位一样，100ma
	SOC_Calculate_Element.acc_cap_K = 1;

	SOC_Result_Pass();
}

uint8_t Get_OpenCircuit_Value_new(uint16_t VCell)
{
#if 0
	uint8_t result = 0;

	// temp_compensatation();

	switch (g_tParam.other.u16Soc_TableSelect)
	{
	case SOC_TABLE_LIFEPO:
		result = GetEndValue(SOC_Table_LiFePO, (uint16_t)SOC_Size_LiFePO, VCell);
		if (VCell <= SOC_0_VAL)
		{
			result = 0;
		}
		break;
	case SOC_TABLE_TERNARYLI:
		result = GetEndValue(SocTable_TernaryLi, (uint16_t)SOC_Size_TernaryLi, (uint16_t)VCELLMIN);
		break;
	case SOC_TABLE_LIFEPO2:
		result = GetEndValue(SocTable_LiFePO2, (uint16_t)SOC_Size_LiFePO2, (uint16_t)VCELLMIN);
		break;
	default:
		break;
	}
	return result;
#endif
}

int8_t get_soc_from_openVol_onlyDec_new(uint16_t VCell)
{
	uint8_t result;
	uint8_t old_soc = get_dispsoc();

	result = Get_OpenCircuit_Value_new(VCell);
	if (result < old_soc)
		return result;

	return old_soc;
}

int8_t get_soc_from_openVol_new(uint16_t VCell)
{
	return Get_OpenCircuit_Value_new(VCell);
}

#if 1
void CorrectionTerminal_CV(enum _CUR CurrentType)
{
#if 0
	static uint16_t su16_SocChgCal_L1_Tcnt = 0;
	static uint16_t su16_SocChgCal_L2_Tcnt = 0;
	static uint16_t su16_SocChgCal_L3_Tcnt = 0;
	static uint16_t su16_SocChgCal_L4_Tcnt = 0;

	static uint16_t su16_SocDsgCal_L1_Tcnt = 0;
	static uint16_t su16_SocDsgCal_L2_Tcnt = 0;
	static uint16_t su16_SocDsgCal_L3_Tcnt = 0;
	static uint16_t su16_SocDsgCal_L4_Tcnt = 0;
	switch (CurrentType)
	{
	case CurCHG:
		if (VCELLMAX >= SOC_100_VAL - 100 && VCELLMAX < SOC_100_VAL && get_soc_real() < 95)
		{ // �ͷŵ������Ӧ����һ�Σ���������95%����
			if (++su16_SocChgCal_L1_Tcnt >= 10)
			{
				su16_SocChgCal_L1_Tcnt = 0;
				Inc_real_soc();
			}
		}
		else if (VCELLMAX >= SOC_100_VAL && get_soc_real() < 100)
		{
			if (get_soc_real() > 95)
			{
				if (++su16_SocChgCal_L2_Tcnt >= 8)
				{
					su16_SocChgCal_L2_Tcnt = 0;
					Inc_real_soc();
				}
			}
			else
			{
				if (++su16_SocChgCal_L3_Tcnt >= 4)
				{
					su16_SocChgCal_L3_Tcnt = 0;
					Inc_real_soc();
				}
			}
		}

		// ���ǻ��ڳ������ܴﵽ100%���ռ�������2S + 1%
		if (VCELLMAX >= SOC_100_VAL + 50 && get_soc_real() < 100)
		{
			if (++su16_SocChgCal_L4_Tcnt >= 2)
			{
				su16_SocChgCal_L4_Tcnt = 0;
				Inc_real_soc();
			}
		}

#ifdef _CAL_SLOW_DOWN_CHG
		if (get_soc_real() >= 99 && VCELLMAX < SOC_100_VAL)
		{
			// SOC_Calculate_Element.u8SOC_Now = 98;
			SOC_Calculate_Element.u8SOC_Now = get_soc_real(); // SOC���ֲ���
			SOC_Calculate_Element.u32CapChange = 0;			  // ������ۼ��������ɣ��������©���������1
			SOC_Calculate_Element.u32CapNow = (uint32_t)get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
		}
#endif

		su16_SocDsgCal_L1_Tcnt = 0;
		su16_SocDsgCal_L2_Tcnt = 0;
		su16_SocDsgCal_L3_Tcnt = 0;
		su16_SocDsgCal_L4_Tcnt = 0;
		break;

	case CurDSG:
#if 0
		//???容量加速？？？
		if (VCELLMIN < SOC_0_VAL + 200)
		{
			if (VCELLMIN < SOC_0_VAL)
			{
				if (get_soc_real() > 0)
				{
					su16_SocDsgCal_L4_Tcnt += g_stCellInfoReport.u16IDischg;

					if (su16_SocDsgCal_L4_Tcnt >= time_soc1_100_100mA_unit)
					{
						su16_SocDsgCal_L4_Tcnt = 0;
						Dec_real_soc();
					}
				}
			}
			else if (VCELLMIN < SOC_0_VAL + 50)
			{
				if (get_soc_real() > 5)
				{
					su16_SocDsgCal_L3_Tcnt += g_stCellInfoReport.u16IDischg;

					if (su16_SocDsgCal_L3_Tcnt >= time_soc1_100_100mA_unit)
					{
						su16_SocDsgCal_L3_Tcnt = 0;
						Dec_real_soc();
					}
				}
			}
			else if (VCELLMIN < SOC_0_VAL + 100)
			{
				if (get_soc_real() > 10)
				{
					su16_SocDsgCal_L2_Tcnt += g_stCellInfoReport.u16IDischg;

					if (su16_SocDsgCal_L2_Tcnt >= time_soc1_100_100mA_unit)
					{
						su16_SocDsgCal_L2_Tcnt = 0;
						Dec_real_soc();
					}
				}
			}
			else
			{
				if (get_soc_real() > 20)
				{
					su16_SocDsgCal_L1_Tcnt += g_stCellInfoReport.u16IDischg;

					if (su16_SocDsgCal_L1_Tcnt >= time_soc1_100_100mA_unit)
					{
						su16_SocDsgCal_L1_Tcnt = 0;
						Dec_real_soc();
					}
				}
			}

			if (get_soc_real() <= 1 && VCELLMIN > SOC_0_VAL)
			{
				SOC_Calculate_Element.u8SOC_Now = get_soc_real(); // SOC���ֲ���
				SOC_Calculate_Element.u32CapChange = 0;			  // ������ۼ��������ɣ��������©���������1
				SOC_Calculate_Element.u32CapNow = (uint32_t)get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
			}
		}

		su16_SocChgCal_L1_Tcnt = 0;
		su16_SocChgCal_L2_Tcnt = 0;
		su16_SocChgCal_L3_Tcnt = 0;
		su16_SocChgCal_L4_Tcnt = 0;
#endif
		break;

	default:
		break;
	}
#endif
}
#endif

void Correction_Terminal(enum _CUR CurrentType)
{
#if 0
	switch (CurrentType)
	{
	case CurCHG:
		CorrectionTerminal_CV(CurrentType);
		break;

	case CurDSG:
		CorrectionTerminal_CV(CurrentType);
		break;
	default:
		break;
	}
#endif
}

void SOC_Cont_AH_Int_CHG(void)
{
	uint32_t C_change_per;
	static uint8_t s_u8_CHG200msCnt = 0;
	static uint8_t s_u8_Transfer200msCnt = 0;
#if 1
	if (g_stCellInfoReport.u16Ichg >= SOC_VIRTUAL_CURRENT_CHG)
	{
		// if(g_stCellInfoReport.u16Ichg > 0) {
		if (++s_u8_CHG200msCnt >= 5)
		{
			s_u8_CHG200msCnt = 0;
			SOC_Calculate_Element.u8CHG_AHCalcu_Flag = 1;
		}
		if (s_u8_Transfer200msCnt)
			s_u8_Transfer200msCnt = 0;
	}
	else
	{
		if (++s_u8_Transfer200msCnt >= 2)
		{ // ��ֹ˲����������
			s_u8_Transfer200msCnt = 0;
			s_u8_CHG200msCnt = 0;
			SOC_Cali_Flag = SOC_CALI_STATE_TRANSFER;
			return;
		}
		--s_u8_CHG200msCnt;
	}
#endif

	if (SOC_Calculate_Element.u8CHG_AHCalcu_Flag)
	{
		Correction_Terminal(CurCHG);
		SOC_Calculate_Element.u8SOC_Old = get_soc_real();
		SOC_Calculate_Element.u32CapChange += (uint32_t)g_stCellInfoReport.u16Ichg * 1; // As*10*100(����Ч��100)
		SOC_Calculate_Element.u32CapNow += (uint32_t)g_stCellInfoReport.u16Ichg * 1;	  // ʣ������ʵʱ����

		if (SOC_Calculate_Element.u32CapNow > SOC_Calculate_Element.u32CapFull)
			SOC_Calculate_Element.u32CapNow = SOC_Calculate_Element.u32CapFull;
		C_change_per = SOC_Calculate_Element.u32CapChange * 100 / SOC_Calculate_Element.u32CapFull;
		SOC_Calculate_Element.u8SOC_Now = SOC_Calculate_Element.u8SOC_Old + C_change_per;
		if (get_soc_real() > 100)
			SOC_Calculate_Element.u8SOC_Now = 100;
		SOC_Calculate_Element.u32CapChange = (((SOC_Calculate_Element.u32CapChange * 100) % SOC_Calculate_Element.u32CapFull) + 50) / 100;
		SOC_Calculate_Element.u8CHG_AHCalcu_Flag = 0;

		SOC_Calculate_Element.u32CapFull_Cal_As += (uint32_t)g_stCellInfoReport.u16Ichg * 1;
	}
}

void SOC_Cont_AH_Int_DSG(void)
{
	uint32_t C_change_per;
	static uint8_t s_u8_DSG200msCnt = 0;
	static uint8_t s_u8_Transfer200msCnt = 0;
#if 1
	if (g_stCellInfoReport.u16IDischg >= SOC_VIRTUAL_CURRENT_DSG)
	{
		if (++s_u8_DSG200msCnt >= 5)
		{
			s_u8_DSG200msCnt = 0;
			SOC_Calculate_Element.u8DSG_AHCalcu_Flag = 1;
		}
		if (s_u8_Transfer200msCnt)
			s_u8_Transfer200msCnt = 0;
	}
	else
	{
		if (++s_u8_Transfer200msCnt >= 2)
		{
			s_u8_Transfer200msCnt = 0;
			s_u8_DSG200msCnt = 0;
			SOC_Cali_Flag = SOC_CALI_STATE_TRANSFER;
			return;
		}
		--s_u8_DSG200msCnt;
	}
#endif

	if (SOC_Calculate_Element.u8DSG_AHCalcu_Flag)
	{
		SOC_Calculate_Element.acc_cap_K = get_dsg_rate_permil();
		// Correction_Terminal(CurDSG);
		// todo 怎么处理容量与soc，静态功耗在哪里处理？
		SOC_Calculate_Element.u8SOC_Old = get_soc_real();
		// SOC_Calculate_Element.u32CapChange += (uint32_t)g_stCellInfoReport.u16IDischg * 1;
		// SOC_Calculate_Element.u32CapNow -= (uint32_t)g_stCellInfoReport.u16IDischg * 1;
		SOC_Calculate_Element.delata_cap = SOC_Calculate_Element.acc_cap_K * g_stCellInfoReport.u16IDischg * 1;
		SOC_Calculate_Element.u32CapChange += (uint32_t)SOC_Calculate_Element.delata_cap;
		SOC_Calculate_Element.u32CapNow -= (uint32_t)SOC_Calculate_Element.delata_cap;

		if (SOC_Calculate_Element.u32CapNow > SOC_Calculate_Element.u32CapFull)
			SOC_Calculate_Element.u32CapNow = 0;
		C_change_per = SOC_Calculate_Element.u32CapChange * 100 / SOC_Calculate_Element.u32CapFull;
		SOC_Calculate_Element.u8SOC_Now = SOC_Calculate_Element.u8SOC_Old - C_change_per;
		if (get_soc_real() > 100)
			SOC_Calculate_Element.u8SOC_Now = 0;
		SOC_Calculate_Element.u32CapChange = (((SOC_Calculate_Element.u32CapChange * 100) % SOC_Calculate_Element.u32CapFull) + 50) / 100; // �������룬�ؼ�
		SOC_Calculate_Element.u8DSG_AHCalcu_Flag = 0;

		if (get_soc_real() != 0)
		{
			SOC_Calculate_Element.u8DSG_SOC_Int += C_change_per;
			// SOC_Calculate_Element.u8DSG_SOC_Int += 1;
			if (SOC_Calculate_Element.u8DSG_SOC_Int >= 80)
			{
				SOC_Calculate_Element.u8DSG_SOC_Int = 0;
				SOC_Calculate_Element.u32Cycle_times += 100;
			}
		}
	}
}

void SOC_State_Transfer(void)
{
	static uint8_t s_u8SOC_State_CHG = 0;
	static uint8_t s_u8SOC_State_DSG = 0;
	static uint8_t s_u8SOC_State_OCV = 0;
	if (g_stCellInfoReport.u16Ichg >= SOC_VIRTUAL_CURRENT_CHG)
	{
		if (++s_u8SOC_State_CHG >= 3)
		{
			s_u8SOC_State_CHG = 0;
			SOC_Cali_Flag = SOC_CALI_CONT_CHG;
		}
		if (s_u8SOC_State_DSG)
			s_u8SOC_State_DSG = 0;
		if (s_u8SOC_State_OCV)
			s_u8SOC_State_OCV = 0;
	}
	else if (g_stCellInfoReport.u16IDischg >= SOC_VIRTUAL_CURRENT_DSG)
	{
		if (++s_u8SOC_State_DSG >= 3)
		{
			s_u8SOC_State_DSG = 0;
			SOC_Cali_Flag = SOC_CALI_CONT_DSG;
		}
		if (s_u8SOC_State_CHG)
			s_u8SOC_State_CHG = 0;
		if (s_u8SOC_State_OCV)
			s_u8SOC_State_OCV = 0;
	}
	else
	{
		SOC_Calculate_Element.acc_cap_K = 1;

		if (++s_u8SOC_State_OCV >= 3)
		{
			s_u8SOC_State_OCV = 0;
		}
		if (s_u8SOC_State_CHG)
			s_u8SOC_State_CHG = 0;
		if (s_u8SOC_State_DSG)
			s_u8SOC_State_DSG = 0;
	}
}

void SOC_Update_param(void)
{
#if 0
	SOC_Calculate_Element.u8DSG_SOC_Int = 0;
	SOC_Calculate_Element.u32CapFactory = (uint32_t)g_tParam.other.u16Soc_Ah * 3600;
	SOC_Calculate_Element.u32Cycle_times = (uint32_t)g_tParam.other.u16Soc_Cycle_times * 100;
	// ����SOC_Calculate_Element.u32CapFactory�Ѿ���ʼ��
	SOC_Calculate_Element.u32CapFull = SOC_Calculate_Element.u32CapFactory;

	SOC_Calculate_Element.u32CapNow = get_soc_real() * SOC_Calculate_Element.u32CapFull / 100;
#endif
	InitData_SOC();
}

void SOC_Result_Pass(void)
{
	g_stCellInfoReport.SocElement.u16Soc = get_soc_real();
	g_stCellInfoReport.SocElement.u16CapacityNow = SOC_Calculate_Element.u32CapNow * 1 / 360;

	// todo soh algo
	g_stCellInfoReport.SocElement.u16Soh = 100;

	g_stCellInfoReport.SocElement.u16CapacityFull = SOC_Calculate_Element.u32CapFull * 1 / 360;
	g_stCellInfoReport.SocElement.u16CapacityFactory = SOC_Calculate_Element.u32CapFactory * 1 / 360;
	g_stCellInfoReport.SocElement.u16Cycle_times = SOC_Calculate_Element.u32Cycle_times / 100;
}

void SOC_EEPROM_Deal_Monitor(void)
{
	if (back_SOC_Calculate_Element.u8SOC_Now != SOC_Calculate_Element.u8SOC_Now)
	{
		back_SOC_Calculate_Element.u8SOC_Now = SOC_Calculate_Element.u8SOC_Now;
		// nvm_param_set(NVM_KEY_SOC, SOC_Calculate_Element.u8SOC_Now);
		// printf("soc %d", SOC_Calculate_Element.u8SOC_Now);
	}
	if (back_SOC_Calculate_Element.u8DSG_SOC_Int != SOC_Calculate_Element.u8DSG_SOC_Int)
	{
		back_SOC_Calculate_Element.u8DSG_SOC_Int = SOC_Calculate_Element.u8DSG_SOC_Int;
		// nvm_param_set(NVM_KEY_DSGSOC_INT, SOC_Calculate_Element.u8DSG_SOC_Int);
		// printf("dsg soc int %d", SOC_Calculate_Element.u8DSG_SOC_Int);
	}
	if (back_SOC_Calculate_Element.u32Cycle_times != SOC_Calculate_Element.u32Cycle_times)
	{
		back_SOC_Calculate_Element.u32Cycle_times = SOC_Calculate_Element.u32Cycle_times;
		// nvm_param_set(NVM_KEY_CYCLES, SOC_Calculate_Element.u32Cycle_times);
		// nvm_param_set(NVM_KEY_CAPACITY, SOC_Calculate_Element.u32CapFactory);
	}
	// if()
}

extern enum status_sys sys_status;
void soc_cali(void)
{
	static uint8_t dsg_soc0_delay = 0;
#if 0
#ifdef _SOC_OCV_Fix2_func_
	SOC_OCV_Fix2();
#endif
#endif
	// //???只触发一次
	// if ((sys_status == s_CHG) && (g_stCellInfoReport.u16VCellTotle * 10 >= 4000 * SNum) && (isCOV || g_stCellInfoReport.u16VCellMax >= SOC_100_VAL) && g_stCellInfoReport.u16VCellMin >= 4000)
	// {
	// 	set_soc_param(100, 1, 1);
	// }
	// // else if ((g_stCellInfoReport.u16VCellTotle * 10 <= 2900 * SNum) && (gcel))
	// else if ((sys_status == s_DSG) && (g_stCellInfoReport.u16VCellMin <= SOC_0_VAL) && (g_stCellInfoReport.u16VCellMin >= 2000))
	// {
	// 	if(++dsg_soc0_delay >= (5 * 10))
	// 	{
	// 		dsg_soc0_delay = 0;
	// 		set_soc_param(0, 1, 1);
	// 	}
	// }
	// else
	// {
	// 	dsg_soc0_delay = 0;
	// }
}

void APP_SOC_IntEnhance_Ctrl()
{
	static uint16_t silent_power_delay = 0;

	switch (SOC_Cali_Flag)
	{
	case SOC_CALI_STATE_TRANSFER:
		SOC_State_Transfer();
		break;
	case SOC_CALI_CONT_CHG:
		SOC_Cont_AH_Int_CHG();
		break;
	case SOC_CALI_CONT_DSG:
		SOC_Cont_AH_Int_DSG();
		break;
	default:
		break;
	}

	// if (sys_status != s_CHG)
	// {
	// 	if (SOC_Calculate_Element.u8SOC_Now < 100)
	// 	{
	// 		if (++silent_power_delay >= 5 * 60)
	// 		{
	// 			uint32_t C_change_per;
	// 			silent_power_delay = 0;

	// 			SOC_Calculate_Element.u8SOC_Old = get_soc_real();

	// 			SOC_Calculate_Element.delata_cap = SOC_Calculate_Element.silent_power * 1 * 60;
	// 			SOC_Calculate_Element.u32CapChange += (uint32_t)SOC_Calculate_Element.delata_cap;
	// 			SOC_Calculate_Element.u32CapNow -= (uint32_t)SOC_Calculate_Element.delata_cap;

	// 			if (SOC_Calculate_Element.u32CapNow > SOC_Calculate_Element.u32CapFactory)
	// 				SOC_Calculate_Element.u32CapNow = 0;
	// 		}
	// 	}
	// }

	soc_cali();

	SOC_EEPROM_Deal_Monitor();

	SOC_Result_Pass();
}

#endif
