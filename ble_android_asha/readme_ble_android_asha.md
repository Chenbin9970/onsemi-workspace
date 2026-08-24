Android Audio Streaming Hearing Aid Peripheral Sample Code
==========================================================

NOTE: If you use this sample application for your own purposes, follow the
licensing agreement specified in `Software Use Agreement - use and` 
`accept (ONIPLAW 08142020).pdf` in the root directory of the installed CMSIS-Pack.

Overview
========
This sample project demonstrates the use of the RSL10 Bluetooth Low Energy 
library to develop an audio streaming application for the Audio Streaming for 
Hearing Aids profile ([ASHA][ASHA_link]). The application establishes two audio links 
and streams stereo audio data from the Android device to two hearing aids (left
 and right hearing aids).

[ASHA_link]: https://source.android.com/devices/bluetooth/asha
 
Depending on the device address type selected by the application (in app.h), 
either a public or private address is used for the hearing aid.

An Android device can scan, connect, pair/bond to the hearing aid and perform 
service discovery. It displays the name of the discovered hearing aid (which is
set by default to `ble_android_asha`). This sample project also generates the 
Device Information Service and the Battery Service.

The side of the ASHA profile is selected by the `ASHA_CAPABILITIES_SIDE` 
preprocessor symbol. Four preset build configurations support Debug and Release
builds for the left and right channels.

The user then can start streaming audio from the Android device to the two 
connected hearing aids using a streaming applications (e.g., YouTube(TM) or 
Netflix(TM)). The user can pause, restart or adjust the volume of the audio 
stream.

This sample project is equipped with a Bluetooth low energy abstraction layer
that provides a higher level application programming interface (API) to 
abstract the Bluetooth GAP, GATT and L2C layers. The abstraction layer has been
designed to improve flexibility and simplicity by providing the following 
features:
  - An event subscription mechanism that allows the application to subscribe 
  and receive callback notifications for any Kernel or Bluetooth event. This 
  improves encapsulation/integration of different modules, as they can be 
  implemented while isolated in their own files.
  - Generic definition of custom services with callback notification support
  - Security handling and bond list implementation in RSL10 flash
  - Code structure and API naming aligned with RivieraWaves documentation, so
    you can map the code into the documentation more easily

The sample code was refactored to keep the generic abstraction layer code
and application-specific code in separate files. This increases flexibility,
maintainability and reusability of components between distinct applications.

After the connection is established, for any profiles/services that need to
be enabled, an enable request message is sent to the corresponding profile of
the Bluetooth stack. Once a response is received for each of those profiles,
the application starts advertising.

Battery Service: this service database is configured for a single battery
                 instance. A second battery can be added by modifying the
                 `APP_BAS_NB` definition in `app_bass.h`. The application 
                 provides a callback function to read the battery level using 
                 the average of 16 reads of the RSL10 ADC.

The application notifies clients about the battery level when it changes: the 
application monitors the level periodically and sends a notification when a 
change is detected. The monitoring timeout is configured using the
`BASS_NotifyOnBattLevelChange()` function.

The message subscription mechanism allows the application and services to
subscribe and receive callback notifications based on the Kernel message ID
or task ID. This allows each module of the application to be independently
implemented in its own files. The subscription is performed using the
`MsgHandler_Add()` function. The application subscribes to receive GAPM and
GAPC events (see `app.c`). The services subscribe to receive Kernel events in
their initialization function (see `BASS_Initialize()` in `ble_bass.c`, for an
example). The application event handlers are implemented in `app_msg_handler.c`.

The basic sequence of operations and event messages exchanged between the
application and the Bluetooth stack is presented below:

    APP <--> Bluetooth low energy Stack
    Startup
        --->  GAPM_ResetCmd() - app.c
        <---  GAPM_CMP_EVT / GAPM_RESET
        --->  GAPM_SetDevConfigCmd() - app_msg_handler.c
        <---  GAPM_CMP_EVT / GAPM_SET_DEV_CONFIG
        --->  GATTM_AddAttributeDatabase() - app_msg_handler.c
        --->  GAPM_ProfileTaskAddCmd() - ble_bass.c
        <---  GATTM_ADD_SVC_RSP
        <---  GAPM_PROFILE_ADDED_IND
        --->  GAPM_StartAdvertiseCmd() - app_msg_handler.c
  
    Connection request / parameters update request / device info request
        <--- GAPC_CONNECTION_REQ_IND
        ---> GAPM_ResolvAddrCmd() - app_msg_handler.c
        ---> GAPC_ConnectionCfm() - app_msg_handler.c
        <--- GAPC_PARAM_UPDATE_REQ_IND
        ---> GAPC_ParamUpdateCfm() - app_msg_handler.c
        <--- GAPC_GET_DEV_INFO_REQ_IND
        ---> GAPC_GetDevInfoCfm() - app_msg_handler.c
  
    Pairing / Bonding request
        <--- GAPC_BOND_REQ_IND / GAPC_PAIRING_REQ
        GAPC_BondCfm() - app_msg_handler.c
        <--- GAPC_BOND_REQ_IND / GAPC_LTK_EXCH
        GAPC_BondCfm() - app_msg_handler.c
        <--- GAPC_BOND_REQ_IND / GAPC_IRK_EXCH
        GAPC_BondCfm() - app_msg_handler.c
        <--- GAPC_BOND_REQ_IND / GAPC_CSRK_EXCH
        GAPC_BondCfm() - app_msg_handler.c
  
    Encrypt request
        <--- GAPC_ENCRYPT_REQ_IND
        ---> GAPC_EncryptCfm() - app_msg_handler.c
  
    Disconnection
        <--- GAPC_DISCONNECT_IND
        ---> GAPM_StartAdvertiseCmd() - app_msg_handler.c
  
    Audio volume change
        <--- *appCallback(ASHA_VOLUME_CHANGE)
  
    Audio streaming start
        <--- *appCallback(ASHA_AUDIO_START)
        <--- *appCallback(ASHA_AUDIO_RCVD)
  
    Audio streaming stop
        <--- *appCallback(ASHA_AUDIO_STOP)

Android ASHA Service
==========================
The ASHA service is implemented through a custom service (UUID `0xFDF0`) with 
the following characteristics:

```
| Characteristic      |           UUID                       |
|---------------------|:------------------------------------:|
| ReadOnlyProperties  | 6333651e-c481-4a3e-9169-7c902aad37bb |
| AudioControlPoint   | f0d4de7e-4a88-476c-9d9f-1937b0996cc0 |
| AudioStatusPoint    | 38663f1a-e711-4cac-b641-326b56404837 |
| Volume              | 00e4ca9e-ab14-41e4-8823-f9e70c7e91df |
| LE_PSM              | 2d410339-82b6-42aa-b34e-e2e01df8cc1a |
```

Register a callback function through the `ASHA_Initialize()` function. This 
callback function is called for different events that require processing by 
the application code.  The callback's function first argument specifies the 
event, and the second argument holds the parameter for the event.

  - `ASHA_Operation_volumeChange`. This event is activated when the phone 
  requires a volume change, either increasing or decreasing. The parameter 
  contains the new volume setting.
  - `ASHA_Operation_audioStart`. This event is activated when the phone writes
  to the `AudioControlPoint` characteristic indicating that the audio stream 
  transmission is starting. The ASHA specification requires the phone to write
  to the `AudioControlPoint` characteristic whenever it starts or stops 
  streaming. The Bluetooth Low Energy abstraction creates different events for
  the audio start and stop.
  - `ASHA_Operation_audioStop`. This event is activated when the phone writes 
  to the `AudioControlPoint` characteristic indicating that the audio stream 
  transmission was stopped. The ASHA specification requires the phone to write
  to the `AudioControlPoint` characteristic whenever it starts or stops 
  streaming. The Bluetooth Low Energy abstraction creates different events for
  the audio start and stop.
  - `ASHA_Operation_audioRcvd`. This event is activated when an audio streaming
  packet is received. The application code has to then handle queuing and 
  rendering of the audio data.

The ASHA service uses an L2CAP credit-based connection to perform flow control.
Typically an application adds credits to the connection when it has finished 
rendering an audio sample and thus has freed a buffer or queue position. The 
Bluetooth Low Energy abstraction offers the `ASHA_AddCredits()` function to 
help handle this.

Source Code Organization
==========================  

This sample project is structured as follows:
The source code exists in a "source" folder. Application-related header files
are in the "include" folder. The `main()` function is implemented in the `app.c`
file, which is located in the parent directory.

The Bluetooth Low Energy abstraction layer contains support functions for the 
GAP and GATT layers and has a message handling mechanism that allows the 
application to subscribe to Kernel events. It also contains standard profile-
related files.

The device address type and source are set in app.h. If `APP_DEVICE_PARAM_SRC`
is set to `FLASH_PROVIDED_or_DFLT` and the address type is set to
`GAPM_CFG_ADDR_PUBLIC`, the stack loads the public device address stored in NVR3
flash. Otherwise, the address provided in the application is used.

Application-specific files
--------------------------

    app.c                  - Main application file
    
    source
    -------
        app_config.c      - Device configuration and definition of application-
                            specific Bluetooth low energy messages.
        app_msg_handler.c - Application-specific message handlers
        app_bass.c        - Application-specific battery service functions and
                            message handlers (ADC read, BATMON alarm, etc.)
        app_diss.c        - Application-specific DISS functions and message handlers
        app_trace.c       - Debugging (printf) utility functions
        app_init_audio.c  - Functions that initialize various blocks in the
                            reference audio path
        app_func_audio.c  - Interrupt service routines for audio data
                            buffering, direct memory access, and other operations
                            in the reference audio path
        dsp_pm_dm.c       - Contains the arrays generated for testing and debug
    
    source\codecs
    -------------
        G.722 codec and PLC related code.
    
    include
    -------
        app.h             - Main application header file
        app_bass.h        - Application-specific Battery Service Server header
        app_audio.h       - Provides definitions and APIs used by LPDSP32 (G.722 Decoder)
                            and other blocks in the reference audio paths; interrupt
                            service routines used for audio data buffering, direct memory
                            access, and other operations in the reference audio path
        dsp_pm_dm.h       - Header file for the testing arrays

Bluetooth Low Energy abstraction files (CMSIS-Pack component is generic for any application)
--------------------------------------------------------------------


        ble_gap.c         - GAP layer support functions and message handlers
        ble_gatt.c        - GATT layer support functions and message handlers
        ble_l2c.c         - L2CAP support functions and message handlers
        msg_handler.c     - Message handling subscribing mechanism implementation
    

        ble_gap.h         - GAP layer abstraction header
        ble_gatt.h        - GATT layer abstraction header
        ble_l2c.h         - L2CAP layer abstraction header
        msg_handler.h     - Message handling subscribing mechanism header
    

        ble_bass.c        - Battery Service Server support functions and handlers
        ble_diss.c        - Device Information Service Server support functions and
                            handlers
        ble_asha.c        - ASHA profile support functions and handlers
    

        ble_bass.h        - Battery Service Server header
        ble_diss.h        - Device Information Service Server header
        ble_asha.h        - ASHA profile header

Hardware Requirements
=====================
This application can be configured to run on two different hardware setups. The
first setup is a RSL10 Evaluation and Development Board and an Ezairo 7100
Evaluation and Development Board as the audio renderer. The second supported 
setup is the Ezairo 7160SL Hybrid Demonstrator Board.

RSL10 and Ezairo 7100
---------------------
To support this configuration, the project must be compiled with the 
`EZAIRO_71XX_DIO_CFG` preprocessor symbol set to `7100`. This configuration uses 
the following pin connections between the RSL10 and Ezairo 7100 boards:

```
| Pin Signal                  |        RSL10        |         Ezairo 7160       |
|-----------------------------|:-------------------:|:-------------------------:|
| SPI Chip Select             | (CS_DO)       DIO0  | (DIO_C_SPI_CS)      DIO25 |
| SPI MOSI                    | (SER_DO)      DIO1  | (DIO_C_SPI_SERI)    DIO26 |
| SPI MISO                    | (SER_DI)      DIO2  | (DIO_C_SPI_SERO)    DIO23 |
| SPI Clock                   | (CLK_DO)      DIO3  | (DIO_C_SPI_CLK)     DIO24 |
| Sampling Frequency Clock    | (SAMPL_CLK)   DIO13 | (DIO_MCLK)          DIO21 |
| RF Interrupt                | (RF_INT)      DIO14 | (DIO_RX_IRQ)        DIO15 |
| VDDO                        |         VDDO        |             VDDO3         |
| Ground                      |         GND         |             GND           |
```

In this configuration, the RSL10 is the SPI Master and the Ezairo 7100 is the
SPI Slave.

In addition to the above pin connection diagram, note the following when setting
up the hardware configurations:
  - Connection cables between the two boards should be as short as possible.
  - On RSL10 Evaluation and Development Boards: ensure that no jumpers are
    placed on P9 (since VDDO is being externally supplied).
  - On Ezairo 7100 Evaluation and Development Boards: ensure that a jumper
    is placed on VBATOD-EN, and jumpers are placed on pins 1 and 2 of
    `VDDO2_SEL` and `VDDO3_SEL`. The `FILTEN0` jumpers must be closed 
    (1-2 and 3-4).

Please also change the `define` below to the presented values in `app.cfi` of 
the `audio_spi_slave` Ezairo application (see the Verification below):

`EZAIRO_71XX_DIO_CFG` preprocessor symbol set to `7100`

Ezairo 7160
---------------------
To support this configuration, the project must be compiled with 
`EZAIRO_71XX_DIO_CFG` preprocessor symbol set to 7160. The pins used in this 
configuration are as follows:

```
| Pin Signal                  |        RSL10        |         Ezairo 7100       |
|-----------------------------|:-------------------:|:-------------------------:|
| SPI Chip Select             | (CS_DO)       DIO0  | (DIO_C_SPI_CS)      DIO11 |
| SPI MOSI                    | (SER_DO)      DIO1  | (DIO_C_SPI_SERI)    DIO12 |
| SPI MISO                    | (SER_DI)      DIO14 | (DIO_C_SPI_SERO)    DIO18 |
| SPI Clock                   | (CLK_DO)      DIO3  | (DIO_C_SPI_CLK)     DIO14 |
| Sampling Frequency Clock    | (SAMPL_CLK)   DIO2  | (DIO_MCLK)          DIO13 |
| RF Interrupt                | (RF_INT)      DIO13 | (DIO_RX_IRQ)        DIO16 |
| VDDO                        |         VDDO        |             VDDO3         |
| Ground                      |         GND         |             GND           |
```

Please also change the `define` below to the presented values in `app.cfi` of 
the `audio_spi_slave` Ezairo application (see the Verification below):

`EZAIRO_71XX_DIO_CFG` preprocessor symbol set to `7160`

Importing a Project
===================
To import the sample code into your IDE workspace, refer to the
Getting Started Guide for your IDE for more information.

Select the project configuration according to the required optimization level.
Use "Debug" configuration for optimization level "None" or "Release"
configuration for optimization level "More" or "O2". The Debug and Release
configurations will set `ASHA_CAPABILITIES_SIDE` preprocessor symbol to 
`ASHA_CAPABILITIES_SIDE_LEFT`. This compiles the sample application as a left
audio channel hearing aid. To support the right audio channel, please use the 
`Debug_Right` and `Release_Right` build configurations.

Verification
============
To verify if this application is functioning correctly, use an RSL10 Evaluation
and Development Board combined with an audio renderer as a hearing aid (a pair
of hearing aids is required) and an Android phone device. The hearing aid feature
should be enabled on the phone. At the time of writing this instruction, no
Android phone has this feature by default. The user needs to download the
Android source code (aosp master branch), enable the feature, recompile it and
load the custom generated ROM on the Android phone. [This link][config_xml] has
the required changes in the Android source code. The feature has been 
successfully tested with Android 9.0 r16 (build PQ1A.181105.017.A1) on a Pixel 
2 (walleye) as well as a Pixel 2 XL (taimen) phones.

[config_xml]: https://android-review.googlesource.com/c/platform/packages/apps/Bluetooth/+/736737/1/res/values/config.xml

This sample code needs to be compiled and flashed onto the RSL10 Evaluation and
Development Boards. If Ezairo 7100 or 7160 Evaluation and Development Boards 
are used as the audio renderer, each board must be programmed with the 
`audio_spi_slave` application (required to process the transmitted data from 
RSL10) that is provided in the utility applications folder (obtained by 
extracting the `RSL10_Utility_Apps.zip` file). Make sure to set the MFI_APP 
definition to 1 in `app.cfi`.

Initially the hearing aids need to be manually paitred with the Android device. 
After this first pairing, the Android device detects the hearing aids and 
automatically connects to them. The audio signal in the Android device can be 
streamed to the hearing aids and perceived by the user.

Each time a valid audio packet is received from the Android device, DIO11 pins on
the RSL10 Evaluation and Development Boards are toggled.

When using Ezairo 7100 Evaluation and Development Boards as described above, 
the audio output can be monitored through the first RCA output channel (RCVR0 -
with FILTEN0 pins 1-2 and 3-4 connected). The left and right audio packet delay
can be measured by comparing SPI Chip Select pins on the two Ezairo 7100 
Evaluation and Development Boards.

Notes
=====
Sometimes the firmware in RSL10 cannot be successfully re-flashed, due to the
application going into Sleep Mode or resetting continuously (either by design
or due to a programming error). To circumvent this scenario, a software 
recovery mode using DIO12 can be implemented with the following steps:

1.  Connect DIO12 to ground.
2.  Press the RESET button (this restarts the application, which will
    pause at the start of its initialization routine).
3.  Re-flash RSL10. After successful re-flashing, disconnect DIO12 from
    ground, and press the RESET button so that the application will work
    properly.

\==============================================================================
Copyright (c) 2019 Semiconductor Components Industries, LLC
(d/b/a ON Semiconductor).