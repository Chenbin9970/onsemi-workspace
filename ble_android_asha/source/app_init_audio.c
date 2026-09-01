/* ----------------------------------------------------------------------------
 * Copyright (c) 2017 Semiconductor Components Industries, LLC (d/b/a
 * ON Semiconductor), All Rights Reserved
 *
 * This code is the property of ON Semiconductor and may not be redistributed
 * in any form without prior written permission from ON Semiconductor.
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 *
 * This is Reusable Code.
 *
 * ----------------------------------------------------------------------------
 * app_init.c
 * - Audio application initialization function
 * ------------------------------------------------------------------------- */

#include <app_audio.h>
#include <rsl10.h>
#include "queue.h"

/* Define the LPDSP32 Context */
LPDSP32Context lpdsp32;
extern struct queue_t audio_queue;

/* ----------------------------------------------------------------------------
 * Function      : void App_CodecInitialize(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize the codec subsystem
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
static void App_CodecInitialize(void)
{
    /* Initialise the dsp context */
    lpdsp32.state    = DSP_STARTING;
    lpdsp32.channels.action  = PACKED | DECODE_RESET | DECODE;

    /* Set the framesize and block length */
    lpdsp32.channels.frameSize       = FRAME_LENGTH;
    lpdsp32.channels.blockSize       = FRAME_LENGTH;

    /* Set the mode and channel, at the moment the channel is encoded as the
     * top four bits of the field
     */
    lpdsp32.channels.modeAndChannel  = 0x00 | CODEC_MODE;

    /* Sample rate is not needed in G722 */
    lpdsp32.channels.sampleRate      = 0;

    /* Create a codec located in the configuration area */
    lpdsp32.codec = populateG722PLCDSPCodec(Buffer.configuration,
                                            CODEC_CONFIGURATION_SIZE);
    if (!codecIsCodec(lpdsp32.codec))
    {
        return;
    }

    /* initialise the codec */
    codecInitialise(lpdsp32.codec);

    /* Configure the static parts of the codec */
    codecSetStatusBuffer(lpdsp32.codec, Buffer.configuration,
                         CODEC_CONFIGURATION_SIZE);
    codecSetOutputBuffer(lpdsp32.codec, Buffer.output, CODEC_OUTPUT_SIZE);

    /* In the current implementation, the management of the data into the codec
     * is handled by a queing mechanism, as such only one input buffer is
     * required, this can be hard coded here.
     */
    codecSetInputBuffer(lpdsp32.codec, Buffer.input, CODEC_INPUT_SIZE);

    /* we have a handshake protocol to ensure the DSP is alive and in synch
     * before we try to use it.
     */
    dspHandshake(lpdsp32.codec);

    lpdsp32.outgoing = Buffer.output;
    lpdsp32.incoming = Buffer.input;

    lpdsp32.state = DSP_IDLE;
    lpdsp32.channels.action = DECODE_RESET | DECODE;
    codecSetParameters(lpdsp32.codec, &lpdsp32.channels);
}

/* ----------------------------------------------------------------------------
 * Function      : void Audio_Initialize_System(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize audio functions (TX/RX)
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Audio_Initialize_System(void)
{
    App_CodecInitialize();

#if (OUTPUT_INTRF == SPI_TX_OUTPUT)

    /* SPI disabled: DIO0/DIO1 are repurposed for the 7100 I2C bus
     * (SCL=DIO0, SDA=DIO1). See i2c_7100_hal.c. */
#if 0
    /* Initialize SPI interface */
    Sys_SPI_DIOConfig(0, SPI0_SELECT_MASTER,
                      DIO_LPF_DISABLE | DIO_WEAK_PULL_UP, CLK_DO, CS_DO, SER_DI,
                      SER_DO);

    /* Configure the SPI0 interface */
    Sys_SPI_Config(0, SPI0_SELECT_MASTER | SPI0_ENABLE |
                   SPI0_CLK_POLARITY_NORMAL | SPI0_CONTROLLER_DMA |
                   SPI0_MODE_SELECT_AUTO | SPI0_PRESCALE_32);
    Sys_SPI_TransferConfig(0, SPI0_START | SPI0_WRITE_DATA | SPI0_CS_1 |
                           SPI0_WORD_SIZE_16);
#endif
#endif    /* if (OUTPUT_INTRF == SPI_TX_OUTPUT) */

    /* Start period count */
    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = 1;

    /* Enable ASRC error IRQ */
    Sys_ASRC_IntEnableConfig(INT_EBL_ASRC_IN_ERR | INT_EBL_ASRC_UPDATE_ERR);
    ASRC_CTRL->ASRC_DISABLE_ALIAS = ASRC_DISABLED_BITBAND;

    /* Configuration of DMA channels */
    /* Configure DMA for ASRC input */
    Sys_DMA_ChannelConfig(
        ASRC_IN_IDX,
        RX_DMA_ASRC_IN,
        SUBFRAME_LENGTH,
        0,
        (uint32_t)lpdsp32.outgoing,
        (uint32_t)&ASRC->IN
        );

    /* Configure DMA for ASRC output to Port*/
#if (OUTPUT_INTRF == SPI_TX_OUTPUT)
    Sys_DMA_ChannelConfig(
        ASRC_OUT_IDX,
        RX_DMA_ASRC_OUT,
        1,
        0,
        (uint32_t)&ASRC->OUT,
        (uint32_t)&SPI0->TX_DATA
        );
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);
#else    /* if (OUTPUT_INTRF == SPI_TX_OUTPUT) */
    Sys_DMA_ChannelConfig(
        CODE_IDX,
        TX_DMA_SPI,
        FRAME_LENGTH / 2,
        0,
        (uint32_t)&env_audio.frame_in_prev,
        (uint32_t)&SPI0->TX_DATA
        );
#endif    /* if (OUTPUT_INTRF == SPI_TX_OUTPUT) */

    /* Configuration of Audio Sink Clock Counters */
    Sys_Audiosink_ResetCounters();
    Sys_Audiosink_InputClock(0,
                             ((uint32_t)(SAMPL_CLK << DIO_AUDIOSINK_SRC_CLK_Pos)));
    Sys_Audiosink_Config(AUDIO_SINK_PERIODS_16, 0, 0);

#if SIMUL
    Sys_Timer_Set_Control(TIMER_SIMUL, TIMER_FREE_RUN | (SIMUL_TIME_US - 1) | TIMER_SLOWCLK_DIV2);
    Sys_Timers_Start(1U << TIMER_SIMUL);
    SYSCTRL->RF_ACCESS_CFG = RF_ACCESS_ENABLE_BITBAND | RF_IRQ_ACCESS_ENABLE;
    NVIC_EnableIRQ(TIMER_IRQn(TIMER_SIMUL));
#else    /* if SIMUL */
    Sys_BBIF_SyncConfig(SYNC_ENABLE | SYNC_SOURCE_BLE_RX, 0,
                        SLAVE_CONNECT);
#endif    /* if SIMUL */

    Reset_Audio_Sync_Param(&env_sync, &env_audio);

    Sys_DIO_Config(ASCC_PHASE_ISR_DIO, DIO_MODE_GPIO_OUT_0);

    Sys_DIO_Config(ASHA_EVENT_DIO, DIO_MODE_GPIO_OUT_0);

    NVIC_SetPriority(DSP0_IRQn, 4);
    NVIC_SetPriority(TIMER_IRQn(TIMER_RENDER), 1);

    NVIC_SetPriority(AUDIOSINK_PERIOD_IRQn, 0);
    NVIC_SetPriority(AUDIOSINK_PHASE_IRQn, 0);

    NVIC_SetPriority(DMA_IRQn(ASRC_IN_IDX), 0);

    NVIC_SetPriority(BLE_EVENT_IRQn, 0);
    NVIC_SetPriority(BLE_RX_IRQn, 0);
    NVIC_SetPriority(BLE_CRYPT_IRQn, 0);
    NVIC_SetPriority(BLE_ERROR_IRQn, 0);
    NVIC_SetPriority(BLE_SW_IRQn, 0);
    NVIC_SetPriority(BLE_GROSSTGTIM_IRQn, 0);
    NVIC_SetPriority(BLE_FINETGTIM_IRQn, 0);
    NVIC_SetPriority(BLE_CSCNT_IRQn, 0);
    NVIC_SetPriority(BLE_SLP_IRQn, 0);


    QueueInit(&audio_queue);

    APP_ResetPrevSeqNumber();

#if 0
    /* Setup DIO5 as a GPIO input with interrupts on transitions, DIO6 as a
     * GPIO output. Use the integrated debounce circuit to ensure that only a
     * single interrupt event occurs for each push of the pushbutton.
     * The debounce circuit always has to be used in combination with the
     * transition mode to deal with the debounce circuit limitations.
     * A debounce filter time of 50 ms is used. */
    Sys_DIO_Config(BUTTON_DIO, DIO_MODE_GPIO_IN_0 | DIO_WEAK_PULL_UP |
                   DIO_LPF_DISABLE);
    Sys_DIO_IntConfig(0, DIO_EVENT_TRANSITION | DIO_SRC(BUTTON_DIO) |
                      DIO_DEBOUNCE_ENABLE,
                      DIO_DEBOUNCE_SLOWCLK_DIV1024, 49);
    NVIC_EnableIRQ(DIO0_IRQn);
#endif
}
