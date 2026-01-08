#ifndef SOCENHANCE_H
#define SOCENHANCE_H

#include "conf.h"

enum SOC_TABLE_SELECT {
	SOC_TABLE_TEST = 0,
	SOC_TABLE_LIFEPO,
	SOC_TABLE_TERNARYLI,
	SOC_TABLE_LIFEPO2
};

#define E2P_AdressNum 			(uint16_t)16

void APP_SOC_IntEnhance_Ctrl();

void soc_factory_param_init_first(void);

void bmsParam_save(void);
int8_t get_soc_from_openVol_new(uint16_t VCell);
void set_soc_param(uint8_t _soc_val, uint16_t _cap_factory, uint8_t disp_sync_updatae);

#endif	/* SOCENHANCE_H */

