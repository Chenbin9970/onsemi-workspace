# RSL10 Peripheral Server Sleep — Build Configuration

TC    = /c/Program Files (x86)/onsemi/IDE_V4.3.1.132/arm_tools/bin
CMSIS = /c/Users/admin/AppData/Local/Arm/Packs/ONSemiconductor/RSL10/3.7.606

CC      = "$(TC)/arm-none-eabi-gcc"
AS      = "$(TC)/arm-none-eabi-gcc"
LD      = "$(TC)/arm-none-eabi-gcc"
OBJCOPY = "$(TC)/arm-none-eabi-objcopy"
SIZE    = "$(TC)/arm-none-eabi-size"

BUILD_DIR = build

# App source files
C_SRCS = app.c code/app_init.c code/app_process.c code/ble_bass.c \
         code/ble_custom.c code/ble_std.c code/calibration.c \
         code/rm_event.c code/rm_pkt_hdl.c code/config_data.c \
         code/dsp_pm_dm.c code/audio_func.c code/rm_app.c code/queue.c \
         RTE/Device/RSL10/system_rsl10.c \
         RTE/Device/RSL10/rsl10_protocol.c

# Syslib (local copies for Makefile build; excluded from IDE via .cproject)
SYSLIB_SRCS = syslib/rsl10_sys_clocks.c syslib/rsl10_sys_power.c \
              syslib/rsl10_sys_power_modes.c syslib/rsl10_sys_rffe.c \
              syslib/rsl10_sys_dma.c syslib/rsl10_sys_flash.c \
              syslib/rsl10_sys_timers.c

# Assembly
ASM_SRCS = code/wakeup_asm.S RTE/Device/RSL10/startup_rsl10.S

OBJS = $(addprefix $(BUILD_DIR)/,$(C_SRCS:.c=.o) $(SYSLIB_SRCS:.c=.o) $(ASM_SRCS:.S=.o))

# Includes
INCLUDES = -Iinclude \
           -I$(CMSIS)/include \
           -I$(CMSIS)/include/bb \
           -I$(CMSIS)/include/ble \
           -I$(CMSIS)/include/ble/profiles \
           -I$(CMSIS)/include/kernel \
           -IRTE -IRTE/Device/RSL10

# Defines
DEFINES = -DRSL10_CID=101 -DCFG_CON=8 -DCFG_BLE=1 -DCFG_SLEEP \
          -DCFG_HW_AUDIO -DCFG_ALLROLES=1 -DCFG_APP -DCFG_APP_BATT \
          -DCFG_ATTS=1 -DCFG_EMB=1 -DCFG_HOST=1 -DCFG_RF_ATLAS=1 \
          -DCFG_ALLPRF=1 -DCFG_PRF=1 -DCFG_NB_PRF=2 -DCFG_CHNL_ASSESS=1 \
          -DCFG_SEC_CON=1 -DCFG_EXT_DB -DCFG_PRF_BASS=1 -D_RTE_

# Compiler flags (Debug)
CFLAGS = -mcpu=cortex-m3 -mthumb -Wall -Wmissing-prototypes \
         -fsigned-char -ffunction-sections -fdata-sections \
         -O0 -g3 $(DEFINES) $(INCLUDES)

# Assembler
ASFLAGS = -mcpu=cortex-m3 -mthumb -x assembler-with-cpp $(DEFINES) $(INCLUDES)

# Linker
LDFLAGS = -mcpu=cortex-m3 -mthumb -Wl,--defsym,end=0 \
          --specs=nano.specs --specs=nosys.specs \
          -Wl,--gc-sections -TRTE/Device/RSL10/sections.ld

LIBS = -L$(CMSIS)/lib/ble_core/Release \
       -L$(CMSIS)/lib/ble_profiles/Release \
       -lblelib -lkelib -lbass -lweak_prf

TARGET = $(BUILD_DIR)/peripheral_server_sleep

.PHONY: all clean

all: $(BUILD_DIR) $(TARGET).elf
	@echo "=== BUILD COMPLETE ==="
	@$(SIZE) $(TARGET).elf

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/code
	mkdir -p $(BUILD_DIR)/RTE/Device/RSL10
	mkdir -p $(BUILD_DIR)/syslib

# C compilation (project sources)
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "[CC] $<"
	@$(CC) -c $(CFLAGS) -o $@ $<

# Assembly
$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	@echo "[AS] $<"
	@$(AS) -c $(ASFLAGS) -o $@ $<

# Linking
$(TARGET).elf: $(OBJS)
	@echo "[LD] $@"
	@$(LD) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

clean:
	rm -rf $(BUILD_DIR)
