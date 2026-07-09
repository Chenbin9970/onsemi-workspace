################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../code/app_func.c \
../code/app_init.c \
../code/app_process.c \
../code/ble_bass.c \
../code/ble_custom.c \
../code/ble_std.c \
../code/bs300_calib.c \
../code/bs300_driver.c \
../code/bs300_hal.c \
../code/bs300_param_encode.c \
../code/bs300_program_read.c \
../code/bs300_ram_sync.c \
../code/bs300_startup.c \
../code/bs300_storage.c \
../code/bs300_test.c \
../code/calibration.c \
../code/dsp_pm_dm.c \
../code/queue.c \
../code/rm_app.c 

S_UPPER_SRCS += \
../code/wakeup_asm.S 

OBJS += \
./code/app_func.o \
./code/app_init.o \
./code/app_process.o \
./code/ble_bass.o \
./code/ble_custom.o \
./code/ble_std.o \
./code/bs300_calib.o \
./code/bs300_driver.o \
./code/bs300_hal.o \
./code/bs300_param_encode.o \
./code/bs300_program_read.o \
./code/bs300_ram_sync.o \
./code/bs300_startup.o \
./code/bs300_storage.o \
./code/bs300_test.o \
./code/calibration.o \
./code/dsp_pm_dm.o \
./code/queue.o \
./code/rm_app.o \
./code/wakeup_asm.o 

S_UPPER_DEPS += \
./code/wakeup_asm.d 

C_DEPS += \
./code/app_func.d \
./code/app_init.d \
./code/app_process.d \
./code/ble_bass.d \
./code/ble_custom.d \
./code/ble_std.d \
./code/bs300_calib.d \
./code/bs300_driver.d \
./code/bs300_hal.d \
./code/bs300_param_encode.d \
./code/bs300_program_read.d \
./code/bs300_ram_sync.d \
./code/bs300_startup.d \
./code/bs300_storage.d \
./code/bs300_test.d \
./code/calibration.d \
./code/dsp_pm_dm.d \
./code/queue.d \
./code/rm_app.d 


# Each subdirectory must supply rules for building sources it contributes
code/%.o: ../code/%.c code/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross ARM C Compiler'
	arm-none-eabi-gcc -Wall -mcpu=cortex-m3 -mthumb -O2 -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections  -g3 -DRSL10_CID=101 -DCFG_CON=8 -DCFG_BLE=1 -DCFG_SLEEP -DCFG_HW_AUDIO -DCFG_ALLROLES=1 -DCFG_APP -DCFG_APP_BATT -DCFG_ATTS=1 -DCFG_EMB=1 -DCFG_HOST=1 -DCFG_RF_ATLAS=1 -DCFG_ALLPRF=1 -DCFG_PRF=1 -DCFG_NB_PRF=2 -DCFG_CHNL_ASSESS=1 -DCFG_SEC_CON=1 -DCFG_EXT_DB -DCFG_PRF_BASS=1 -D_RTE_ -I"C:\Users\admin\onsemi-workspace4.5\peripheral_server_sleep\include" -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include" -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/bb" -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/ble" -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/ble/profiles" -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/kernel" -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/source/firmware/printf" -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/source/firmware/rtt" -I"C:\Users\admin\onsemi-workspace4.5\peripheral_server_sleep/RTE" -I"C:\Users\admin\onsemi-workspace4.5\peripheral_server_sleep/RTE/Device/RSL10" -I"C:\Users\admin\onsemi-workspace4.5\peripheral_server_sleep/RTE/Utility" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/bb" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/ble" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/ble/profiles" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/kernel" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/source/firmware/printf" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/source/firmware/rtt" -isystem"C:\Users\admin\onsemi-workspace4.5\peripheral_server_sleep/RTE" -isystem"C:\Users\admin\onsemi-workspace4.5\peripheral_server_sleep/RTE/Device/RSL10" -isystem"C:\Users\admin\onsemi-workspace4.5\peripheral_server_sleep/RTE/Utility" -std=gnu11 -Wmissing-prototypes -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '

code/%.o: ../code/%.S code/subdir.mk
	@echo 'Building file: $<'
	@echo 'Invoking: Cross ARM GNU Assembler'
	arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -O2 -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections  -g3 -x assembler-with-cpp -D_RTE_ -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include" -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/bb" -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/ble" -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/ble/profiles" -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/kernel" -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/source/firmware/printf" -I"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/source/firmware/rtt" -I"C:\Users\admin\onsemi-workspace4.5\peripheral_server_sleep/RTE" -I"C:\Users\admin\onsemi-workspace4.5\peripheral_server_sleep/RTE/Device/RSL10" -I"C:\Users\admin\onsemi-workspace4.5\peripheral_server_sleep/RTE/Utility" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/bb" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/ble" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/ble/profiles" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/include/kernel" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/source/firmware/printf" -isystem"C:/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606/source/firmware/rtt" -isystem"C:\Users\admin\onsemi-workspace4.5\peripheral_server_sleep/RTE" -isystem"C:\Users\admin\onsemi-workspace4.5\peripheral_server_sleep/RTE/Device/RSL10" -isystem"C:\Users\admin\onsemi-workspace4.5\peripheral_server_sleep/RTE/Utility" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" -c -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


