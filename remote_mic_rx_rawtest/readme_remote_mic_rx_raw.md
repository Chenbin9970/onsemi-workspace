Mono Raw Audio Stream Broadcast Receiver Custom Protocol Sample Application
======================

NOTE: If you use this sample application for your own purposes, follow the
licensing agreement specified in `Software Use Agreement - use and` 
`accept (ONIPLAW 08142020).pdf` in the root directory of the installed CMSIS-Pack.

Overview
--------
This sample code demonstrates the functionality of a receiver that receives
audio data wirelessly from a corresponding transmitter using the Audio Stream
Broadcast Custom Protocol (remote microphone custom protocol). It showcases 
the RSL10 firmware implementation required for remote microphone or wireless
auxiliary audio input use cases. This sample code illustrates how the receiver
decrypts and decodes the encoded audio data received from the transmitter 
over the air, and how to configure the required peripheral and audio 
processing hardware blocks for audio output. More specifically, the single 
(left or right) channel wireless audio stream is received via the remote 
microphone custom protocol, decoded by the LPDSP32 DSP, sent to the 
Asynchronous Sample Rate Converter (ASRC) to synchronize the audio sampling 
rate between the audio renderer and RSL10, and finally, sent to either the 
Serial Peripheral Interface (SPI) or Output Driver (OD) interface. The audio 
output on the RSL10 receiver can be configured to be sent to the internal 
output driver, or an external device such as the Ezairo 7100 Evaluation and 
Development Board. The LPDSP32 decoder can be configured to use the G.722 or 
CELT CODEC options.

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

The appropriate libraries and include files are loaded according to the build
configuration selected.

Hardware Requirements
---------------------
This application can be executed on any RSL10 Evaluation and Development 
Board. 

If the raw (decoded) audio output from the RSL10 receiver is configured to be
sent to the SPI bus, then an Ezairo 7100 Evaluation and Development Board can 
be used to pass the SPI audio output through to its output driver.
Alternatively, the raw audio output in the RSL10 receiver can also be
configured to be sent to its internal output driver.

For the receiver configuration where the RSL10 audio output is sent to the
Ezairo 7100 over the SPI bus, ensure that the following pins are connected
between the two devices:

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

For the receiver configuration where the RSL10 audio output is sent to the
internal output driver, ensure that the output driver interface is configured
to be as follows:

    ------------------
    Pin Signal | RSL10
    ------------------
    OD_P       | DIO0
    OD_N       | DIO1
    ------------------

Ensure that the output driver is connected to the headphone as follows:

- Headphone+ to DIO0 through an inductor (e.g. 680 uH)
- Headphone- to DIO1 through an inductor (e.g. 680 uH)

The inductors are required to filter high frequency currents, unless the
headphone has sufficient self-inductance.

In addition, note the following when setting up the various hardware 
configurations: 

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
received from the transmitter can be monitored on the receiver by recording
audio output on either the RSL10 output driver or the Ezairo 7100 output 
driver. In addition to ensuring that the hardware setup is as outlined in the 
Hardware Requirements section for the various receiver configurations, this 
sample code needs to be compiled and flashed on to the RSL10 Evaluation and
Development Board with the appropriate pre-processor definition settings, to
ensure that the receiver can function as expected. The corresponding 
Ezairo 7100 firmware also needs to be flashed onto the Ezairo 7100 Evaluation
and Development Board, for those configurations where it is used. The 
Ezairo 7100 firmware required to process the transmitted data from the RSL10 
is provided in the `RSL10_Utility_Apps.zip` file.

The sample project has build configurations that allow you to select the
G.722 or CELT decoder. This option is defined in the project settings by the 
following preprocessor symbols:
`CODEC_CONFIG == CODEC_CONFIG_G722` or `CODEC_CONFIG == CODEC_CONFIG_CELT` 

For the receiver configuration where the RSL10 audio output is sent to the
Ezairo 7100 over the SPI bus, compile and flash this sample application with
the `OUTPUT_INTRF` define as follows:

    #define OUTPUT_INTRF                    SPI_TX_RAW_OUTPUT

In addition, program the Ezairo 7100 with the `audio_spi_slave` application 
provided in the utility applications folder. The audio output can be monitored
through the first RCA output channel (RCVR0 - with FILTEN0 pins 1-2 and 
3-4 shorted) on the Ezairo 7100 Evaluation and Development Board.

For the receiver configuration where the RSL10 audio output is sent to the
internal output driver, compile and flash this sample application with the
`OUTPUT_INTRF` define as follows:

    #define OUTPUT_INTRF                    OD_TX_RAW_OUTPUT

The audio output can be monitored through `OD_P_DIO` and `OD_N_DIO` if the
headphones/speakers are connected to the output driver interface as described
in the *Hardware Requirements* section.

By default, this sample code is configured to play the right channel,
extracted from the stereo audio stream being broadcast by the transmitter. 
Press the push-button (DIO5) to switch to the left channel; or alternatively,
modify the `APP_RM_AUDIO_CHANNEL` define as follows to play the left channel 
on the receiver by default:

    #define APP_RM_AUDIO_CHANNEL            RM_LEFT

This sample code plays a 1 kHz tone for approximately one second immediately
after boot-up; therefore, this mechanism can be used to isolate hardware setup
issues to either the transmitter or receiver, if no discernible audio is being
played through the loop.

Note that the frequency channel hopping list (`RM_HOPLIST`), 
modulation index (`app_env.rm_param.mod_idx`), and remote microphone custom
protocol streaming address (`app_env.rm_param.accessword`) in the receiver and
transmitter RSL10 programs need to be matched, in order to successfully 
transmit/receive audio via the remote microphone custom protocol.

To enable end-to-end AES-128 ECB encryption, you can enable the crypto 
flag as below:

    #define CRY_AES_128_ECB                 1

Ensure that the crypto flag has been enabled on the transmitter side
and the same key (`KEY_AES_128_ECB`) is used.

Notes
=====
The RSL10 and/or Ezairo 7100 can be damaged if the corresponding
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
