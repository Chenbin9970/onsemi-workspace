Peripheral Device with Sleep Mode Sample Code
=============================================

NOTE: If you use this sample application for your own purposes, follow the
licensing agreement specified in Software Use Agreement - use and 
accept (ONIPLAW 08142020).pdf in the root directory of the installed CMSIS-Pack.

Overview
--------
This sample project generates a battery service and a custom service. It
then starts an undirected connectable advertising with the device's public
address, if an address is available at `DEVICE_INFO_BLUETOOTH_ADDR` in
non-volatile memory three (`NVR3`). If this address is not defined
(all 1s or 0s), use a pre-defined, private Bluetooth(R) address
(`PRIVATE_BDADDR`) located in `ble_std.h`.

When interacting with a device implementing this sample project, any central
device can scan, connect, and perform service discovery, receive battery value
notifications, or read the battery value. The central device has the ability 
to read and write custom attributes. The RSL10 ADC is used to read the battery
level value. The average for 16 reads is calculated, and if this average value 
changes, a flag is set to send a battery level notification.

The Sleep Mode of the device is supported by the Bluetooth Low Energy
library and the system library. In each loop of the main
application, after routine operations (including battery level readings
and service notifications) are performed, if the system can switch to Sleep
Mode, Bluetooth Low Energy configurations and states are saved
and the system is put into Sleep Mode. The system is then wakened up by the
Bluetooth Low Energy baseband timer. On waking up, configurations
and states are restored; therefore, the Bluetooth Low Energy connection with 
the central device (established before going to Sleep Mode) and normal 
operations of the application are resumed.

This sample project passes through several states before all services are
enabled:

1.  `APPM_INIT` (initialization)
    Application initializes and is configured into an idle state. 
2.  `APPM_CREATE_DB` (create database)
    Application has configured the Bluetooth stack, including GAP, according 
    to the required role and features. It is now adding services, handlers, 
    and characteristics for all services that it can provide.
3.  `APPM_READY` (ready)
    Application has added the desired standard and custom services or profiles 
    into the Bluetooth GATT database and handlers.
4.  `APPM_ADVERTISING` (advertising)
    The device starts advertising based on the sample project.
5.  `APPM_CONNECTED` (connected)
    Connection is now established with another compatible device.

**This sample project is structured as follows:**

The source code exists in a `code` folder, and application-related include
header files are in the `include` folder.

Code
----
    app.c         - main()
    app_init.c    - All initialization functions are called here, but the
                    implementation is in the respective `.c` files
    app_process.c - Message handlers for application
    ble_bass.c    - Support functions and message handlers pertaining to the 
                    Battery Service Server
    app_custom.c  - Support functions and message handlers pertaining to the 
                    Custom Service Server
    ble_std.c     - Support functions and message handlers pertaining to
                    Bluetooth Low Energy technology
    calibration.c - Fetches or calculates trim values and loads them into the
                    appropriate trim registers
    wakeup_asm.S  - Initializes the stack pointer for wakeup function

Headers
-------
    app.h         - Overall application header file
    ble_bass.h    - Header file for the Battery Service Server
    ble_custom.h  - Header file for the Custom Service Server
    ble_std.h     - Header file for standard Bluetooth Low Energy support
    calibration.h - Header file for the Calibration file

Linker Script
-------------
    sections.ld       - Linker script with custom DRAM size and directives to
                        place sleep and wakeup routines in DRAM. This file is
                        used for the full Bluetooth Low Energy stack.

Configuration
-------------
The project comes preconfigured with the release variant of the Bluetooth 
Low Energy stack, which supports 2 Mbps Bluetooth Low Energy operation and is configured to an advertisement interval of 40 ms. This configuration keeps 
32 KB of memory in retention during Sleep Mode.

For lower power consumption, all build targets use VCC 1.10 V and VDDRF 1.05 V
for nearly 0 dBm output (typically -0.25 dBm). While these voltages are not
stored in the calibration records during manufacturing (NVR4), they are still
supported by the `Sys_Power_VCCConfig` and `Sys_Power_VDDRFConfig` system 
library functions.

Hardware Requirements
---------------------
This application can be executed on any RSL10 Evaluation and Development Board
with no external connections required.

Importing Project
-----------------
To import the sample code into your IDE workspace, refer to the
*Getting Started Guide* for your IDE for more information.

Verification
------------
To verify that this application is functioning correctly, use RSL10 or another
third-party central device application to establish a connection. In addition 
to establishing a connection, this application can be used to read/write 
characteristics and receive notifications. Note that, for power saving 
purposes, the default name of the peripheral device is set to a zero-length 
string.

To show how an application can send notifications, the application sends a
custom service notification every 10 sleep-wakeup cycles. Changing the 
Bluetooth Low Energy connection interval or slave latency changes data 
notification intervals accordingly. The LED blinks to show sleep-wakeup 
cycles.

To show that the RF transmission power is set to the desired level (which 
could be 0, 3, or 6 dBm), use a spectrum analyzer to probe the antenna with an
SMA connector. For instance, for a 0 dBm configuration, the spectrum analyzer
yields a maximum RF power of around 0 dBm.     

To show the current consumption of the application, configure the board for an
unregulated external supply, as referred to in the 
*RSL10 Evaluation and Development Board User`s Manual*. Connect the board to a 
DC power analyzer and acquire the measurement parameters listed below:

Is - Average Sleep Mode current
Ts - Sleep Mode time duration
Ia - Average active current
Ta - Active time duration
I  - Overall average current

The overall average current is calculated as follows:
    
    I = ( (Ts * Is) + (Ia * Ta) ) / (Ts + Ta)

Compare the calculated overall average current against the value in the
datasheet.

Notes
-----
This sample application considers three possible cases regarding the 
calibration of supply voltages:

  - **Case 1:** VDDC, VDDM and VCC trim values are read from NVR4 and loaded into
    corresponding trim registers to calibrate the board. `Sys_RFFE_SetTXPower()`
    sets VDDRF, VDDPA and `PA_PWR` (`RF_REG19`) (when applicable) to have the 
    desirable radio transmission power. Select this case by defining 
    `CALIB_RECORD` as `MANU_CALIB`.

  - **Case 2:** VDDC, VDDM and VCC trim values are calculated and stored in the
    NVR3 by the supplemental calibrate sample application. During the system
    boot process, the user-defined initialization function reads and loads
    those supplemental trim values from NVR3 into corresponding trim registers
    to calibrate the board. `Sys_RFFE_SetTXPower()` sets VDDRF, VDDPA and `PA_PWR`
    (`RF_REG19`) (when applicable) to have the desirable radio transmission power.
    Select this case by defining `CALIB_RECORD` as `SUPPLEMENTAL_CALIB`.

  - **Case 3:** this sample application needs to calculate trim values of VDDC,
    VDDM, VCC, VDDRF and VDDPA for desired voltages, and load them into
    corresponding trim registers to calibrate the board. In this case, define
    `CALIB_RECORD` as `USER_CALIB`.
      
To use DIOs 0, 1, 2, and 3 as wakeup sources, in the `Sleep_Mode_Configure()`
function, set `sleep_mode_init_env->wakeup_cfg` with `WAKEUP_DIO*_ENABLE` and
`WAKEUP_DIO*_[RISING | FALLING]`. In addition, you must configure
the corresponding DIO pins as inputs.

Trim values for VDDT, VDDM, and VDDC retention regulators can be configured by
using the "Retention Regulator Trim Configuration" in the `RTE_Device.h` file.

  - For system stability, trim values for VDDT, VDDM, and VDDC retention
    regulators need to be set to 0x1 for non-automotive RSL10 products. This 
    is the default configuration in the `RTE_Device.h` file. However, for
    automotive RSL10 products, these trim values need to be set to 0x3. Besides 
    this, if the low power clock source is the RC 32 KHz oscillator (i.e., 
    `RTC_CLK_SRC` is defined to `RTC_CLK_SRC_RC_OSC` in the `app.h` file),
    the trim value for the VDDT retention regulator must be always set to 0x3.
    
When Sleep Mode (or Standby Mode) is used in the application, the 
`Sys_PowerModes_Sleep_Init()` or `Sys_PowerModes_Sleep_Init_2Mbps()` 
(or `Sys_PowerModes_Standby_Init()`) function is called to back up the 
values of the Bluetooth Low Energy hardware registers and RF front-end 
registers. These values are restored at each wakeup initialization to 
maintain consistent RF output power level over sleep (or standby) 
and wakeup cycles. In the application, when needed, you can change 
the RF output power level by calling `Sys_RFEE_SetTxPower()`; however, 
note that this function changes the values of some RF registers. 
Therefore, after each time you call `Sys_RFEE_SetTxPower()` function, 
call the corresonding sleep (or standby) initialization function 
to ensure that the most updated RF register values are backed up.

Sometimes the firmware in RSL10 cannot be successfully re-flashed, due to the
application going into Sleep Mode or resetting continuously (either by design 
or due to programming error). To circumvent this scenario, a software recovery
mode using DIO12 can be implemented with the following steps:

1.  Connect DIO12 to ground.
2.  Press the RESET button (this restarts the application, which
    pauses at the start of its initialization routine).
3.  Re-flash RSL10. After successful re-flashing, disconnect DIO12 from
    ground, and press the RESET button so that the application can work
    properly.


***
Copyright (c) 2019 Semiconductor Components Industries, LLC
(d/b/a ON Semiconductor).
