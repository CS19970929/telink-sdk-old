################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../application/usbstd/usb.c \
../application/usbstd/usbdesc.c \
../application/usbstd/usbhw.c 

OBJS += \
./application/usbstd/usb.o \
./application/usbstd/usbdesc.o \
./application/usbstd/usbhw.o 


# Each subdirectory must supply rules for building sources it contributes
application/usbstd/%.o: ../application/usbstd/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: TC32 Compiler'
	tc32-elf-gcc -ffunction-sections -fdata-sections -I"D:\telink\TLSR-8258\TLSR-8258\B85M_SINGLE_BLE_SDK(1)\b85_ble_sdk" -I"D:\telink\TLSR-8258\TLSR-8258\B85M_SINGLE_BLE_SDK(1)\b85_ble_sdk\drivers\8258" -D__PROJECT_8258_BQB_LOWER_TESTER__=1 -DCHIP_TYPE=CHIP_TYPE_825x -Wall -O2 -fpack-struct -fshort-enums -finline-small-functions -std=gnu99 -fshort-wchar -fms-extensions -c -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


