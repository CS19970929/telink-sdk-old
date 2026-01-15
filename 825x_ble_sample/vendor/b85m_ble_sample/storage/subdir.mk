################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../vendor/b85m_ble_sample/storage/st_flash_port.c \
../vendor/b85m_ble_sample/storage/st_kv.c \
../vendor/b85m_ble_sample/storage/st_log.c \
../vendor/b85m_ble_sample/storage/st_storage.c \
../vendor/b85m_ble_sample/storage/st_test.c 

OBJS += \
./vendor/b85m_ble_sample/storage/st_flash_port.o \
./vendor/b85m_ble_sample/storage/st_kv.o \
./vendor/b85m_ble_sample/storage/st_log.o \
./vendor/b85m_ble_sample/storage/st_storage.o \
./vendor/b85m_ble_sample/storage/st_test.o 


# Each subdirectory must supply rules for building sources it contributes
vendor/b85m_ble_sample/storage/%.o: ../vendor/b85m_ble_sample/storage/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: TC32 Compiler'
	tc32-elf-gcc -ffunction-sections -fdata-sections -I"D:\telink\TLSR-8258\TLSR-8258\B85M_SINGLE_BLE_SDK(1)\b85_ble_sdk" -I"D:\telink\TLSR-8258\TLSR-8258\B85M_SINGLE_BLE_SDK(1)\b85_ble_sdk\drivers\8258" -I"D:\telink\TLSR-8258\TLSR-8258\B85M_SINGLE_BLE_SDK(1)\b85_ble_sdk\vendor\b85m_ble_sample\storage" -D__PROJECT_8258_BLE_SAMPLE__=1 -DCHIP_TYPE=CHIP_TYPE_825x -Wall -O2 -fpack-struct -fshort-enums -finline-small-functions -std=gnu99 -fshort-wchar -fms-extensions -c -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


