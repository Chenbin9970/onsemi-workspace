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

#if (PCM_TEST_TONE)
/* Data transmission test: every entry is a 32-bit frame [word0=0x5555,
   word1=0x0000] (value 0x5555), so the output should be constant 0x55550000. */
int32_t pcm_test_buf[PCM_TEST_BUF_LEN] = { 0 };

#if (PCM_TEST_SWEEP)
/* 1k->10k stepped sweep. Each row is one tone at the 24k word rate; 24 words
   hold an exact integer number of cycles (1k=1, 2k=2, ..., 10k=10) so the
   circular DMA has no phase jump. Every word carries the sine (word0 =
   word1) so the full 1-10k range is present on the 24k word stream. */
int16_t pcm_sweep_buf[PCM_SWEEP_TONES][PCM_TEST_BUF_LEN] = {
    { 0, 1553, 3000, 4243, 5196, 5796, 6000, 5796, 5196, 4243, 3000, 1553,
      0, -1553, -3000, -4243, -5196, -5796, -6000, -5796, -5196, -4243,
      -3000, -1553 },                                       /* 1 kHz */
    { 0, 3000, 5196, 6000, 5196, 3000, 0, -3000, -5196, -6000, -5196, -3000,
      0, 3000, 5196, 6000, 5196, 3000, 0, -3000, -5196, -6000, -5196,
      -3000 },                                              /* 2 kHz */
    { 0, 4243, 6000, 4243, 0, -4243, -6000, -4243, 0, 4243, 6000, 4243,
      0, -4243, -6000, -4243, 0, 4243, 6000, 4243, 0, -4243, -6000,
      -4243 },                                              /* 3 kHz */
    { 0, 5196, 5196, 0, -5196, -5196, 0, 5196, 5196, 0, -5196, -5196,
      0, 5196, 5196, 0, -5196, -5196, 0, 5196, 5196, 0, -5196, -5196 },
                                                            /* 4 kHz */
    { 0, 5796, 3000, -4243, -5196, 1553, 6000, 1553, -5196, -4243, 3000,
      5796, 0, -5796, -3000, 4243, 5196, -1553, -6000, -1553, 5196, 4243,
      -3000, -5796 },                                       /* 5 kHz */
    { 0, 6000, 0, -6000, 0, 6000, 0, -6000, 0, 6000, 0, -6000,
      0, 6000, 0, -6000, 0, 6000, 0, -6000, 0, 6000, 0, -6000 },
                                                            /* 6 kHz */
    { 0, 5796, -3000, -4243, 5196, 1553, -6000, 1553, 5196, -4243, -3000,
      5796, 0, -5796, 3000, 4243, -5196, -1553, 6000, -1553, -5196, 4243,
      3000, -5796 },                                        /* 7 kHz */
    { 0, 5196, -5196, 0, 5196, -5196, 0, 5196, -5196, 0, 5196, -5196,
      0, 5196, -5196, 0, 5196, -5196, 0, 5196, -5196, 0, 5196, -5196 },
                                                            /* 8 kHz */
    { 0, 4243, -6000, 4243, 0, -4243, 6000, -4243, 0, 4243, -6000, 4243,
      0, -4243, 6000, -4243, 0, 4243, -6000, 4243, 0, -4243, 6000,
      -4243 },                                              /* 9 kHz */
    { 0, 3000, -5196, 6000, -5196, 3000, 0, -3000, 5196, -6000, 5196,
      -3000, 0, 3000, -5196, 6000, -5196, 3000, 0, -3000, 5196, -6000,
      5196, -3000 },                                        /* 10 kHz */
};
#endif    /* if (PCM_TEST_SWEEP) */
#endif    /* if (PCM_TEST_TONE) */

#if (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT)
/* Raw 12 kHz mono sink from the ASRC (16-bit samples), double buffered so
   DMA4 can re-arm to one buffer while its complete handler packs the other,
   avoiding an ASRC sample-loss window. Each buffer is one 10 ms frame. */
int16_t pcm_raw_buf[2][PCM_FRAME_WORDS];

/* Double-buffered PCM output. Each buffer holds one 10 ms frame of mono
   12 kHz audio packed as one 32-bit write per PCM frame: [word0=s, word1=s]
   (stereo copy, so the 2x16-bit frame is fully populated). DMA4 packs the
   idle buffer; PCM_DMA_NUM (ch5) sends the active one, and its complete
   interrupt swaps them. */
uint32_t pcm_tx_buf[2][PCM_FRAME_WORDS];

#if (PCM_TEST_ASRC)
/* 1 kHz sine at 16 kHz: 16 samples/period, 10 periods per 10 ms frame.
   Filled in Initialize_Receiver_Audio_Output() from the 16-sample table. */
int16_t pcm_asrc_sine_16k[PCM_ASRC_SINE_LEN];
#endif    /* if (PCM_TEST_ASRC) */
#endif    /* if (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT) */

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
    /* Slave mode: the external device (or signal generator) provides BCLK and
       FS on DIO2/DIO3; RSL10 shifts data out on SERO. */
    Sys_PCM_ConfigClk(PCM_SELECT_SLAVE, DIO_WEAK_PULL_UP, PCM_CLK_DO,
                      PCM_FRAME_SYNC, PCM_SER_DI, PCM_SER_DO, DIO_MODE_INPUT);
    Sys_PCM_Config(PCM_CFG_TX);
    Sys_PCM_Enable();

#if !(PCM_TEST_TONE)
    /* Path B output DMA: pcm_tx_buf -> PCM->TX_DATA (M_TO_P, 32-bit frame
       writes). Configured here, armed at link connect so test-tone mode
       (which drives ASRC_OUT_IDX straight to PCM) is unaffected. */
    Sys_DMA_ChannelConfig(PCM_DMA_NUM, RX_DMA_PCM_STEREO, PCM_FRAME_WORDS, 0,
                          (uint32_t)&pcm_tx_buf[0][0], (uint32_t)&PCM->TX_DATA);
#endif    /* if !(PCM_TEST_TONE) */
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
#if !(PCM_TEST_SW_RESAMPLE)
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
#if (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT)
    /* PCM: ASRC outputs 12k mono samples/s, so one 10 ms frame = 120 samples
       into pcm_raw_buf (P_TO_M, linear). DMA4 completes, the handler packs
       them to 32-bit frames, then re-arms. */
    uint32_t asrc_out_len = PCM_FRAME_WORDS;
#else
    uint32_t asrc_out_len = 2 * FRAME_LENGTH;
#endif
    Sys_DMA_ChannelConfig(
        ASRC_OUT_IDX,
        RX_DMA_ASRC_OUT,
        asrc_out_len,
        0,
        (uint32_t)&ASRC->OUT,
        AsrcOutDest //SPI or OD or PCM
        );

    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);
#else
    (void)AsrcOutDest;
#endif
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

#if (PCM_TEST_TONE)
    /* Test mode: bypass decode + ASRC, stream generated tones straight to
     * the PCM slave output. */
    Initialize_Raw_PCM_Output_Type();

#if (PCM_TEST_SWEEP)
    /* 1k->10k stepped sweep: stream the 1 kHz table first; TIMER3 advances
       the DMA source to the next tone every second. */
    Sys_DMA_ChannelConfig(ASRC_OUT_IDX, RX_DMA_PCM_TEST, PCM_TEST_BUF_LEN, 0,
                          (uint32_t)&pcm_sweep_buf[0][0], (uint32_t)&PCM->TX_DATA);
    Sys_DMA_ClearChannelStatus(ASRC_OUT_IDX);
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);

    /* SLOWCLK = 2 MHz, /2 -> 1 MHz, timeout 10,000,000 -> ~10 s tick. */
    Sys_Timer_Set_Control(PCM_SWEEP_TIMER, TIMER_FREE_RUN |
                          (10000000 - 1) | TIMER_SLOWCLK_DIV2);
    NVIC_SetPriority(TIMER_IRQn(PCM_SWEEP_TIMER), 3);
    NVIC_ClearPendingIRQ(TIMER_IRQn(PCM_SWEEP_TIMER));
    NVIC_EnableIRQ(TIMER_IRQn(PCM_SWEEP_TIMER));
    Sys_Timers_Start(1 << PCM_SWEEP_TIMER);
#else    /* if (PCM_TEST_SWEEP) */
    /* 1 kHz pure tone test: stream a 12-point sine at 12k, packed [s,0]
       (sample in high 16 bits, low 16 bits = 0), via ch5 32-bit writes.
       SAI-style start: disable PCM, fill buffer, pre-load, start DMA, enable
       PCM last. */
    Sys_PCM_Config(PCM_CFG_TX);             /* disable (PCM_DISABLE) */
    /* 诊断 pattern：0x8000 = 只有 bit15(符号位)=1，其余 0。
       word0 = TX_DATA 高 16 位（MSB first 先移出），故数据打包在高 16 位。
       逻辑分析仪预期（若干净配置正确）：FS 高电平段第 1 个 BCLK = 1，
       其余全 0 —— 应与 baseline（WORD_SIZE_32 配置）完全一致。 */
    /* 1 kHz sine @ 12 kHz sampling (12 samples/period), bypasses ASRC */
    static const int16_t sine_1k_12k[12] = {
        0, 3000, 5196, 6000, 5196, 3000, 0, -3000, -5196, -6000, -5196, -3000
    };
    for (uint32_t i = 0; i < PCM_TEST_BUF_LEN; i++)
    {
        pcm_test_buf[i] = (uint32_t)(uint16_t)sine_1k_12k[i % 12];  /* 低16=正弦，两声道复制 */
    }
    PCM->TX_DATA = pcm_test_buf[0];         /* pre-load first word */

    Sys_DMA_ChannelConfig(PCM_DMA_NUM, RX_DMA_PCM_TEST, PCM_TEST_BUF_LEN, 0,
                          (uint32_t)pcm_test_buf, (uint32_t)&PCM->TX_DATA);
    Sys_DMA_ClearChannelStatus(PCM_DMA_NUM);
    Sys_DMA_ChannelEnable(PCM_DMA_NUM);
    NVIC_EnableIRQ(DMA_IRQn(PCM_DMA_NUM));  /* re-arm on completion */

    Sys_PCM_Enable();                       /* enable LAST */
#endif    /* if (PCM_TEST_SWEEP) */
    return;
#elif (PCM_TEST_ASRC)
    /* Isolated ASRC test: no wireless / G722 decode. A 1 kHz sine at 16 kHz is
       fed into the ASRC by a 500 us timer (8 samples/tick), down-sampled to
       12 kHz, and streamed to the PCM slave output through the real ch4/ch5
       double-buffer pipeline. */
    Initialize_Raw_PCM_Output_Type();       /* slave; PCM_CFG_TX = disabled */
    Sys_PCM_Config(PCM_CFG_TX);             /* keep disabled until PCM last */

    /* 1 kHz sine at 16 kHz: 16 samples/period, 10 periods per 10 ms frame. */
    static const int16_t sine_1k_16k[16] = {
        0, 2296, 4243, 5544, 6000, 5544, 4243, 2296,
        0, -2296, -4243, -5544, -6000, -5544, -4243, -2296
    };
    for (uint16_t i = 0; i < PCM_ASRC_SINE_LEN; i++)
    {
        pcm_asrc_sine_16k[i] = sine_1k_16k[i % 16];
    }

    /* ch4: ASRC->OUT -> pcm_raw_buf (120 samples / 10 ms frame). */
    Sys_DMA_ChannelConfig(ASRC_OUT_IDX, RX_DMA_ASRC_OUT, PCM_FRAME_WORDS, 0,
                          (uint32_t)&ASRC->OUT, (uint32_t)&pcm_raw_buf[0][0]);
    Sys_DMA_ClearChannelStatus(ASRC_OUT_IDX);

    /* ch5: pcm_tx_buf -> PCM->TX_DATA (32-bit frames). */
    Sys_DMA_ChannelConfig(PCM_DMA_NUM, RX_DMA_PCM_STEREO, PCM_FRAME_WORDS, 0,
                          (uint32_t)&pcm_tx_buf[0][0], (uint32_t)&PCM->TX_DATA);
    Sys_DMA_ClearChannelStatus(PCM_DMA_NUM);
    PCM->TX_DATA = pcm_tx_buf[0][0];        /* pre-load first frame */

    /* Initial 16k->12k ASRC ratio (ideal). ASCC measures the real 12 kHz FS on
       DIO3 and the pacing timer re-locks it via ASRC_Reconfig(). */
    int64_t cr = FRAME_LENGTH << SHIFT_BIT;           /* 160 << 20 */
    int64_t ck = (3 * FRAME_LENGTH / 4) << SHIFT_BIT; /* 120 << 20 */
    Sys_ASRC_Config((uint32_t)((((cr - ck) << 28) / ck) & 0xFFFFFFFF),
                    WIDE_BAND | ASRC_DEC_MODE2);
    Initialize_ASCC();

    /* NVIC for ch4/ch5 (ch3 is re-armed by the pacing timer). */
    NVIC_SetPriority(DMA_IRQn(ASRC_OUT_IDX), 3);
    NVIC_SetPriority(DMA_IRQn(PCM_DMA_NUM), 3);
    NVIC_ClearPendingIRQ(DMA_IRQn(ASRC_OUT_IDX));
    NVIC_ClearPendingIRQ(DMA_IRQn(PCM_DMA_NUM));
    NVIC_EnableIRQ(DMA_IRQn(ASRC_OUT_IDX));
    NVIC_EnableIRQ(DMA_IRQn(PCM_DMA_NUM));
    NVIC_ClearPendingIRQ(DMA_IRQn(ASRC_IN_IDX));
    NVIC_EnableIRQ(DMA_IRQn(ASRC_IN_IDX));

    /* Start the double-buffer pipeline, then PCM last (SAI-style). */
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);
    Sys_DMA_ChannelEnable(PCM_DMA_NUM);

    /* Pacing timer: 500 us free-run feeds 8 samples (16 kHz) into the ASRC. */
    Sys_Timer_Set_Control(TIMER_REGUL, TIMER_FREE_RUN | (500 - 1) |
                          TIMER_SLOWCLK_DIV2);
    NVIC_SetPriority(TIMER_IRQn(TIMER_REGUL), 2);
    NVIC_ClearPendingIRQ(TIMER_IRQn(TIMER_REGUL));
    NVIC_EnableIRQ(TIMER_IRQn(TIMER_REGUL));
    Sys_Timers_Start(1 << TIMER_REGUL);

    Sys_PCM_Enable();                       /* enable PCM LAST */
    return;
#else    /* if (PCM_TEST_TONE) */

    /* Initialize Codec Framework and load code into LPDSP32 */
	App_CodecInitialize();

    /* Configure interrupt priorities. */
    NVIC_SetPriority(TIMER_IRQn(TIMER_REGUL), 2);

#if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT)
    AsrcOutDest = (uint32_t)&SPI0->TX_DATA;
    Initialize_Raw_SPI_Output_Type();
#elif (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT)
    AsrcOutDest = (uint32_t)pcm_raw_buf;
    Initialize_Raw_PCM_Output_Type();
#else    /* if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT) */
    AsrcOutDest = (uint32_t)BufferOut;
    Initialize_Raw_OD_Output_Type();
#endif    /* if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT) */

    /* Configure the Audio Sink Clock Counters */
    Initialize_ASCC();

    /* Configure the ASRC and the required DMA channels */
	Initialize_ASRC(AsrcOutDest);
#endif    /* if (PCM_TEST_TONE) */
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

    /* Audio-path debug DIOs (scope these to find the restart): DIO6 = DSP0
       (decode done), DIO8 = ch4 (ASRC->raw complete, DIO15 is RM debug),
       DIO9 = ch5 (PCM complete). */
    Sys_DIO_Config(6, DIO_MODE_GPIO_OUT_0);
    Sys_DIO_Config(8, DIO_MODE_GPIO_OUT_0);
    Sys_DIO_Config(9, DIO_MODE_GPIO_OUT_0);

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
