Stereo Audio Stream Broadcast Transmitter Custom Protocol Coexistence Sample Application
========================================================================================

NOTE: If you use this sample application for your own purposes, follow the
licensing agreement specified in `Software Use Agreement - use and` 
`accept (ONIPLAW 08142020).pdf` in the root directory of the installed CMSIS-Pack.

Overview
--------
This sample code demonstrates switching from the Bluetooth Low Energy mode 
(during an active connection) to transmitting audio through the Audio Stream 
Broadcast Custom Protocol (remote microphone custom protocol) at runtime. The 
transmitter (central device) establishes a Bluetooth Low Energy link with the
receiver (peripheral device), exchanges various remote microphone custom 
protocol parameters required for the audio stream to begin, then drops the 
Bluetooth Low Energy link with the receiver and begins to stream audio through
the remote microphone custom protocol. It showcases the RSL10 firmware 
implementation required for remote microphone or wireless auxiliary audio 
input use cases where Bluetooth Low Energy coexistence is required. 

The Bluetooth Low Energy section of this application is based on the 
`peripheral_server` sample application, in which a new custom service is defined
to allow the reception of remote microphone custom protocol parameters from a
central device. This custom service is also used to configure the process by
which the program will start or stop receiving audio. 

The audio streaming section of this application is based on the
`remote_mic_tx_raw` and `remote_mic_trx_coded` applications. The audio streaming
section illustrates how the transmitter configures the required peripheral and
audio processing hardware blocks for audio input, and how the unencoded audio
input data is then encoded before it is broadcasted over the air. This sample
application also supports the scenario where the external audio input device is
responsible for the encoding process by passing through data directly to the
wireless transciever to be broadcasted. The audio input on the RSL10 receiver
can be configured to be sent from external devices over the SPI and PCM
interfaces.

**This sample project is structured as follows:**

The source code exists in a `code` folder, all application-related include
header files are in the `include` folder, and the `main()` function `app.c` is
located in the parent directory. All source code in the codec folder pertains
to the LPDSP32 DSP encoder program.

Code
----
    app_func.c      - Support functions and message handlers pertaining to 
                      peripheral and audio processing hardware blocks
    app_init.c      - All initialization functions are called here, but the
                      implementations are in their respective C files
    fifo.c          - Support functions required to handle data sent to the              
                      LPDSP32 DSP for encoding
    rm_app.c        - Support functions and message handlers pertaining to the
                      remote microphone custom protocol

Include
-------
    app.h           - Overall application header file
    fifo.h          - Header file for support functions required to handle 
                      data sent to the LPDSP32 DSP for encoding

The appropriate libraries and `include` files are loaded according to the 
build configuration selected.

Hardware Requirements
---------------------
This application can be executed on any RSL10 Evaluation and Development 
Board. 

The raw (unencoded) audio input to the RSL10 transmitter can be configured to
be sent from the SPI and PCM interfaces using the Ezairo 7100 Evaluation and
Development Board. Additionally, the Open Music Labs Codec Shield (Wolfson
WM8731 codec) can also be used to send raw audio input over the PCM interface
to the RSL10 QFN Evaluation and Development Board. The coded (encoded) audio
input to the RSL10 transmitter can be configured to be sent only from the from
the SPI bus using the Ezairo 7100 Evaluation and Development Board.

For the transmitter configuration where raw input audio is sent to the RSL10
from the Ezairo 7100 over the SPI bus, ensure that the following pins are
connected between the two devices:

    ----------------------------------------------
    Pin Signal               | RSL10 | Ezairo 7100
    ----------------------------------------------
    SPI Chip Select          | DIO0  | DIO25  
    SPI MISO                 | DIO1  | DIO26
    SPI MOSI                 | DIO2  | DIO23 
    SPI Clock                | DIO3  | DIO24 
    Sampling Frequency Clock | DIO7  | DIO21 
    VDDO                     | VDDO  | VDDO3
    Ground                   | GND   | GND   
    ----------------------------------------------

In this configuration, the Ezairo 7100 is the SPI Master and the RSL10 is the
SPI Slave.

This sample application is compatible with a variety of sources for raw PCM
input audio data. The PCM format and input pins used for the PCM interface are
flexible, and these configuration parameters can be modified within the sample
application to meet the needs of the PCM input source. For sample purposes,
this application includes build configurations supporting input from an
Ezairo 7100 Evaluation and Development Board or an Open Music Labs Codec
Shield (Wolfson WM8731 codec).

For the transmitter configuration where raw input audio is sent to the RSL10
from the Ezairo 7100 over the PCM interface, ensure that the following pins 
are connected between the two devices:

    ----------------------------------------------------
    Pin Signal (RSL10 Perspective) | RSL10 | Ezairo 7100
    ----------------------------------------------------
    PCM Frame Sync                 | DIO0  | DIO25
    PCM Data Out                   | DIO1  | DIO26
    PCM Data In                    | DIO2  | DIO23
    PCM Clock                      | DIO3  | DIO24
    VDDO                           | VDDO  | VDDO3
    Ground                         | GND   | GND
    ----------------------------------------------------
In this configuration, the Ezairo 7100 is the PCM Master and the RSL10 is the 
PCM Slave.

The Open Music Labs Codec Shield can be used as a PCM input source by stacking
it on the RSL10 QFN board, facilitating an easy plug-and-play connection.
However, additional modifications are required to set up this configuration.
For the transmitter configuration where the raw input audio to the RSL10 is
sent from the Open Music Labs Codec Shield over the PCM interface, ensure
that the following pins are connected between the two devices:
- Ensure that the I2C pins on the shield are connected to the appropriate
  DIOs on the RSL10:
  
    -------------------------------------------------
    Pin Signal | RSL10 | Open Music Labs Codec Shield
    -------------------------------------------------
    I2C SCL    | DIO2  | J2-1
    I2C SDA    | DIO3  | J2-2
    -------------------------------------------------

- Ensure that the oscillator on the shield is compatible with outputting
  a sampling rate of 32 KHz. This sample application configures the shield
  appropriately, assuming the use of a 12 MHz oscillator.
- Clear the quad buffer/line driver. Ensure that the appropriate pins are
  shorted on the shield's PCB after removal of the buffer:
  ADCDAT(2) -> MISO(3), LRC(8) -> SS(9), and BCLK(11) -> SCK(12).
- Ensure that a jumper is placed on the quad buffer/line driver enable (EN)
- Populate a 3 way jumper on P7 on the RSL10 QFN board to configure the 3.3V
  pin as an output supply voltage.
- Ensure that the potentiometers (MOD0 and MOD1) are not populated on the 
  shield.

In this configuration, the audio codec shield is the PCM Master and the RSL10
is the PCM Slave.

For the transmitter configuration where coded input audio is sent to the RSL10
from the Ezairo 7100 over the SPI bus, ensure that the following pins are
connected between the two devices:

    ----------------------------------------------
    Pin Signal               | RSL10 | Ezairo 7100
    ----------------------------------------------
    SPI Chip Select          | DIO0  | DIO25     
    SPI MISO                 | DIO1  | DIO26
    SPI MOSI                 | DIO2  | DIO23
    SPI Clock                | DIO3  | DIO24
    Sampling Frequency Clock | DIO7  | DIO21
    PLL Sync                 | DIO8  | DIO19
    VDDO                     | VDDO  | VDDO3
    Ground                   | GND   | GND
    ----------------------------------------------
In this configuration, the Ezairo 7100 is the SPI Master and the RSL10 is the
SPI Slave.  Also, ensure that both VDDO2-SEL and VDDO3-SEL have jumpers on 
pins 1-2 on the Ezairo 7100 EDK.

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
Use `Debug` configuration for optimization level `None`, or `Release`
configuration for optimization level `More` or `O2`.

Verification
============
To verify that this application is functioning correctly, the transmitter’s 
input audio stream can be monitored on the receiver. In addition to ensuring 
that the hardware setup is as outlined in the Hardware Requirements section 
for the various transmitter configurations, this sample code needs to be 
compiled and flashed onto the RSL10 Evaluation and Development Board with the
appropriate pre-processor definition settings, to ensure that the transmitter
can function as expected. The corresponding Ezairo 7100 firmware also needs to
be flashed onto the Ezairo 7100 Evaluation and Development Board, for those 
configurations where it is used. The Ezairo 7100 firmware required to send its
input audio to the RSL10 is provided in the `RSL10_Utility_Apps.zip` file.

For the transmitter configuration where the raw input audio to the RSL10 is
sent from the Ezairo 7100 Evaluation and Development Board over the SPI
bus, set the `INPUT_INTRF` define as follows:

    #define INPUT_INTRF                     SPI_RX_RAW_INPUT

In addition, program the Ezairo 7100 with the `bi_directional_master_stereo` 
application provided in the utility applications folder. AI0 and AI1 capture 
the left and right audio channel data respectively (16 kHz sampling rate) on 
the Ezairo 7100 Evaluation and Development Board.

For the transmitter configuration where the raw input audio to the RSL10 is
sent from the Ezairo 7100 over the PCM interface, set the `INPUT_INTRF` and
`PCM_RX_RAW_SOURCE` defines as follows:

    #define INPUT_INTRF                     PCM_RX_RAW_INPUT
    #define PCM_RX_RAW_SOURCE               EZAIRO_7100

In addition, program the Ezairo 7100 with the `bi_directional_pcm_master` 
application provided in the utility applications folder. Note that the
`bi_directional_pcm_master` program only captures the left channel audio data
through AI0 on the Ezairo 7100 Evaluation and Development Board.

For the transmitter configuration where the raw input audio to the RSL10 is
sent from the Open Music Labs Codec Shield over the PCM interface, set the
`INPUT_INTRF` and `PCM_RX_RAW_SOURCE` defines as follows:

    #define INPUT_INTRF                     PCM_RX_RAW_INPUT
    #define PCM_RX_RAW_SOURCE               AUDIO_CODEC_SHIELD

The Open Music Labs Codec Shield captures the left and right audio through
its 3.5 mm stereo input jack (`LINE_IN`). In this configuration, the Open Music
Labs Codec Shield is the PCM Master and the RSL10 is the PCM Slave.

For the transmitter configuration where the coded input audio to the RSL10 is
sent from the Ezairo 7100 Evaluation and Development Board over the SPI
bus, set the `INPUT_INTRF` define as follows:

    #define INPUT_INTRF                     SPI_RX_CODED_INPUT

In addition, program the Ezairo 7100 with the `RSL10_RemoteDongle` application
provided in the utility applications folder. AI1 and AI3 capture the left and
right audio channel data respectively on the Ezairo 7100 Evaluation and 
Development Board.

Various remote microphone custom protocol settings, such as the frequency
channel hopping list (`RM_HOPLIST`), modulation index 
(`app_env.rm_param.mod_idx`), and the remote microphone custom protocol 
streaming address (`app_env.rm_param.accessword`) in the receiver and 
transmitter RSL10 programs need to be matched, in order to successfully 
transmit/receive audio via the remote microphone custom protocol. If the 
Bluetooth address of the receiver and the peer Bluetooth address
(`DIRECT_PEER_BD_ADDRESS`) of the transmitter are matched, on startup, the 
transmitter and receiver exchange/synchronize all the remote microphone custom
protocol parameters as needed to successfully start the audio stream. Note 
that if the RSL10 receiver device’s public address is available in 
`DEVICE_INFO_BLUETOOTH_ADDR` (located in `NVR3`), then it is used as the 
device address. If no public address is defined in the above mentioned address
in `NVR3`, then a pre-defined private address (`PRIVATE_BDADDR`) is used as 
the Bluetooth address for the receiver. Also note that the peer Bluetooth 
address type of the transmitter (`DIRECT_PEER_BD_ADDRESS_TYPE`) needs to match
the device address type of the receiver (public or private). Set the input 
audio source and peer device/address pre-processor definitions to the 
appropriate values and then compile and flash this sample application to the 
RSL10. To test coexistence functionality, connect (and read, write, discover 
services, etc.) to the receiver with a central device while simultaneously 
monitoring the audio stream input and output.

More information on how to ensure that the receiver Bluetooth address and the
transmitter peer address are matched:

Power up the receiver with any configuration from this application; it should
start advertising. Use a Bluetooth Low Energy device to scan for advertising 
devices. Record the address of the receiver (`RemoteMic_Rx`). If the address 
is not `PRIVATE_BDADDR` (0xC01111111111 - default private address), then the
`DIRECT_PEER_BD_ADDRESS` and `DIRECT_PEER_BD_ADDRESS_TYPE` defines (in the
transmitter program) need to be modified.

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
