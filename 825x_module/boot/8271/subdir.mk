################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
S_UPPER_SRCS += \
../boot/8271/cstartup_8271_RET_16K.S \
../boot/8271/cstartup_8271_RET_32K.S 

OBJS += \
./boot/8271/cstartup_8271_RET_16K.o \
./boot/8271/cstartup_8271_RET_32K.o 


# Each subdirectory must supply rules for building sources it contributes
boot/8271/%.o: ../boot/8271/%.S
	@echo 'Building file: $<'
	@echo 'Invoking: TC32 CC/Assembler'
	tc32-elf-gcc -DMCU_STARTUP_8258_RET_16K -c -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


