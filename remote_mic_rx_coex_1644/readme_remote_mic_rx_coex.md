Mono Audio Stream Broadcast Receiver Custom Protocol Coexistence Sample Application
===================================================================================

NOTE: If you use this sample application for your own purposes, follow the
licensing agreement specified in `Software Use Agreement - use and` 
`accept (ONIPLAW 08142020).pdf` in the root directory of the installed CMSIS-Pack.

Overview
--------
This sample code demonstrates the functionality of a receiver that receives
audio data wirelessly from a corresponding transmitter using the Audio Stream
Broadcast Custom Protocol (remote microphone custom protocol), while
simultaneously maintaining an active Bluetooth Low Energy connection,
demonstrating coexistence. The application showcases the RSL10 firmware 
implementation required for remote microphone or wireless auxiliary audio 
input use cases where Bluetooth Low Energy coexistence is required.

The Bluetooth Low Energy section of this application is based on the 
`peripheral_server` sample application, in which a new custom service is 
defined to allow the reception of remote microphone custom protocol parameters
from a central device. This custom service is also used to configure the 
process by which the program will start or stop receiving audio. 

The audio streaming section of this application is based on the
`remote_mic_rx_raw` and `remote_mic_trx_coded` applications. This section
illustrates how the receiver decodes or passes through the encoded audio data
received from a corresponding transmitter over the air, and how to configure
the required peripheral and audio processing hardware blocks for outputting
audio data faultlessly. The audio output on the RSL10 is sent to the Ezairo
7100 Evaluation an Development Board, which outputs the audio through its
output driver.

**This sample project is structured as follows:**

The source code exists in a `code` folder, all application-related include
header files are in the `include` folder, and the `main()` function `app.c` is
located in the parent directory.

Code
----
    app_func.c      - Support functions and message handlers pertaining to
                      peripheral and audio processing hardware blocks
    app_init.c      - All initialization functions are called here, but the
                      implementations are in their respective C files
    dsp_pm_dm.c     - LPDSP32 DSP decoder program
    rm_app.c        - Support functions and message handlers pertaining to the
                      remote microphone custom protocol

Include
-------
    app.h           - Overall application header file
    dsp_pm_dm.h     - Header file for LPDSP32 DSP decoder program

The appropriate libraries and `include` files are loaded according to the 
build configuration selected.
    
Hardware Requirements
---------------------
This application can be executed on any RSL10 Evaluation and Development 
Board. 

The receiver can be configured to send either the raw (decoded) or the coded
(encoded) audio output to an Ezairo 7100 Evaluation and Development Board
through the SPI bus.

For the receiver configuration where raw audio output is sent from the RSL10
to the Ezairo 7100 over the SPI bus, ensure that the following pins are
connected between the two devices:

    ----------------------------------------------
    Pin Signal               | RSL10 | Ezairo 7100
    ----------------------------------------------
    SPI Chip Select          | DIO0  | DIO25
    SPI MOSI                 | DIO1  | DIO26
    SPI MISO                 | DIO2  | DIO23
    SPI Clock                | DIO3  | DIO24
    Sampling Frequency Clock | DIO7  | DIO21
    VDDO                     | VDDO  | VDDO3
    Ground                   | GND   | GND
    ----------------------------------------------
In this configuration, the RSL10 is the SPI Master and the Ezairo 7100 is the
SPI Slave.

For the receiver configuration where coded audio output is sent from the RSL10
to the Ezairo 7100 over the SPI bus, ensure that the following pins are
connected between the two devices:

    ----------------------------------------------
    Pin Signal               | RSL10 | Ezairo 7100
    ----------------------------------------------
    SPI Chip Select          | DIO0  | DIO25
    SPI MOSI                 | DIO1  | DIO26
    SPI MISO                 | DIO2  | DIO15
    SPI Clock                | DIO3  | DIO24
    Sampling Frequency Clock | DIO7  | DIO21
    PLL Sync                 | DIO8  | DIO19
    VDDO                     | VDDO  | VDDO3
    Ground                   | GND   | GND
    ----------------------------------------------
In this configuration, the RSL10 is the SPI Master and the Ezairo 7100 is the
SPI Slave. As well, ensure that both VDDO2-SEL and VDDO3-SEL have jumpers on 
pins 1-2 on the Ezairo 7100.

Also, note the following when setting up the various hardware configurations:
- Ensure that no jumpers are placed on P9 on the RSL10 QFN Evaluation and
  Development Board for the configurations where VDDO is being supplied
  externally.
- On the Ezairo 7100 EDK, ensure that jumpers are placed on VBATOD-EN and on 
  pins 1-2 on the `VDDO*_SEL` of the `VDDO*` being provided to the RSL10 in 
  all configurations.
- DIO7 and DIO8 are mapped to the SCL and SDA pins respectively on the RSL10
  QFN Evaluation and Development Board.

Importing a Project
-------------------
To import the sample code into your IDE workspace, refer to the 
*Getting Started Guide* for your IDE for more information.

Select the project configuration according to the required optimization level.
Use **Debug** configuration for optimization level **None**, or **Release**
configuration for optimization level **More** or **O2**.

Verification
============
To verify that this application is functioning correctly, the audio stream
received from the transmitter can be monitored on the receiver while
simultaneously maintaining an active Bluetooth Low Energy connection. In 
addition to ensuring that the hardware setup is as outlined in the 
*Hardware Requirements* section for the various receiver configurations, this sample code
needs to be compiled and flashed onto the RSL10 Evaluation and Development 
Board with the appropriate pre-processor definition settings, to ensure that 
the receiver can function as expected. The corresponding Ezairo 7100 firmware 
also needs to be flashed onto the Ezairo 7100 Evaluation and Development 
Board, for those configurations where it is used. The Ezairo 7100 firmware 
required to process the transmitted data from the RSL10 is provided in the 
`RSL10_Utility_Apps.zip` file.

For the receiver configuration where raw audio output is sent from the RSL10
to the Ezairo 7100 over the SPI bus, compile and flash this sample application
with the `OUTPUT_INTRF` define as follows:

    #define OUTPUT_INTRF                    SPI_TX_RAW_OUTPUT

In addition, program the Ezairo 7100 with the `audio_spi_slave` application 
provided in the utility applications folder. The audio output can be 
monitored through the first RCA output channel (RCVR0 - with FILTEN0 pins 1-2
and 3-4 shorted) on the Ezairo 7100 Evaluation and Development Board.

For the receiver configuration where coded audio output is sent from the RSL10
to the Ezairo 7100 over the SPI bus, compile and flash this sample application
with the `OUTPUT_INTRF` define as follows:

    #define OUTPUT_INTRF                    SPI_TX_CODED_OUTPUT

In addition, program the Ezairo 7100 with the `RSL10_RM_HearingAid` 
application provided in the utility applications folder. The audio output can 
be monitored through the first RCA output channel (RCVR0 - with FILTEN0 pins 
1-2 and 3-4 shorted) on the Ezairo 7100 Evaluation and Development Board.

Various remote microphone custom protocol settings such as the frequency
channel hopping list (`RM_HOPLIST`), modulation index 
(`app_env.rm_param.mod_idx`), and the remote microphone custom protocol 
streaming address (`app_env.rm_param.accessword`) in the receiver and 
transmitter RSL10 programs need to be matched, in order to successfully 
transmit/receive audio via the remote microphone custom protocol. If the 
Bluetooth address of the receiver and the peer Bluetooth address
(`DIRECT_PEER_BD_ADDRESS`) of the transmitter are matched, on startup, the 
transmitter and receiver exchange/synchronize all the remote microphone custom
protocol parameters as needed to successfully start the audio stream. 
Note that if the RSL10 receiver device’s public address is available in 
`DEVICE_INFO_BLUETOOTH_ADDR` (located in `NVR3`), then it is used as the 
device address. If no public address is defined in the above mentioned address
in `NVR3`, then a pre-defined private address (`PRIVATE_BDADDR`) is used as 
the Bluetooth address for the receiver. Also note that the peer Bluetooth 
address type of the transmitter (`DIRECT_PEER_BD_ADDRESS_TYPE`) needs to match
the device address type of the receiver (public or private). To test 
coexistence functionality, connect (and read, write, discover services, etc.) 
to the receiver with a central device while simultaneously monitoring the 
audio stream input and output.

More information on how to ensure that the receiver Bluetooth address and the
transmitter peer address are matched:

Power up the receiver with any configuration from this application; you can 
see it start advertising. Use a Bluetooth Low Energy device to scan for 
advertising devices. Record the address of the receiver (`RemoteMic_Rx`). If 
the address is not `PRIVATE_BDADDR` (0xC01111111111 - default private 
address), then the `DIRECT_PEER_BD_ADDRESS` and `DIRECT_PEER_BD_ADDRESS_TYPE` 
defines (in the transmitter program) need to be modified.

For example, if the Bluetooth address is 0xA1B2C3D4E5F6 (public address), set
the defines mentioned above as follows:

    #define DIRECT_PEER_BD_ADDRESS          {0xF6,0xE5,0xD4,0xC3,0xB2,0xA1}
    #define DIRECT_PEER_BD_ADDRESS_TYPE     BD_TYPE_PUBLIC

If the receiver address is the address defined in `PRIVATE_BDADDR` (in the
receiver program), no changes are required to the defines in the transmitter
program mentioned above to establish the Bluetooth Low Energy connection on
startup. Also, note that the receiver has to be powered up and advertising
before the transmitter is powered up, to establish the Bluetooth Low Energy
connection on startup.

Notes
=====
The RSL10 and/or Ezairo 7100 may be damaged if the corresponding
transmitter/receiver side programs do not match because of potential pin
direction conflicts.

Sometimes the firmware in RSL10 cannot be successfully re-flashed, due to the
application going into Sleep Mode or resetting continuously (either by design
or due to programming error). To circumvent this scenario, a software recovery
mode using DIO13 can be implemented with the following steps:

1.  Connect DIO13 to ground.
2.  Press the RESET button (this restarts the application, which
    pauses at the start of its initialization routine).
3.  Re-flash RSL10. After successful re-flashing, disconnect DIO13 from
    ground, and press the RESET button so that the application can work
    properly.

***
Copyright (c) 2019 Semiconductor Components Industries, LLC
(d/b/a ON Semiconductor).
