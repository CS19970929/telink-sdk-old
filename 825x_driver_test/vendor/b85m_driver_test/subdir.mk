################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../vendor/b85m_driver_test/app.c \
../vendor/b85m_driver_test/app_adc.c \
../vendor/b85m_driver_test/app_emi.c \
../vendor/b85m_driver_test/app_gpio_irq.c \
../vendor/b85m_driver_test/app_i2c.c \
../vendor/b85m_driver_test/app_i2c_master.c \
../vendor/b85m_driver_test/app_i2c_slave.c \
../vendor/b85m_driver_test/app_pwm.c \
../vendor/b85m_driver_test/app_spi.c \
../vendor/b85m_driver_test/app_spi_master.c \
../vendor/b85m_driver_test/app_spi_slave.c \
../vendor/b85m_driver_test/app_timer.c \
../vendor/b85m_driver_test/app_uart.c \
../vendor/b85m_driver_test/main.c \
../vendor/b85m_driver_test/test_low_power.c 

OBJS += \
./vendor/b85m_driver_test/app.o \
./vendor/b85m_driver_test/app_adc.o \
./vendor/b85m_driver_test/app_emi.o \
./vendor/b85m_driver_test/app_gpio_irq.o \
./vendor/b85m_driver_test/app_i2c.o \
./vendor/b85m_driver_test/app_i2c_master.o \
./vendor/b85m_driver_test/app_i2c_slave.o \
./vendor/b85m_driver_test/app_pwm.o \
./vendor/b85m_driver_test/app_spi.o \
./vendor/b85m_driver_test/app_spi_master.o \
./vendor/b85m_driver_test/app_spi_slave.o \
./vendor/b85m_driver_test/app_timer.o \
./vendor/b85m_driver_test/app_uart.o \
./vendor/b85m_driver_test/main.o \
./vendor/b85m_driver_test/test_low_power.o 


# Each subdirectory must supply rules for building sources it contributes
vendor/b85m_driver_test/%.o: ../vendor/b85m_driver_test/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: TC32 Compiler'
	tc32-elf-gcc -ffunction-sections -fdata-sections -I"D:\telink\TLSR-8258\TLSR-8258\B85M_SINGLE_BLE_SDK(1)\b85_ble_sdk" -I"D:\telink\TLSR-8258\TLSR-8258\B85M_SINGLE_BLE_SDK(1)\b85_ble_sdk\drivers\8258" -D__PROJECT_8258_DRIVER_TEST__=1 -DCHIP_TYPE=CHIP_TYPE_825x -Wall -O2 -fpack-struct -fshort-enums -finline-small-functions -std=gnu99 -fshort-wchar -fms-extensions -c -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


