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
 * - Contains application initialization code for hardware
 * ----------------------------------------------------------------------------
 * $Revision: 1.24 $
 * $Date: 2019/09/03 22:09:17 $
 * ------------------------------------------------------------------------- */

#include "app.h"

#include "codecs/codec.h"
#include "codecs/baseDSP/baseDSPCodec.h"
#include "sharedBuffers.h"

/* Define the LPDSP32 Context */
LPDSP32Context lpdsp32;

int16_t BufferOut[2 * FRAME_LENGTH] =
{
    /* This 1-kHz tone is played initially on the SPI for debugging purposes.
     * If you hear a tone upon turning on the HW, your HW setup/connection
     * with Ezairo is correct */
    0, 49, 90, 117, 127, 117, 90, 49, 0, -49, -90, -117, -127, -117, -90, -49,
};

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
    lpdsp32.state = DSP_STARTING;

    /* Link shared buffers to LPDSP32 context variable */
    lpdsp32.outgoing  = Buffer.output;
    lpdsp32.incoming = Buffer.input[0];

    /* Set the mode and channel, at the moment the channel is encoded as the
     * top four bits of the field */
    lpdsp32.channels.modeAndChannel = 0x00 | CODEC_MODE;

    /* Configure subframe length and block size */
    lpdsp32.channels.frameSize = SUBFRAME_LENGTH;
    lpdsp32.channels.blockSize = CODEC_BLOCK_SIZE;
    lpdsp32.channels.sampleRate = CODEC_SAMPLE_RATE;

    /* Create a codec located in the configuration area */
    memset(Buffer.configuration, 0, CODEC_CONFIGURATION_SIZE);
    lpdsp32.codec = POPULATE_CODEC_FUNCTION(Buffer.configuration, CODEC_CONFIGURATION_SIZE);

    if (!codecIsCodec(lpdsp32.codec))
    {
        return;
    }

    /* initialise the codec */
    codecInitialise(lpdsp32.codec);

    /* we have a handshake protocol to ensure the DSP is alive and in synch
     * before we try to use it. */
    dspHandshake(lpdsp32.codec);

    /* Configure the static parts of the codec */
    codecSetStatusBuffer(lpdsp32.codec, Buffer.configuration, CODEC_CONFIGURATION_SIZE);
    codecSetOutputBuffer(lpdsp32.codec, Buffer.output, CODEC_OUTPUT_SIZE);
    codecSetInputBuffer(lpdsp32.codec, Buffer.input[0], CODEC_INPUT_SIZE);

    lpdsp32.channels.action  = CONFIGURE;
    codecSetParameters(lpdsp32.codec, &lpdsp32.channels);
    codecConfigure(lpdsp32.codec);

    /* Setup the left and right channels to perform a decode with reset. */
    lpdsp32.channels.action = CODEC_ACTION;
}

/* ----------------------------------------------------------------------------
 * Function      : void Initialize_Raw_SPI_Output_Type(void)
 * ----------------------------------------------------------------------------
 * Description   : Configure SPI peripheral and DMA
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Initialize_Raw_SPI_Output_Type(void)
{
    /* Initialize SPI interface in Master Mode. */
    Sys_SPI_DIOConfig(0, SPI0_SELECT_MASTER,
                      DIO_LPF_DISABLE |
                      DIO_WEAK_PULL_UP, SPI_CLK_DO, SPI_CS_DO, SPI_SER_DI,
                      SPI_SER_DO);

    /* Configure the SPI0 interface in Master Mode. */
    Sys_SPI_Config(0, SPI0_SELECT_MASTER | SPI0_ENABLE |
                   SPI0_CLK_POLARITY_NORMAL | SPI0_CONTROLLER_DMA |
                   SPI0_MODE_SELECT_AUTO | SPI0_PRESCALE_32);

    /* Configure SPI0 Master for 16-bit transfers. */
    Sys_SPI_TransferConfig(0, SPI0_START | SPI0_WRITE_DATA | SPI0_CS_0 |
                           SPI0_WORD_SIZE_16);

    /* Generates an initial tone on the SPI for debugging purposes. If you hear a short
     * tone upon turning on, your HW setup/connection with Ezairo is correct */
    /* Configure a DMA for tone generation */
    Sys_DMA_ChannelConfig(
        ASRC_OUT_IDX,
        SIN_DMA_SPI,
        16,
        0,
        (uint32_t)BufferOut,
        (uint32_t)&(SPI0->TX_DATA)
        );

    for (uint32_t i = 0; i < 500; i++)
    {
        Sys_Watchdog_Refresh();
        Sys_Delay_ProgramROM(16000);
        Sys_DMA_ChannelEnable(ASRC_OUT_IDX);
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void Initialize_Raw_OD_Output_Type(void)
 * ----------------------------------------------------------------------------
 * Description   : Configure OD peripheral
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Initialize_Raw_OD_Output_Type(void)
{

	Sys_Clocks_SystemClkPrescale1(AUDIOCLK_PRESCALE_5);
	Sys_Audio_Set_Config(AUDIO_CONFIG);
	AUDIO->OD_CFG = (DCRM_CUTOFF_240HZ | DITHER_ENABLE);
		AUDIO->SDM_CFG = 0x00002;
		AUDIO->OD_GAIN = 0xfff;
		Sys_DIO_Config(OD_P_DIO, DIO_6X_DRIVE | DIO_LPF_DISABLE |
					  DIO_NO_PULL | DIO_MODE_OD_P);

		Sys_DMA_ChannelDisable(OD_DMA_NUM);
		Sys_DMA_ChannelConfig(OD_DMA_NUM, RX_DMA_OD, 16, 0,
							 (uint32_t)BufferOut, (uint32_t)&(AUDIO->OD_DATA));

    /* Configure OD DMA */
    Sys_DMA_ChannelConfig(
        OD_DMA_NUM,
        RX_DMA_OD,
        16,
        0,
        (uint32_t)BufferOut,
        (uint32_t)&(AUDIO->OD_DATA)
        );

    for (uint32_t i = 0; i < 10000; i++)
    {
        Sys_Watchdog_Refresh();
        Sys_Delay_ProgramROM(1000);
    }

    DMA_CTRL1[OD_DMA_NUM].TRANSFER_LENGTH_SHORT = 2 * FRAME_LENGTH;
}

/* ----------------------------------------------------------------------------
 * Function      : void Initialize_Raw_PCM_Output_Type(void)
 * ----------------------------------------------------------------------------
 * Description   : Configure the PCM interface as slave for raw audio output
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Initialize_Raw_PCM_Output_Type(void)
{
    /* The external device is the clock master and provides BCLK and FS.
     * RSL10 is the PCM slave: CLK=DIO2, FRAME=DIO3 are inputs, SERO=DIO14
     * shifts audio data out, SERI=DIO4 is an unused input. */
    Sys_PCM_ConfigClk(PCM_SELECT_SLAVE, DIO_WEAK_PULL_UP, PCM_CLK_DO,
                      PCM_FRAME_SYNC, PCM_SER_DI, PCM_SER_DO, DIO_MODE_INPUT);
    Sys_PCM_Config(PCM_CFG_TX);
    Sys_PCM_Enable();
}

/* ----------------------------------------------------------------------------
 * Function      : void Initialize_ASCC(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize the Audio Sink Clock Counters
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Initialize_ASCC(void)
{
    /* Configuration of Audio Sink Clock Counters (ASCC). */
    Sys_Audiosink_ResetCounters();
    Sys_Audiosink_InputClock(0, SAMPLING_CLK_SRC);
    Sys_Audiosink_Config(AUDIO_SINK_PERIODS_16, 0, 0);

    /* Start Audio Sink Phase and Period measurement. */
    AUDIOSINK_CTRL->PHASE_CNT_START_ALIAS  = PHASE_CNT_START_BITBAND;
    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = PERIOD_CNT_START_BITBAND;

    /* Set interrupt priority */
    NVIC_SetPriority(AUDIOSINK_PERIOD_IRQn, 4);
    NVIC_SetPriority(AUDIOSINK_PHASE_IRQn, 4);

    /* Clear and enabled ASRC interrupts */
    NVIC_ClearPendingIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_ClearPendingIRQ(AUDIOSINK_PERIOD_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);
}

/* ----------------------------------------------------------------------------
 * Function      : void Initialize_ASRC(uint32_t AsrcOutDest)
 * ----------------------------------------------------------------------------
 * Description   : Initialize the ASRC and its input and output DMA channels
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Initialize_ASRC(uint32_t AsrcOutDest)
{
    /* Setup DMA channel for transferring data from memory to ASRC interface. */
    Sys_DMA_ChannelConfig(
        ASRC_IN_IDX,
        RX_DMA_ASRC_IN,
        SUBFRAME_LENGTH,
        0,
        (uint32_t)lpdsp32.outgoing,
        (uint32_t)&ASRC->IN
        );

    /* Clear any pending ASRC DMA channel requests. */
    Sys_DMA_ClearChannelStatus(ASRC_IN_IDX);

    /* Setup DMA channel for transferring data from ASRC to a port. */
    Sys_DMA_ChannelConfig(
        ASRC_OUT_IDX,
        RX_DMA_ASRC_OUT,
        2 * FRAME_LENGTH,
        0,
        (uint32_t)&ASRC->OUT,
        AsrcOutDest //SPI or OD
        );

    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);
}

/* ----------------------------------------------------------------------------
 * Function      : void Initialize_Receiver_Audio_Output(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize SPI and DMA resources for audio rendering
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Initialize_Receiver_Audio_Output(void)
{
    uint32_t AsrcOutDest;

    /* Enable DSP interrupt. */
    NVIC_SetPriority(DSP0_IRQn, 2);
    NVIC_EnableIRQ(DSP0_IRQn);

    /* Initialize Codec Framework and load code into LPDSP32 */
	App_CodecInitialize();

    /* Configure interrupt priorities. */
    NVIC_SetPriority(TIMER_IRQn(TIMER_REGUL), 2);

#if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT)
    AsrcOutDest = (uint32_t)&SPI0->TX_DATA;
    Initialize_Raw_SPI_Output_Type();
#elif (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT)
    AsrcOutDest = (uint32_t)&PCM->TX_DATA;
    Initialize_Raw_PCM_Output_Type();
#else    /* if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT) */
    AsrcOutDest = (uint32_t)BufferOut;
    Initialize_Raw_OD_Output_Type();
#endif    /* if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT) */

    /* Configure the Audio Sink Clock Counters */
    Initialize_ASCC();

    /* Configure the ASRC and the required DMA channels */
	Initialize_ASRC(AsrcOutDest);
}

/* ----------------------------------------------------------------------------
 * Function      : void App_Initialize(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize the system for proper application execution.
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void App_Initialize(void)
{
    /* Initialize the ISR Vector Table address in VTOR. */
    SCB->VTOR = (unsigned int)(&ISR_Vector_Table);

    /* Mask all interrupts */
    __set_PRIMASK(PRIMASK_DISABLE_INTERRUPTS);

    /* Disable all interrupts and clear any pending interrupts. */
    Sys_NVIC_DisableAllInt();
    Sys_NVIC_ClearAllPendingInt();

    /* Disable operation of DIO 13-15 in JTAG mode */
    DIO_JTAG_SW_PAD_CFG->CM3_JTAG_DATA_EN_ALIAS =
        CM3_JTAG_DATA_DISABLED_BITBAND;
    DIO_JTAG_SW_PAD_CFG->CM3_JTAG_TRST_EN_ALIAS =
        CM3_JTAG_TRST_DISABLED_BITBAND;

    /* Test the recovery DIO to pause the program to make it easy to
     * re-flash. Moved to DIO12 because DIO14 is now the PCM serial data
     * output. */
    DIO->CFG[RECOVERY_DIO] = DIO_MODE_INPUT | DIO_WEAK_PULL_UP |
                             DIO_LPF_DISABLE | DIO_6X_DRIVE;
    while (DIO_DATA->ALIAS[RECOVERY_DIO] == 0);

    /* Configure the current trim settings for VCC, VDDA. */
    ACS_VCC_CTRL->ICH_TRIM_BYTE  = VCC_ICHTRIM_16MA_BYTE;
    ACS_VDDA_CP_CTRL->PTRIM_BYTE = VDDA_PTRIM_16MA_BYTE;

    /* Start and configure VDDRF. */
    ACS_VDDRF_CTRL->ENABLE_ALIAS = VDDRF_ENABLE_BITBAND;
    ACS_VDDRF_CTRL->CLAMP_ALIAS  = VDDRF_DISABLE_HIZ_BITBAND;

    /* Wait until VDDRF supply has powered up. */
    while (ACS_VDDRF_CTRL->READY_ALIAS != VDDRF_READY_BITBAND);

    /* Disable RF power amplifier. */
    ACS_VDDPA_CTRL->ENABLE_ALIAS = VDDPA_DISABLE_BITBAND;
    ACS_VDDPA_CTRL->VDDPA_SW_CTRL_ALIAS    = VDDPA_SW_VDDRF_BITBAND;

    /* Enable RF power switches. */
    SYSCTRL_RF_POWER_CFG->RF_POWER_ALIAS   = RF_POWER_ENABLE_BITBAND;

    /* Remove RF isolation. */
    SYSCTRL_RF_ACCESS_CFG->RF_ACCESS_ALIAS = RF_ACCESS_ENABLE_BITBAND;

    /* Start the 48 MHz oscillator without changing the other register bits. */
    RF->XTAL_CTRL = ((RF->XTAL_CTRL & ~XTAL_CTRL_DISABLE_OSCILLATOR) |
                     XTAL_CTRL_REG_VALUE_SEL_INTERNAL);

    /* Enable the 48 MHz oscillator divider using the desired prescale value. */
    RF_REG2F->CK_DIV_1_6_CK_DIV_1_6_BYTE = CK_DIV_1_6_PRESCALE_3_BYTE;

    /* Wait until 48 MHz oscillator is started. */
    while (RF_REG39->ANALOG_INFO_CLK_DIG_READY_ALIAS !=
           ANALOG_INFO_CLK_DIG_READY_BITBAND);

    /* Switch to (divided 48 MHz) oscillator clock. */
    Sys_Clocks_SystemClkConfig(JTCK_PRESCALE_1   |
                               EXTCLK_PRESCALE_1 |
                               SYSCLK_CLKSRC_RFCLK);

    /* Configure clock dividers. */
    CLK->DIV_CFG0 = (SLOWCLK_PRESCALE_8 | BBCLK_PRESCALE_2 |
                     USRCLK_PRESCALE_1);
    CLK->DIV_CFG2 = (CPCLK_PRESCALE_12 | DCCLK_PRESCALE_4);

    /* Wake-up and apply clock to the BLE base-band interface. */
    BBIF->CTRL    = (BB_CLK_ENABLE | BBCLK_DIVIDER_8 | BB_WAKEUP);

    /* Enable 6dBM or 0dBM mode. */
	#if (OUTPUT_POWER_6DBM)
		Sys_RFFE_SetTXPower(6);
	#else    /* 0DBM */
		Sys_RFFE_SetTXPower(0);
	#endif    /* CFG_6DBM */

    /* Enable Flash overlay. */
    memcpy((uint8_t *)PRAM0_BASE, (uint8_t *)FLASH_MAIN_BASE, PRAM0_SIZE);
    memcpy((uint8_t *)PRAM1_BASE, (uint8_t *)(FLASH_MAIN_BASE + PRAM0_SIZE),
           PRAM1_SIZE);
    memcpy((uint8_t *)PRAM2_BASE,
           (uint8_t *)(FLASH_MAIN_BASE + PRAM0_SIZE + PRAM1_SIZE),
           PRAM2_SIZE);
    memcpy((uint8_t *)PRAM3_BASE,
           (uint8_t *)(FLASH_MAIN_BASE + PRAM0_SIZE + PRAM1_SIZE + PRAM2_SIZE),
           PRAM3_SIZE);
    SYSCTRL->FLASH_OVERLAY_CFG  = 0xf;

    /* Enable CM3 loop cache. */
    SYSCTRL->CSS_LOOP_CACHE_CFG = CSS_LOOP_CACHE_ENABLE;

    /* Configure Button on the Evaluation and Development Board to toggle
     * ear-side (left or right) on the receiver. */

    Sys_DIO_Config(DEBUG_DIO_FIRST, DIO_MODE_GPIO_OUT_0);
    Sys_DIO_Config(DEBUG_DIO_SECOND, DIO_MODE_GPIO_OUT_0);
    Sys_DIO_Config(DEBUG_DIO_THIRD, DIO_MODE_GPIO_OUT_0);
    Sys_GPIO_Set_Low(DEBUG_DIO_FIRST);

    /* Initialize Receiver Audio Input Source */
    Initialize_Receiver_Audio_Output();

    /* Zero content of BufferOut to stop the sinwave on OD */
    ClearBufferOut();

    /* Un-mask interrupts. */
    __set_PRIMASK(PRIMASK_ENABLE_INTERRUPTS);
    __set_FAULTMASK(FAULTMASK_ENABLE_INTERRUPTS);
}

/* ----------------------------------------------------------------------------
 * Function      : void ClearBufferOut(void)
 * ----------------------------------------------------------------------------
 * Description   : Clear content of BufferOut for audio output
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void ClearBufferOut(void)
{
    memset(BufferOut, 0, sizeof(BufferOut));
}
