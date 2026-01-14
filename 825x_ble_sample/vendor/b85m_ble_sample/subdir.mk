################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../vendor/b85m_ble_sample/SocEnhance.c \
../vendor/b85m_ble_sample/app.c \
../vendor/b85m_ble_sample/app_att.c \
../vendor/b85m_ble_sample/button.c \
../vendor/b85m_ble_sample/main.c \
../vendor/b85m_ble_sample/modbus_rtu.c \
../vendor/b85m_ble_sample/modbus_uart.c \
../vendor/b85m_ble_sample/param.c \
../vendor/b85m_ble_sample/sh367309_datadeal.c \
../vendor/b85m_ble_sample/sif_send.c \
../vendor/b85m_ble_sample/soc_kv_store.c 

OBJS += \
./vendor/b85m_ble_sample/SocEnhance.o \
./vendor/b85m_ble_sample/app.o \
./vendor/b85m_ble_sample/app_att.o \
./vendor/b85m_ble_sample/button.o \
./vendor/b85m_ble_sample/main.o \
./vendor/b85m_ble_sample/modbus_rtu.o \
./vendor/b85m_ble_sample/modbus_uart.o \
./vendor/b85m_ble_sample/param.o \
./vendor/b85m_ble_sample/sh367309_datadeal.o \
./vendor/b85m_ble_sample/sif_send.o \
./vendor/b85m_ble_sample/soc_kv_store.o 


# Each subdirectory must supply rules for building sources it contributes
vendor/b85m_ble_sample/%.o: ../vendor/b85m_ble_sample/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: TC32 Compiler'
	tc32-elf-gcc -ffunction-sections -fdata-sections -I"D:\telink work\telink-sdk-old" -I"D:\telink work\telink-sdk-old\drivers\8258" -D__PROJECT_8258_BLE_SAMPLE__=1 -DCHIP_TYPE=CHIP_TYPE_825x -Wall -O2 -fpack-struct -fshort-enums -finline-small-functions -std=gnu99 -fshort-wchar -fms-extensions -c -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


