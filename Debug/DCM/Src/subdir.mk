################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (9-2020-q2-update)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../DCM/Src/SIGMA_dcm_core.c \
../DCM/Src/SIGMA_flash.c \
../DCM/Src/SIGMA_io_control.c \
../DCM/Src/SIGMA_iso_tp.c \
../DCM/Src/SIGMA_uds.c 

OBJS += \
./DCM/Src/SIGMA_dcm_core.o \
./DCM/Src/SIGMA_flash.o \
./DCM/Src/SIGMA_io_control.o \
./DCM/Src/SIGMA_iso_tp.o \
./DCM/Src/SIGMA_uds.o 

C_DEPS += \
./DCM/Src/SIGMA_dcm_core.d \
./DCM/Src/SIGMA_flash.d \
./DCM/Src/SIGMA_io_control.d \
./DCM/Src/SIGMA_iso_tp.d \
./DCM/Src/SIGMA_uds.d 


# Each subdirectory must supply rules for building sources it contributes
DCM/Src/SIGMA_dcm_core.o: ../DCM/Src/SIGMA_dcm_core.c DCM/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F411xE -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/include" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/include/cipher" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/legacy_v3/include" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/legacy_v3/include/cipher" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/DCM/Inc" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP -MF"DCM/Src/SIGMA_dcm_core.d" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
DCM/Src/SIGMA_flash.o: ../DCM/Src/SIGMA_flash.c DCM/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F411xE -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/include" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/include/cipher" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/legacy_v3/include" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/legacy_v3/include/cipher" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/DCM/Inc" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP -MF"DCM/Src/SIGMA_flash.d" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
DCM/Src/SIGMA_io_control.o: ../DCM/Src/SIGMA_io_control.c DCM/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F411xE -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/include" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/include/cipher" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/legacy_v3/include" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/legacy_v3/include/cipher" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/DCM/Inc" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP -MF"DCM/Src/SIGMA_io_control.d" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
DCM/Src/SIGMA_iso_tp.o: ../DCM/Src/SIGMA_iso_tp.c DCM/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F411xE -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/include" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/include/cipher" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/legacy_v3/include" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/legacy_v3/include/cipher" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/DCM/Inc" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP -MF"DCM/Src/SIGMA_iso_tp.d" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"
DCM/Src/SIGMA_uds.o: ../DCM/Src/SIGMA_uds.c DCM/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F411xE -c -I../Core/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/include" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/include/cipher" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/legacy_v3/include" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/STM32_Cryptographic/legacy_v3/include/cipher" -I"C:/Users/HP/Documents/work_space/SIGMA_UDS_TP/DCM/Inc" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP -MF"DCM/Src/SIGMA_uds.d" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

