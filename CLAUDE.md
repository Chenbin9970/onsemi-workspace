# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ON Semiconductor RSL10 BLE peripheral server with sleep mode. Targets the RSL10 SoC (Cortex-M3, 48 MHz). The device advertises a Battery Service and a Custom Service, accepts connections from a central device, sends battery level notifications, and supports read/write of custom characteristics. Sleep mode is entered between radio events to save power.

## Build System

Eclipse-based project using GNU ARM Eclipse plugin. Toolchain: `arm-none-eabi-*` (GNU Tools for ARM Embedded).

**Two build configurations** (defined in `.cproject`):
- **Debug** — `-O0`, max debug info (`-g3`)
- **Release** — `-Os`, no debug info

**Key compiler defines:** `CFG_SLEEP`, `CFG_BLE=1`, `CFG_APP`, `CFG_APP_BATT`, `CFG_PRF_BASS=1`, `RSL10_CID=101`, `_RTE_`, plus many BLE feature flags. See [.cproject](peripheral_server_sleep/.cproject) for the full list.

**SDK:** RSL10 CMSIS-Pack v3.7.606, expected at `${cmsis_pack_root}/ONSemiconductor/RSL10/3.7.606/`.

**Pre-built libraries linked:**
- `libblelib.a` — BLE core stack (Release variant, supports 2 Mbps)
- `libkelib.a` — Kernel/scheduler
- `libbass.a` — Battery Service Server profile

**Linker script:** [sections.ld](peripheral_server_sleep/RTE/Device/RSL10/sections.ld) — places sleep/wakeup routines (`.app_wakeup`, `.sys_powermodes_sleep`) in retention DRAM. DRAM configured as 3×8K-24 bytes (24 bytes reserved for wakeup info).

## Architecture

The application is structured around a cooperative kernel scheduler (`Kernel_Schedule()`) called in the main loop. The app transitions through five states: `APPM_INIT` → `APPM_CREATE_DB` → `APPM_READY` → `APPM_ADVERTISING` → `APPM_CONNECTED`.

### Source files (`code/`)

| File | Role |
|------|------|
| [app.c](peripheral_server_sleep/app.c) | `main()` — initialization, then `Main_Loop()` which runs the kernel, reads battery level, sends notifications, and enters sleep mode |
| [app_init.c](peripheral_server_sleep/code/app_init.c) | Orchestrates all subsystem initialization |
| [app_process.c](peripheral_server_sleep/code/app_process.c) | Application-level message handlers |
| [ble_std.c](peripheral_server_sleep/code/ble_std.c) | Standard BLE operations: GAP configuration, advertising, connection management |
| [ble_bass.c](peripheral_server_sleep/code/ble_bass.c) | Battery Service Server — ADC-based battery level measurement (16-sample moving average), notification sending |
| [ble_custom.c](peripheral_server_sleep/code/ble_custom.c) | Custom Service Server — read/write characteristics, periodic notifications every 10 sleep cycles |
| [calibration.c](peripheral_server_sleep/code/calibration.c) | Voltage regulator trim values — supports three modes: factory calibration (`MANU_CALIB`), supplemental NVR3 (`SUPPLEMENTAL_CALIB`), or runtime calculation (`USER_CALIB`) |
| [wakeup_asm.S](peripheral_server_sleep/code/wakeup_asm.S) | Assembly wakeup stub in retention RAM |

### Headers (`include/`)

| File | Key constants defined |
|------|----------------------|
| [app.h](peripheral_server_sleep/include/app.h) | Clock dividers (varies by `RFCLK_FREQ`), `RTC_CLK_SRC`, calibration mode, sleep/standby timings, service function lists, GPIO assignments (`LED_DIO=6`, `RECOVERY_DIO=12`) |

### RTE (Run-Time Environment)

- [RTE_Device.h](peripheral_server_sleep/RTE/Device/RSL10/RTE_Device.h) — Retention regulator trim values (0x1 for non-automotive, 0x3 for automotive or RC oscillator)
- [rsl10_protocol.c](peripheral_server_sleep/RTE/Device/RSL10/rsl10_protocol.c) — Flash-based protocol handling
- [startup_rsl10.S](peripheral_server_sleep/RTE/Device/RSL10/startup_rsl10.S) — Startup code, vector table
- [system_rsl10.c](peripheral_server_sleep/RTE/Device/RSL10/system_rsl10.c) — System initialization, clock setup

### Key globals

- `struct app_env_tag app_env` — battery level, sleep cycle counter, notification flags
- `struct sleep_mode_env_tag sleep_mode_env` — sleep mode configuration (wakeup pins, memory power, retention)
- `struct low_power_clk_param_tag low_power_clk_param` — low power clock measurement state
- `struct ble_env_tag ble_env` — current BLE state
- `struct bass_support_env_tag bass_support_env` — Battery Service state
- `struct cs_env_tag cs_env` — Custom Service state (notification data, CCCD)

## Recovery Mode

DIO12 pulled to ground during reset pauses the 3-second startup delay, allowing re-flashing of a bricked device that enters sleep/reset too quickly. See [readme](peripheral_server_sleep/readme_peripheral_server_sleep.md) lines 200-210.
