################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../application/print/putchar.c \
../application/print/u_printf.c 

OBJS += \
./application/print/putchar.o \
./application/print/u_printf.o 


# Each subdirectory must supply rules for building sources it contributes
application/print/%.o: ../application/print/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: TC32 Compiler'
	tc32-elf-gcc -ffunction-sections -fdata-sections -I"D:\telink\TLSR-8258\TLSR-8258\B85M_SINGLE_BLE_SDK(1)\b85_ble_sdk" -I"D:\telink\TLSR-8258\TLSR-8258\B85M_SINGLE_BLE_SDK(1)\b85_ble_sdk\drivers\8258" -I"D:\telink\TLSR-8258\TLSR-8258\B85M_SINGLE_BLE_SDK(1)\b85_ble_sdk\vendor\common" -D__PROJECT_8258_MODULE__=1 -DCHIP_TYPE=CHIP_TYPE_825x -Wall -O2 -fpack-struct -fshort-enums -finline-small-functions -std=gnu99 -fshort-wchar -fms-extensions -c -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


