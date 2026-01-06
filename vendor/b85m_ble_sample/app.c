/********************************************************************************************************
 * @file	app.c
 *
 * @brief	This is the source file for BLE SDK
 *
 * @author	BLE GROUP
 * @date	06,2020
 *
 * @par     Copyright (c) 2020, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *          All rights reserved.
 *
 *          Redistribution and use in source and binary forms, with or without
 *          modification, are permitted provided that the following conditions are met:
 *
 *              1. Redistributions of source code must retain the above copyright
 *              notice, this list of conditions and the following disclaimer.
 *
 *              2. Unless for usage inside a TELINK integrated circuit, redistributions
 *              in binary form must reproduce the above copyright notice, this list of
 *              conditions and the following disclaimer in the documentation and/or other
 *              materials provided with the distribution.
 *
 *              3. Neither the name of TELINK, nor the names of its contributors may be
 *              used to endorse or promote products derived from this software without
 *              specific prior written permission.
 *
 *              4. This software, with or without modification, must only be used with a
 *              TELINK integrated circuit. All other usages are subject to written permission
 *              from TELINK and different commercial license may apply.
 *
 *              5. Licensee shall be solely responsible for any claim to the extent arising out of or
 *              relating to such deletion(s), modification(s) or alteration(s).
 *
 *          THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 *          ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *          WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *          DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER BE LIABLE FOR ANY
 *          DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *          (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *          LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *          ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *          (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *          SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *******************************************************************************************************/
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"

#include "app.h"
#include "app_att.h"

#include "application/keyboard/keyboard.h"
#include "application/usbstd/usbkeycode.h"

#include "app_config.h"
#include "param.h"


#define 	   ADV_IDLE_ENTER_DEEP_TIME				60  //60 s
#define 	   CONN_IDLE_ENTER_DEEP_TIME			60  //60 s

#define 	   MY_DIRECT_ADV_TMIE							2000000


#define     MY_APP_ADV_CHANNEL					    BLT_ENABLE_ADV_ALL
#define 	   MY_ADV_INTERVAL_MIN						ADV_INTERVAL_30MS
#define 	   MY_ADV_INTERVAL_MAX						ADV_INTERVAL_35MS

#define	   MY_RF_POWER_INDEX							RF_POWER_P3dBm



#define	   BLE_DEVICE_ADDRESS_TYPE 					BLE_DEVICE_ADDRESS_PUBLIC

_attribute_data_retention_	own_addr_type_t 	app_own_address_type = OWN_ADDRESS_PUBLIC;


/**
 * @brief      LinkLayer RX & TX FIFO configuration
 */
#define	   RX_FIFO_SIZE											64
#define	   RX_FIFO_NUM											8

#define	   TX_FIFO_SIZE											40
#define	   TX_FIFO_NUM											16


_attribute_data_retention_  u8 		 	blt_rxfifo_b[RX_FIFO_SIZE * RX_FIFO_NUM] = {0};
_attribute_data_retention_	my_fifo_t	blt_rxfifo = {
												RX_FIFO_SIZE,
												RX_FIFO_NUM,
												0,
												0,
												blt_rxfifo_b,};


_attribute_data_retention_  u8 		 	blt_txfifo_b[TX_FIFO_SIZE * TX_FIFO_NUM] = {0};
_attribute_data_retention_	my_fifo_t	blt_txfifo = {
												TX_FIFO_SIZE,
												TX_FIFO_NUM,
												0,
												0,
												blt_txfifo_b,};



/**
 * @brief	Adv Packet data
 */

// const u8	tbl_advData[] = {
// 	 0x05, 0x09, 'h', 'a', 'n', 's', 't', 'a', 'r',
// 	 0x02, 0x01, 0x05, 							// BLE limited discoverable mode and BR/EDR not supported
// 	 0x03, 0x19, 0x80, 0x01, 					// 384, Generic Remote Control, Generic category
// 	 0x05, 0x02, 0x12, 0x18, 0x0F, 0x18,		// incomplete list of service class UUIDs (0x1812, 0x180F)
// };
// const u8	tbl_advData[] = {
// 	 0x05, 0x09, 'V', 'H', 'I', 'D',
// 	 0x02, 0x01, 0x05, 							// BLE limited discoverable mode and BR/EDR not supported
// 	 0x03, 0x19, 0x80, 0x01, 					// 384, Generic Remote Control, Generic category
// 	 0x05, 0x02, 0x12, 0x18, 0x0F, 0x18,		// incomplete list of service class UUIDs (0x1812, 0x180F)
// };
// const u8	tbl_advData[] = {
// 	 0x05, 0x09, 'S', 'T', 'A', 'R',
// 	 0x02, 0x01, 0x05, 							// BLE limited discoverable mode and BR/EDR not supported
// 	 0x03, 0x19, 0x80, 0x01, 					// 384, Generic Remote Control, Generic category
// 	 0x05, 0x02, 0x12, 0x18, 0x0F, 0x18,		// incomplete list of service class UUIDs (0x1812, 0x180F)
// };
const u8	tbl_advData[] = {
	 0x05, 0x09, 'B', 'T', 'A', 'R',
	 0x02, 0x01, 0x05, 							// BLE limited discoverable mode and BR/EDR not supported
	 0x03, 0x19, 0x80, 0x01, 					// 384, Generic Remote Control, Generic category
	 0x05, 0x02, 0x12, 0x18, 0x0F, 0x18,		// incomplete list of service class UUIDs (0x1812, 0x180F)
};


/**
 * @brief	Scan Response Packet data
 */
// const u8	tbl_scanRsp [] = {
// 		 0x08, 0x09, 'v', 'S', 'a', 'm', 'p', 'l', 'e',
// 	};
const u8	tbl_scanRsp [] = {
	 0x08, 0x09, 'B', 'T', 'n', 's', 't', 'a', 'r',
	};


_attribute_data_retention_	int device_in_connection_state;
_attribute_data_retention_	u32 advertise_begin_tick;
_attribute_data_retention_	u32	interval_update_tick;
_attribute_data_retention_	u8	sendTerminate_before_enterDeep = 0;
_attribute_data_retention_	u32	latest_user_event_tick;



#if (UI_KEYBOARD_ENABLE)

_attribute_data_retention_	int 		key_not_released;
_attribute_data_retention_	u8 		key_type;
_attribute_data_retention_	static 	u32 keyScanTick = 0;

extern u32	scan_pin_need;

#define CONSUMER_KEY   	   		1
#define KEYBOARD_KEY   	   		2

/**
 * @brief		this function is used to process keyboard matrix status change.
 * @param[in]	none
 * @return      none
 */
void key_change_proc(void)
{
	latest_user_event_tick = clock_time();  //record latest key change time

	u8 key0 = kb_event.keycode[0];
	u8 key_buf[8] = {0,0,0,0,0,0,0,0};

	key_not_released = 1;
	if (kb_event.cnt == 2)   //two key press, do  not process
	{
	}
	else if(kb_event.cnt == 1)
	{
		if(key0 >= CR_VOL_UP )  //volume up/down
		{
			key_type = CONSUMER_KEY;
			u16 consumer_key;
			if(key0 == CR_VOL_UP){  	//volume up
				consumer_key = MKEY_VOL_UP;
			}
			else if(key0 == CR_VOL_DN){ //volume down
				consumer_key = MKEY_VOL_DN;
			}
			blc_gatt_pushHandleValueNotify (BLS_CONN_HANDLE, HID_CONSUME_REPORT_INPUT_DP_H, (u8 *)&consumer_key, 2);
		}
		else
		{
			key_type = KEYBOARD_KEY;
			key_buf[2] = key0;
			blc_gatt_pushHandleValueNotify (BLS_CONN_HANDLE, HID_NORMAL_KB_REPORT_INPUT_DP_H, key_buf, 8);
		}
	}
	else   //kb_event.cnt == 0,  key release
	{
		key_not_released = 0;
		if(key_type == CONSUMER_KEY)
		{
			u16 consumer_key = 0;
			blc_gatt_pushHandleValueNotify (BLS_CONN_HANDLE, HID_CONSUME_REPORT_INPUT_DP_H, (u8 *)&consumer_key, 2);
		}
		else if(key_type == KEYBOARD_KEY)
		{
			key_buf[2] = 0;
			blc_gatt_pushHandleValueNotify (BLS_CONN_HANDLE, HID_NORMAL_KB_REPORT_INPUT_DP_H, key_buf, 8); //release
		}
	}
}



/**
 * @brief      this function is used to detect if key pressed or released.
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
_attribute_ram_code_
void proc_keyboard (u8 e, u8 *p, int n)
{
	if(clock_time_exceed(keyScanTick, 8000)){
		keyScanTick = clock_time();
	}
	else{
		return;
	}

	kb_event.keycode[0] = 0;
	int det_key = kb_scan_key (0, 1);

	if (det_key){
		key_change_proc();
	}
}



#elif (UI_BUTTON_ENABLE)


_attribute_data_retention_ static int button_detect_en = 0;
_attribute_data_retention_ static u32 button_detect_tick = 0;

#endif



/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_SUSPEND_ENTER"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void  ble_remote_set_sleep_wakeup (u8 e, u8 *p, int n)
{
	if( blc_ll_getCurrentState() == BLS_LINK_STATE_CONN && ((u32)(bls_pm_getSystemWakeupTick() - clock_time())) > 80 * SYSTEM_TIMER_TICK_1MS){  //suspend time > 30ms.add gpio wakeup
		bls_pm_setWakeupSource(PM_WAKEUP_PAD);  //gpio pad wakeup suspend/deepsleep
	}
}












/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_ADV_DURATION_TIMEOUT"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void 	app_switch_to_indirect_adv(u8 e, u8 *p, int n)
{

	bls_ll_setAdvParam( MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
						ADV_TYPE_CONNECTABLE_UNDIRECTED, app_own_address_type,
						0,  NULL,
						MY_APP_ADV_CHANNEL,
						ADV_FP_NONE);

	bls_ll_setAdvEnable(1);  //must: set adv enable
}



/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_TERMINATE"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void 	task_terminate(u8 e,u8 *p, int n) //*p is terminate reason
{
	device_in_connection_state = 0;


	if(*p == HCI_ERR_CONN_TIMEOUT){

	}
	else if(*p == HCI_ERR_REMOTE_USER_TERM_CONN){  //0x13

	}
	else if(*p == HCI_ERR_CONN_TERM_MIC_FAILURE){

	}
	else{

	}

	#if (UI_LED_ENABLE)
		gpio_write(GPIO_LED_RED, !LED_ON_LEVAL);
		gpio_write(GPIO_LED_BLUE, !LED_ON_LEVAL);
	#endif


	#if (BLE_APP_PM_ENABLE)
		 //user has push terminate pkt to ble TX buffer before deepsleep
		if(sendTerminate_before_enterDeep == 1){
			sendTerminate_before_enterDeep = 2;
		}
	#endif

	advertise_begin_tick = clock_time();
}



/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_SUSPEND_EXIT"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
_attribute_ram_code_ void	user_set_rf_power (u8 e, u8 *p, int n)
{
	rf_set_power_level_index (MY_RF_POWER_INDEX);
}


/**
 * @brief      callback function of LinkLayer Event "BLT_EV_FLAG_CONNECT"
 * @param[in]  e - LinkLayer Event type
 * @param[in]  p - data pointer of event
 * @param[in]  n - data length of event
 * @return     none
 */
void	task_connect (u8 e, u8 *p, int n)
{

#if (!UI_BUTTON_ENABLE) //if use button, do not use big latency
//	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 19, CONN_TIMEOUT_4S);  // 200mS
	// bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 99, CONN_TIMEOUT_4S);  // 1 S
	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 0, CONN_TIMEOUT_4S);  // 1 S
//	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 149, CONN_TIMEOUT_8S);  // 1.5 S
//	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 199, CONN_TIMEOUT_8S);  // 2 S
//	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 249, CONN_TIMEOUT_8S);  // 2.5 S
//	bls_l2cap_requestConnParamUpdate (CONN_INTERVAL_10MS, CONN_INTERVAL_10MS, 299, CONN_TIMEOUT_8S);  // 3 S
#endif

	latest_user_event_tick = clock_time();

	device_in_connection_state = 1;//

	interval_update_tick = clock_time() | 1; //none zero


	#if (UI_LED_ENABLE)
		gpio_write(GPIO_LED_RED, LED_ON_LEVAL);
		gpio_write(GPIO_LED_BLUE, !LED_ON_LEVAL);
	#endif
}


/**
 * @brief      power management code for application
 * @param	   none
 * @return     none
 */
_attribute_ram_code_ void blt_pm_proc(void)
{
#if(BLE_APP_PM_ENABLE)
	#if (PM_DEEPSLEEP_RETENTION_ENABLE)
		bls_pm_setSuspendMask (SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
	#else
		bls_pm_setSuspendMask (SUSPEND_ADV | SUSPEND_CONN);
	#endif

	//do not care about keyScan/button_detect power here, if you care about this, please refer to "8258_ble_remote" demo
	#if (UI_KEYBOARD_ENABLE)
		if(scan_pin_need || key_not_released){
			bls_pm_setSuspendMask (SUSPEND_DISABLE);
		}
	#endif


	#if (!TEST_CONN_CURRENT_ENABLE)   //test connection power, should disable deepSleep
			if(sendTerminate_before_enterDeep == 2){  //Terminate OK
				analog_write(USED_DEEP_ANA_REG, analog_read(USED_DEEP_ANA_REG) | CONN_DEEP_FLG);
				cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);  //deepSleep
			}


			if(  !blc_ll_isControllerEventPending() ){  //no controller event pending
				//adv 60s, deepsleep
				if( blc_ll_getCurrentState() == BLS_LINK_STATE_ADV && !sendTerminate_before_enterDeep && \
					clock_time_exceed(advertise_begin_tick , ADV_IDLE_ENTER_DEEP_TIME * 1000000))
				{
					cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_PAD, 0);  //deepsleep
				}
				//conn 60s no event(key/voice/led), enter deepsleep
				else if( device_in_connection_state && \
						clock_time_exceed(latest_user_event_tick, CONN_IDLE_ENTER_DEEP_TIME * 1000000) )
				{

					bls_ll_terminateConnection(HCI_ERR_REMOTE_USER_TERM_CONN); //push terminate cmd into ble TX buffer
					bls_ll_setAdvEnable(0);   //disable adv
					sendTerminate_before_enterDeep = 1;
				}
			}
	#endif  //end of !TEST_CONN_CURRENT_ENABLE
#endif  //end of BLE_APP_PM_ENABLE
}

void i2c_master_test_init(void)
{

	//I2C pin set
#if(MCU_CORE_TYPE == MCU_CORE_827x)
	i2c_gpio_set(I2C_GPIO_SDA_C0,I2C_GPIO_SCL_C1);  	//SDA/CK : C0/C1
#elif (MCU_CORE_TYPE == MCU_CORE_825x)
	i2c_gpio_set(I2C_GPIO_GROUP_C0C1);  	//SDA/CK : C0/C1
#endif

	//slave device id 0x5C(write) 0x5D(read)
	//i2c clock 200K, only master need set i2c clock
	// i2c_master_init(0x34, (unsigned char)(CLOCK_SYS_CLOCK_HZ/(4*200000)) );
	// i2c_master_init(0x34, (unsigned char)(CLOCK_SYS_CLOCK_HZ/(4*400000)) );
	i2c_master_init(0x34, (unsigned char)(CLOCK_SYS_CLOCK_HZ/(4*100000)));


}

volatile unsigned char i2c_master_rx_buff[43] = {0};
void i2c_master_mainloop(void)
{
	#define SLAVE_DMA_MODE_OTHER_DEV_WRITE    (0x46)
	#define SLAVE_DMA_MODE_OTHER_DEV_READ     (0x46)
		u8 addr = SLAVE_DMA_MODE_OTHER_DEV_READ;
		u8 len  = 0x2A;              // 手册说：长度不包含CRC
		// i2c_master_tx_buff[0] += 1;
		//825x slave dma mode, sram address(0x40000~0x4FFFF) length should be 3 byte
		// i2c_write_series(SLAVE_DMA_MODE_OTHER_DEV_WRITE, 1, (unsigned char *)i2c_master_tx_buff, DBG_DATA_LEN);
		// WaitMs(100);   //1 S
		i2c_read_series(((u16)addr << 8) | len,  2, (unsigned char *)i2c_master_rx_buff, len + 1);

	#if 0
		/*********** copy the data read by i2c master from slave for debug  ****************/
		memcpy( (unsigned char *)(master_rx_buff_debug + master_rx_index*DBG_DATA_LEN), (unsigned char *)i2c_master_rx_buff, DBG_DATA_LEN);
		master_rx_index ++;
		if(master_rx_index>=DBG_DATA_NUM){
			master_rx_index = 0;
		}
	#endif

}





/**
 * @brief		user initialization when MCU power on or wake_up from deepSleep mode
 * @param[in]	none
 * @return      none
 */
void user_init_normal(void)
{
	//random number generator must be initiated here( in the beginning of user_init_nromal)
	//when deepSleep retention wakeUp, no need initialize again
	random_generator_init();  //this is must



////////////////// BLE stack initialization ////////////////////////////////////
	u8  mac_public[6];
	u8  mac_random_static[6];
	//for 512K Flash, flash_sector_mac_address equals to 0x76000
	//for 1M  Flash, flash_sector_mac_address equals to 0xFF000
	blc_initMacAddress(flash_sector_mac_address, mac_public, mac_random_static);

	#if(BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_PUBLIC)
		app_own_address_type = OWN_ADDRESS_PUBLIC;
	#elif(BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_RANDOM_STATIC)
		app_own_address_type = OWN_ADDRESS_RANDOM;
		blc_ll_setRandomAddr(mac_random_static);
	#endif

	////// Controller Initialization  //////////
	blc_ll_initBasicMCU();                      //mandatory
	blc_ll_initStandby_module(mac_public);				//mandatory
	blc_ll_initAdvertising_module(mac_public); 	//adv module: 		 mandatory for BLE slave,
	blc_ll_initConnection_module();				//connection module  mandatory for BLE slave/master
	blc_ll_initSlaveRole_module();				//slave module: 	 mandatory for BLE slave,
	blc_ll_initPowerManagement_module();        //pm module:      	 optional



	////// Host Initialization  //////////
	blc_gap_peripheral_init();    //gap initialization
	my_att_init (); //gatt initialization
	blc_l2cap_register_handler (blc_l2cap_packet_receive);  	//l2cap initialization

	//Smp Initialization may involve flash write/erase(when one sector stores too much information,
	//   is about to exceed the sector threshold, this sector must be erased, and all useful information
	//   should re_stored) , so it must be done after battery check
#if (BLE_REMOTE_SECURITY_ENABLE)
	blc_smp_peripheral_init();
#else
	blc_smp_setSecurityLevel(No_Security);
#endif




///////////////////// USER application initialization ///////////////////
	bls_ll_setAdvData( (u8 *)tbl_advData, sizeof(tbl_advData) );
	bls_ll_setScanRspData( (u8 *)tbl_scanRsp, sizeof(tbl_scanRsp));




	////////////////// config adv packet /////////////////////
#if (BLE_REMOTE_SECURITY_ENABLE)
	u8 bond_number = blc_smp_param_getCurrentBondingDeviceNumber();  //get bonded device number
	smp_param_save_t  bondInfo;
	if(bond_number)   //at least 1 bonding device exist
	{
		bls_smp_param_loadByIndex( bond_number - 1, &bondInfo);  //get the latest bonding device (index: bond_number-1 )

	}

	if(bond_number)   //set direct adv
	{
		//set direct adv
		u8 status = bls_ll_setAdvParam( MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
										ADV_TYPE_CONNECTABLE_DIRECTED_LOW_DUTY, app_own_address_type,
										bondInfo.peer_addr_type,  bondInfo.peer_addr,
										MY_APP_ADV_CHANNEL,
										ADV_FP_NONE);
		if(status != BLE_SUCCESS) {  	while(1); }  //debug: adv setting err

		//it is recommended that direct adv only last for several seconds, then switch to indirect adv
		bls_ll_setAdvDuration(MY_DIRECT_ADV_TMIE, 1);
		bls_app_registerEventCallback (BLT_EV_FLAG_ADV_DURATION_TIMEOUT, &app_switch_to_indirect_adv);

	}
	else   //set indirect adv
#endif
	{
		u8 status = bls_ll_setAdvParam(  MY_ADV_INTERVAL_MIN, MY_ADV_INTERVAL_MAX,
										 ADV_TYPE_CONNECTABLE_UNDIRECTED, app_own_address_type,
										 0,  NULL,
										 MY_APP_ADV_CHANNEL,
										 ADV_FP_NONE);
		if(status != BLE_SUCCESS) {  	while(1); }  //debug: adv setting err
	}

	bls_ll_setAdvEnable(1);  //adv enable

	blc_ota_initOtaServer_module();

	//set rf power index, user must set it after every suspend wakeup, cause relative setting will be reset in suspend
	user_set_rf_power(0, 0, 0);
	bls_app_registerEventCallback (BLT_EV_FLAG_SUSPEND_EXIT, &user_set_rf_power);



	//ble event call back
	bls_app_registerEventCallback (BLT_EV_FLAG_CONNECT, &task_connect);
	bls_app_registerEventCallback (BLT_EV_FLAG_TERMINATE, &task_terminate);




	///////////////////// Power Management initialization///////////////////
#if(BLE_APP_PM_ENABLE)
	blc_ll_initPowerManagement_module();

	#if (PM_DEEPSLEEP_RETENTION_ENABLE)
	    blc_pm_setDeepsleepRetentionType(DEEPSLEEP_MODE_RET_SRAM_LOW16K); //default use 16k deep retention
		bls_pm_setSuspendMask (SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
		blc_pm_setDeepsleepRetentionThreshold(95, 95);

		#if(MCU_CORE_TYPE == MCU_CORE_825x)
			blc_pm_setDeepsleepRetentionEarlyWakeupTiming(TEST_CONN_CURRENT_ENABLE ? 240 : 260);
		#elif((MCU_CORE_TYPE == MCU_CORE_827x))
			blc_pm_setDeepsleepRetentionEarlyWakeupTiming(TEST_CONN_CURRENT_ENABLE ? 340 : 350);
		#else
		#endif
	#else
		bls_pm_setSuspendMask (SUSPEND_ADV | SUSPEND_CONN);
	#endif

	bls_app_registerEventCallback (BLT_EV_FLAG_SUSPEND_ENTER, &ble_remote_set_sleep_wakeup);
#else
	bls_pm_setSuspendMask (SUSPEND_DISABLE);
#endif


	#if (UI_KEYBOARD_ENABLE)
		/////////// keyboard gpio wakeup init ////////
		u32 pin[] = KB_DRIVE_PINS;
		for (int i=0; i<(sizeof (pin)/sizeof(*pin)); i++)
		{
			cpu_set_gpio_wakeup (pin[i], Level_High,1);  //drive pin pad high wakeup deepsleep
		}

		bls_app_registerEventCallback (BLT_EV_FLAG_GPIO_EARLY_WAKEUP, &proc_keyboard);
	#elif (UI_BUTTON_ENABLE)

		cpu_set_gpio_wakeup (SW1_GPIO, Level_Low,1);  //button pin pad low wakeUp suspend/deepSleep
		cpu_set_gpio_wakeup (SW2_GPIO, Level_Low,1);  //button pin pad low wakeUp suspend/deepSleep

		bls_app_registerEventCallback (BLT_EV_FLAG_GPIO_EARLY_WAKEUP, &proc_button);
	#endif


	advertise_begin_tick = clock_time();

	{
		i2c_master_test_init();

		LoadParam();
	}
}



/**
 * @brief		user initialization when MCU wake_up from deepSleep_retention mode
 * @param[in]	none
 * @return      none
 */
_attribute_ram_code_ void user_init_deepRetn(void)
{
#if (PM_DEEPSLEEP_RETENTION_ENABLE)

	blc_ll_initBasicMCU();   //mandatory
	rf_set_power_level_index (MY_RF_POWER_INDEX);

	blc_ll_recoverDeepRetention();

	DBG_CHN0_HIGH;    //debug

	irq_enable();

	#if (UI_KEYBOARD_ENABLE)
		/////////// keyboard gpio wakeup init ////////
		u32 pin[] = KB_DRIVE_PINS;
		for (int i=0; i<(sizeof (pin)/sizeof(*pin)); i++)
		{
			cpu_set_gpio_wakeup (pin[i], Level_High,1);  //drive pin pad high wakeup deepsleep
		}
	#elif (UI_BUTTON_ENABLE)

		cpu_set_gpio_wakeup (SW1_GPIO, Level_Low,1);  //button pin pad low wakeUp suspend/deepSleep
		cpu_set_gpio_wakeup (SW2_GPIO, Level_Low,1);  //button pin pad low wakeUp suspend/deepSleep
	#endif

#endif
}

// _attribute_data_retention_ u8 notify_data_test[20] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
// _attribute_data_retention_ u8 notify_data_test[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,0x08,0x09,0x0a,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
bool rev_master = false;

u8 Sci_CRC16RTU(u8 *pszBuf, u8 unLength)
{
	u16 CRCC = 0XFFFF;
	u32 CRC_count;

	for (CRC_count = 0; CRC_count < unLength; CRC_count++)
	{
		int i;

		CRCC = CRCC ^ *(pszBuf + CRC_count);

		for (i = 0; i < 8; i++)
		{
			if (CRCC & 1)
			{
				CRCC >>= 1;
				CRCC ^= 0xA001;
			}
			else
			{
				CRCC >>= 1;
			}
		}
	}

	return CRCC;
}


#define MAX_TEST_DATA_LEN   1024

// u8 test_buf[MAX_TEST_DATA_LEN];
u8 test_buf[MAX_TEST_DATA_LEN] = {
	0x01, 0x03, 0x4C, 
	0x0C, 0x6D, 0x0C, 0x6C, 0x0C, 0x6C, 0x0C, 0x5C,
    0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
    0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
    0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
    0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
    0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
    0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
    0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49, 0xEE, 0x49,
    0x0C, 0x6D, 0x0C, 0x5C, 0x00, 0x01, 0x00, 0x04, 
	0x00, 0x11, 0x04, 0xF6, 
	0xAD, 0x2E
};

void generate_test_data(int len)
{
    for(int i = 0; i < len; i++) {
        test_buf[i] = i & 0xFF;  // 有规律的数据，方便 checksum
    }
}

#define TELINK_NOTIFY_PAYLOAD   20   // MTU=23 时 payload = 20

ble_sts_t notify_big_packet(u16 conn, u16 handle, u8 *data, u16 len)
{
    u16 offset = 0;

    while (offset < len)
    {
        u8 chunk = (len - offset) > TELINK_NOTIFY_PAYLOAD ?
                    TELINK_NOTIFY_PAYLOAD :
                    (len - offset);

        ble_sts_t ret = blc_gatt_pushHandleValueNotify(
                            conn,
                            handle,
                            data + offset,
                            chunk);

        if(ret != BLE_SUCCESS) {
            printf("Send FAIL offset=%d ret=%x\r\n", offset, ret);
            return ret;
        }

        offset += chunk;

        // Telink 特性：需要给对端一点时间，否则 notify 会被吞
        // sleep_us(800);   // 0.8ms足够安全
    }

    return BLE_SUCCESS;
}

static int soc = 0;     // 当前 SOC
int get_soc(void)
{
	return soc;
}
int simulate_soc(void)
{
    static int dir = 1;     // 1: 增加, -1: 减少
#if 0
    {
        soc += dir;

        // 到达上限，反向
        if (soc >= 100)
        {
            soc = 100;
            dir = -1;
        }
        // 到达下限，反向
        else if (soc <= 0)
        {
            soc = 0;
            dir = 1;
        }
    }
#endif
	++soc;
	return soc;
}
const u16 protect_para[65] = {
	3400, 3500, 3600, 3500, 100,
	3000, 3000, 3000, 3100, 100,
	12000, 12000, 12000, 11000, 100,
	9000, 9000, 9000, 10000, 100,
	800, 800, 800, 100, 10,
	800, 800, 800, 100, 10,
	1000, 1000, 1000, 960, 100,
	400, 400, 400, 450, 200,
	1000, 1000, 1000, 960, 100,
	400, 400, 400, 450, 500,
	1000, 1000, 1000, 960, 100,
	100, 200, 300, 100, 100,
	20, 10, 5, 6, 100,
};
const u16 soc_para[25] = {
	600, 600, 600, 600, 600, 600,
	580, 590, 600,
	1280,
	1280, 370,
	88, 0,
	66,
	100,
	6600,
	10000,
	10000,
	30,
	0,0,0,
	0,0
};

const u16 protect_status[21] = {
	1, 1, 1,
	1, 2, 
	1, 2,
	1, 2,
	0x0101,
	1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0,
};

const u16 other_status[12] = {
	0x08, 0, 
	0, 0, 
	0, 0, 
	0, 0, 
	0, 0, 
	0, 0, 
};

void notify_other_status(void)
{
	printf("notify_other_status");
	int len = 3 + 12*2 + 2;
	test_buf[0] = 0x01;
	test_buf[1] = 0x03;
	test_buf[2] = 12 * 2;

	size_t i;
	for (i = 0; i < 12; i++)
	{
		test_buf[3 + i * 2] =  other_status[i] >> 8;
		test_buf[4 + i * 2] =  other_status[i] & 0xff;
	}

	i++;
	u16 crc = Sci_CRC16RTU(test_buf, len - 2);
	test_buf[3 + i * 2] = crc & 0xff;
	test_buf[4 + i * 2] = crc >> 8;

	 ble_sts_t r = notify_big_packet(
                    BLS_CONN_HANDLE,
                    SPP_CLIENT_TO_SERVER_DP_H,   // 你的 notify 句柄
                    test_buf,
                    len);
}
void notify_protect_status(void)
{
	printf(" notify_protect_status");
	int len = 3 + 21*2 + 2;
	test_buf[0] = 0x01;
	test_buf[1] = 0x03;
	test_buf[2] = 21 * 2;

	size_t i;
	for (i = 0; i < 21; i++)
	{
		test_buf[3 + i * 2] =  protect_status[i] >> 8;
		test_buf[4 + i * 2] =  protect_status[i] & 0xff;
	}

	i++;
	u16 crc = Sci_CRC16RTU(test_buf, len - 2);
	test_buf[3 + i * 2] = crc & 0xff;
	test_buf[4 + i * 2] = crc >> 8;

	 ble_sts_t r = notify_big_packet(
                    BLS_CONN_HANDLE,
                    SPP_CLIENT_TO_SERVER_DP_H,   // 你的 notify 句柄
                    test_buf,
                    len);
}

void notify_soc(void)
{
	printf("notify_soc");
	int len = 3 + 25*2 + 2;
	test_buf[0] = 0x01;
	test_buf[1] = 0x03;
	test_buf[2] = 25 * 2;

	size_t i;
	for (i = 0; i < 25; i++)
	{
		test_buf[3 + i * 2] =  soc_para[i] >> 8;
		test_buf[4 + i * 2] =  soc_para[i] & 0xff;
	}

	i++;
	u16 crc = Sci_CRC16RTU(test_buf, len - 2);
	test_buf[3 + i * 2] = crc & 0xff;
	test_buf[4 + i * 2] = crc >> 8;

	 ble_sts_t r = notify_big_packet(
                    BLS_CONN_HANDLE,
                    SPP_CLIENT_TO_SERVER_DP_H,   // 你的 notify 句柄
                    test_buf,
                    len);
}

void notify_protect_prarm(void)
{
	printf("notify_protect_prarm");
	int len = 3 + 65*2 + 2;
	test_buf[0] = 0x01;
	test_buf[1] = 0x03;
	test_buf[2] = 65 * 2;
	// u16 *p = &g_tParam.protect;
	u16 *p = &g_tParam.protect;

	size_t i;
	for (i = 0; i < 65; i++)
	{
		// test_buf[3 + i * 2] =  protect_para[i] >> 8;
		// test_buf[4 + i * 2] =  protect_para[i] & 0xff;
		test_buf[3 + i * 2] =  *p >> 8;
		test_buf[4 + i * 2] =  *p & 0xff;
		p++;
	}

	i++;
	u16 crc = Sci_CRC16RTU(test_buf, len - 2);
	test_buf[3 + i * 2] = crc & 0xff;
	test_buf[4 + i * 2] = crc >> 8;

	 ble_sts_t r = notify_big_packet(
                    BLS_CONN_HANDLE,
                    SPP_CLIENT_TO_SERVER_DP_H,   // 你的 notify 句柄
                    test_buf,
                    len);

}

void notify_votage(void)
{
	int len = 3 + 38*2 + 2;  // 你想测多少就填多少
	printf("notify voltage");

	static u8 vol_cnt = 0;
	vol_cnt++;
	{
				u16 vol = 2500 + vol_cnt;
				test_buf[0] = 0x01;
				test_buf[1] = 0x03;
				test_buf[2] = 38 * 2;

				for (size_t i = 0; i < 39; i++)
				{
					if(i <= 16)
					{
						int temp = i2c_master_rx_buff[8 + 2*i];
						temp = temp << 8 | i2c_master_rx_buff[9+ 2*i];
						temp = temp * 5 /32;
						//todo flash 与soc
						if(i == 1)
							temp = get_soc();
						test_buf[3 + i * 2] =  temp >> 8;
						test_buf[4 + i * 2] =  temp & 0xff;
					}
					if(i == 32)
					{
						test_buf[3 + i * 2] =  (3500 + vol_cnt) >> 8;
						test_buf[4 + i * 2] =  (3500 + vol_cnt) & 0xff;
					}
					else if(i == 33)
					{
						test_buf[3 + i * 2] =  (2000 + vol_cnt) >> 8;
						test_buf[4 + i * 2] =  (2000 + vol_cnt) & 0xff;
					}
					else if(i == 34)
					{
						test_buf[3 + i * 2] =  1 >> 8;
						test_buf[4 + i * 2] =  1 & 0xff;
					}
					else if(i == 35)
					{
						test_buf[3 + i * 2] =  2 >> 8;
						test_buf[4 + i * 2] =  2 & 0xff;
					}
					else if(i == 36)
					{
						test_buf[3 + i * 2] =  (0 + vol_cnt) >> 8;
						test_buf[4 + i * 2] =  (0 + vol_cnt) & 0xff;
					}
					else if(i == 37)
					{
						test_buf[3 + i * 2] =  (1000 + vol_cnt) >> 8;
						test_buf[4 + i * 2] =  (1000 + vol_cnt) & 0xff;
					}
					else if(i == 38)
					{
						u16 crc = Sci_CRC16RTU(test_buf, len - 2);
						test_buf[3 + i * 2] = crc & 0xff;
						test_buf[4 + i * 2] = crc >> 8;
					}
				}

				
			}

			 ble_sts_t r = notify_big_packet(
                    BLS_CONN_HANDLE,
                    SPP_CLIENT_TO_SERVER_DP_H,   // 你的 notify 句柄
                    test_buf,
                    len);
}

/**
 * @brief     BLE main loop
 * @param[in]  none.
 * @return     none.
 */
void main_loop (void)
{
	////////////////////////////////////// BLE entry /////////////////////////////////
	blt_sdk_main_loop();


	////////////////////////////////////// UI entry /////////////////////////////////
	#if (UI_KEYBOARD_ENABLE)
			proc_keyboard (0,0, 0);
	#elif (UI_BUTTON_ENABLE)
			// process button 1 second later after power on, to avoid power unstable
			if(!button_detect_en && clock_time_exceed(0, 1000000)){
				button_detect_en = 1;
			}
			if(button_detect_en && clock_time_exceed(button_detect_tick, 5000))
			{
				button_detect_tick = clock_time();
				proc_button(0, 0, 0);  //button triggers pair & unpair  and OTA
			}
	#endif
	/*
	ffff
	fff0
	ffef
	ffea
	*/

	_attribute_data_retention_ static u32 update_bms_info_tick = 0;
	if(clock_time_exceed(update_bms_info_tick , 1000 * 1000))
	{
		update_bms_info_tick = clock_time();
		gpio_toggle(GPIO_LED_BLUE);
		i2c_master_mainloop();
		//todo 1s擦写一次flash，并notify
void update_my_batVal(void);
		// update_my_batVal();
		simulate_soc();
		// printf("device_in_connection_state && rev_master");
		u8 test_buf[16] = {0,1,2,3,4,5,6,7,8,9,0xa,0xb,0xc,0xd,0xe,0xf};
		// array_printf(test_buf, sizeof(test_buf));
		extern u32 rev_cnt;
		// printf("rev cnt %d", rev_cnt);
	}

	{
		// if(device_in_connection_state && clock_time_exceed(interval_update_tick, 1000*1000))
		if(device_in_connection_state && rev_master)
		{
extern u16 addr;
			rev_master = false;
			if(addr == 0xd000)
				notify_votage();
			else if (addr == 0x2100)
				notify_protect_prarm();
			else if (addr == 0xd026)
				notify_soc();
			else if (addr == 0xd115)
				notify_other_status();
			else if (addr == 0xd100)
				notify_protect_status();
		#if 0
			u8 remain = sizeof(notify_data_test);
			// u8 remain = 30;
			u8 *p = notify_data_test;
			rev_master = false;
			ble_sts_t ret;
			interval_update_tick = clock_time();
			// ret = blc_gatt_pushHandleValueNotify(BLS_CONN_HANDLE, SPP_SERVER_TO_CLIENT_DP_H, notify_data_test, 8);
			// ret = blc_gatt_pushHandleValueNotify(BLS_CONN_HANDLE, SPP_CLIENT_TO_SERVER_DP_H, notify_data_test, sizeof(notify_data_test));
		#endif
		}

	}


	////////////////////////////////////// PM Process /////////////////////////////////
	#if (UI_KEYBOARD_ENABLE)
			blt_pm_proc();
	#elif (UI_BUTTON_ENABLE)
			if(button_not_released){
				bls_pm_setSuspendMask (SUSPEND_DISABLE);
			}
			else{
				bls_pm_setSuspendMask (SUSPEND_ADV | SUSPEND_CONN);
			}
	#endif
}


