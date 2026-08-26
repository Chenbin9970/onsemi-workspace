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
 * app_func.c
 * - Application functions
 * ----------------------------------------------------------------------------
 * $Revision: 1.23 $
 * $Date: 2019/09/03 22:09:17 $
 * ------------------------------------------------------------------------- */

#include "app.h"

#include "sharedBuffers.h"

uint8_t round_key[(AES128_ROUNDS + 1) * AES128_KEY_LENGTH];
uint8_t ear_side = APP_RM_AUDIO_CHANNEL;
int64_t audio_sink_cnt   = 0;
int64_t audio_sink_period_cnt = 0;
bool flag_ascc_phase  = false;
uint8_t frame_idx = 0;
bool frame_decoded    = true;
bool phase_cnt_missed = false;
uint8_t *frame_in;

/* Forward Declarations */
void LPDSP32_Start_DEC(uint8_t *src_addr);

void Renderer(uint8_t *src_addr);

void ASRC_Reconfig(void);

void Decry_AES_128 (uint8_t *ptr, uint8_t block_num);

/* ----------------------------------------------------------------------------
 * Function      : void Decry_AES_128 (uint8_t *ptr, uint8_t block_num)
 * ----------------------------------------------------------------------------
 * Description   : Decryption the data on over-write the result result
 * Inputs        : ptr       - Pointer to packet data in memory
 *                 block_num - Number of blocks (16 bytes) need to be encrypted
 * Outputs       : None
 * Assumptions   : 'aes128_key_expand' has been already called for 'round_key'
 * ------------------------------------------------------------------------- */
void Decry_AES_128 (uint8_t *ptr, uint8_t block_num)
{
    int i = 0;
    uint8_t *ptr_tmp = ptr;
    for (i = 0; i < block_num; i++, ptr_tmp += AES128_KEY_LENGTH)
    {
        aes128_sw_decipher(round_key, ptr_tmp);
    }
}

/* Interrupt Handlers*/

/* ----------------------------------------------------------------------------
 * Function      : void DMA_IRQ_FUNC(ASRC_IN_IDX)
 * ----------------------------------------------------------------------------
 * Description   : ASRC input DMA interrupt handler.
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DMA_IRQ_FUNC(ASRC_IN_IDX) (void)
{
    Sys_GPIO_Toggle(7);   /* debug: ch3 ASRC-in complete */

    /* Stop the ASRC if the intended number of samples has been routed into it */
    Sys_ASRC_StatusConfig(ASRC_DISABLE);
    Sys_DMA_ClearChannelStatus(ASRC_IN_IDX);
}

#if (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT)
/* ----------------------------------------------------------------------------
 * Function      : void DMA_IRQ_FUNC(PCM_DMA_NUM)
 * ----------------------------------------------------------------------------
 * Description   : PCM TX DMA complete. The single buffer finished streaming;
 *                 re-arm it (full reconfig reloads the LIN transfer counter).
 *                 This is the documented LIN + complete-interrupt re-arm loop,
 *                 not circular DMA.
 * ------------------------------------------------------------------------- */
void DMA_IRQ_FUNC(PCM_DMA_NUM) (void)
{
    Sys_GPIO_Toggle(9);   /* debug: ch5 PCM complete */

    Sys_DMA_ChannelConfig(PCM_DMA_NUM, RX_DMA_PCM_STEREO, PCM_FRAME_WORDS, 0,
                          (uint32_t)&pcm_tx_buf[0], (uint32_t)&PCM->TX_DATA);
    Sys_DMA_ClearChannelStatus(PCM_DMA_NUM);
    Sys_DMA_ChannelEnable(PCM_DMA_NUM);
}

/* ----------------------------------------------------------------------------
 * Function      : void DMA_IRQ_FUNC(ASRC_OUT_IDX)
 * ----------------------------------------------------------------------------
 * Description   : ASRC output DMA complete. ch4 read ASRC->OUT (16-bit, 120
 *                 samples per 10 ms at 12k) into pcm_tx_buf. Zero the high 16
 *                 bits of each word (PCM uses only TX_DATA low 16), then re-arm.
 * ------------------------------------------------------------------------- */
void DMA_IRQ_FUNC(ASRC_OUT_IDX) (void)
{
    Sys_GPIO_Toggle(8);   /* debug: ch4 ASRC->PCM complete */

    /* Re-arm ch4 to refill pcm_tx_buf. Each 32-bit word = two 16-bit samples
       (ASRC->OUT); the PCM uses only TX_DATA low 16, so it naturally takes
       one sample per word. */
    Sys_DMA_ChannelConfig(ASRC_OUT_IDX, RX_DMA_ASRC_OUT, PCM_FRAME_WORDS, 0,
                          (uint32_t)&ASRC->OUT, (uint32_t)&pcm_tx_buf[0]);
    Sys_DMA_ClearChannelStatus(ASRC_OUT_IDX);
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);
}
#endif    /* if (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT) */

/* ----------------------------------------------------------------------------
 * Function      : void AUDIOSINK_PHASE_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : ASCC phase interrupt handler.
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void AUDIOSINK_PHASE_IRQHandler(void)
{
    if (AUDIOSINK_CTRL->PHASE_CNT_MISSED_STATUS_ALIAS)
    {
        /* Set flag to indicate one of the phase count value was missed */
        phase_cnt_missed = true;
    }
    else
    {
        /* Get audio sink phase count. */
        static int64_t audio_sink_phase_cnt;
        static int64_t audio_sink_phase_cnt_prev = 0;
        audio_sink_phase_cnt = Sys_Audiosink_PhaseCounter();

        /* Check if phase count variables are consequent values */
        if (phase_cnt_missed == false)
        {
            /* Get audio sink count. */
            audio_sink_cnt  = Sys_Audiosink_Counter() << SHIFT_BIT;
            audio_sink_cnt +=
                ((((audio_sink_phase_cnt_prev - audio_sink_phase_cnt))
                  << SHIFT_BIT) / audio_sink_period_cnt);
        }

        /* Store audio sink count phase for the next time. */
        audio_sink_phase_cnt_prev = audio_sink_phase_cnt;
        phase_cnt_missed = false;
    }

    /* Set flag to indicate ASCC phase interrupt was triggered*/
    flag_ascc_phase = true;

    /* Restart phase count sampling for the next cycle */
    AUDIOSINK_CTRL->CNT_RESET_ALIAS = CNT_RESET_BITBAND;
    AUDIOSINK->PHASE_CNT = 0;
    AUDIOSINK_CTRL->PHASE_CNT_START_ALIAS = PHASE_CNT_START_BITBAND;
}

/* ----------------------------------------------------------------------------
 * Function      : void AUDIOSINK_PERIOD_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : ASCC period interrupt handler.
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void AUDIOSINK_PERIOD_IRQHandler(void)
{
    audio_sink_period_cnt = Sys_Audiosink_PeriodCounter() /
                            (AUDIOSINK->CFG + 1);

    /* Restart period count sampling for the next cycle */
    AUDIOSINK->PERIOD_CNT = 0;
    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = PERIOD_CNT_START_BITBAND;
}

/* ----------------------------------------------------------------------------
 * Function      : void DSP0_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : DSP0 interrupt handler.
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DSP0_IRQHandler(void)
{
    Sys_GPIO_Toggle(6);   /* debug: DSP0 decode-done IRQ rate */

    if (lpdsp32.state == DSP_STARTING)
    {
        lpdsp32.state = DSP_IDLE;
        return;
    }

    if (flag_ascc_phase)
    {
        ASRC_Reconfig();
        flag_ascc_phase = false;
    }

    if (frame_idx < ENCODED_FRAME_LENGTH)
    {
        uint8_t subframe_avoid;
        Sys_Timers_Stop(1 << TIMER_REGUL);

        /* To avoid radio transactions. */
        if (ear_side == RM_RIGHT)
        {
            subframe_avoid = 10;
        }
        else
        {
            subframe_avoid = 8;
        }

        if (frame_idx == subframe_avoid * ENCODED_SUBFRAME_LENGTH)
        {
            Sys_Timer_Set_Control(TIMER_REGUL, TIMER_SHOT_MODE |
                                  (1000 - 1) |
                                  TIMER_SLOWCLK_DIV2);
        }
        else
        {
            Sys_Timer_Set_Control(TIMER_REGUL, TIMER_SHOT_MODE |
                                  (200 - 1) |
                                  TIMER_SLOWCLK_DIV2);
        }
        Sys_Timers_Start(1 << TIMER_REGUL);
    }
    else if (frame_idx == ENCODED_FRAME_LENGTH)
    {
        frame_decoded = true;
    }

    /* Assert SPI_CS. */
    SPI0_CTRL1->SPI0_CS_ALIAS = SPI0_CS_0_BITBAND;

    /* Assert SPI_CS. */
    SPI0_CTRL1->SPI0_CS_ALIAS = SPI0_CS_0_BITBAND;
#if 0
    /* Test: overwrite Buffer.output with the 1 kHz sine (8 samples/subframe),
       keeping the official ch3 source (Buffer.output) and DSP0 enable feed.
       ch3 reads SRC_WORD_SIZE_32 and unpacks 32->16, so pack two samples per
       int32 word (low = first, high = second) and write 4 words = 16 bytes
       = 8 samples, exactly what the DMA consumes each subframe. */
    {
        static uint16_t pcm_asrc_sine_pos = 0;
        int32_t *out = (int32_t *)Buffer.output;
        for (uint8_t i = 0; i < (SUBFRAME_LENGTH / 2); i++)
        {
            uint16_t a = (uint16_t)pcm_asrc_sine_16k[(pcm_asrc_sine_pos + 2 * i) % FRAME_LENGTH];
            uint16_t b = (uint16_t)pcm_asrc_sine_16k[(pcm_asrc_sine_pos + 2 * i + 1) % FRAME_LENGTH];
            out[i] = (uint32_t)a | ((uint32_t)b << 16);
        }
        pcm_asrc_sine_pos += SUBFRAME_LENGTH;
        if (pcm_asrc_sine_pos >= FRAME_LENGTH)
        {
            pcm_asrc_sine_pos = 0;
        }
    }
#endif
    /* Clear DMA channel status. */
    Sys_DMA_ClearChannelStatus(ASRC_IN_IDX);

    /* Re-enable DMA for ASRC input. */
    Sys_DMA_ChannelEnable(ASRC_IN_IDX);

    /* Enable ASRC block. */
    Sys_ASRC_StatusConfig(ASRC_ENABLE);

    lpdsp32.state = DSP_IDLE;
}

/* ----------------------------------------------------------------------------
 * Function      : void TIMER_IRQ_FUNC(TIMER_REGUL)(void)
 * ----------------------------------------------------------------------------
 * Description   : Regulate flow of data into LPDSP32 for decoding.
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void TIMER_IRQ_FUNC(TIMER_REGUL)(void)
{
    if (frame_idx < ENCODED_FRAME_LENGTH)
    {
        LPDSP32_Start_DEC(&frame_in[frame_idx]);
        frame_idx += ENCODED_SUBFRAME_LENGTH;
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void DIO0_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : Toggle selection for left or right channel [receiver]
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DIO0_IRQHandler(void)
{
    static uint8_t ignore_next_dio_int = 0;

    if (ignore_next_dio_int == 1)
    {
        ignore_next_dio_int = 0;
    }
    else if (DIO_DATA->ALIAS[BUTTON_DIO] == 0)
    {
        /* Button is pressed: Ignore next interrupt.
         * This is required to deal with the de-bounce circuit limitations. */
        ignore_next_dio_int = 1;
        ear_side = (ear_side == RM_RIGHT) ? RM_LEFT : RM_RIGHT;
        APP_RM_Init(ear_side);
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void Receiver_Asrc_reconfig(void)
 * ----------------------------------------------------------------------------
 * Description   : Configure ASRC (RX)
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void ASRC_Reconfig(void)
{
    /* Get ASRC output count */
    int64_t Cr = FRAME_LENGTH << SHIFT_BIT;   /* 16k input frame = 160<<20 */

    /* DEC_MODE2: 16k -> 12k downsampling (verified reference config).
       phase_inc = 2^28 * (f_src - f_sink) / f_sink. */
    int64_t Ck = audio_sink_cnt;
    int64_t asrc_inc_carrier = ((((Cr - Ck) << 28) / Ck) << 0);
    asrc_inc_carrier &= 0xFFFFFFFF;
    Sys_ASRC_Config(asrc_inc_carrier, WIDE_BAND | ASRC_DEC_MODE2);
}

/* ----------------------------------------------------------------------------
 * Function      : void Renderer(uint8_t * src_addr)
 * ----------------------------------------------------------------------------
 * Description   : Send the received data to the decoder
 * Inputs        : src_addr Pointer to data received over remote MIC protocol
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Renderer(uint8_t *src_addr)
{
    frame_in = src_addr;

    /* De-assert SPI_CS */
    SPI0_CTRL1->SPI0_CS_ALIAS = SPI0_CS_1_BITBAND;

    /* Call the decoder for the first sub-frame in a received full frame */
    LPDSP32_Start_DEC(&frame_in[0]);
    frame_idx     = ENCODED_SUBFRAME_LENGTH;
    frame_decoded = false;
}

/* ----------------------------------------------------------------------------
 * Function      : void Start_Dec_Lpdsp32(uint8_t * src_addr)
 * ----------------------------------------------------------------------------
 * Description   : Issue a decode start command to LPDSD32 (TX/RX)
 * Inputs        : src_addr - source address of LPDSP32 input
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void LPDSP32_Start_DEC(uint8_t *src_addr)
{
    memcpy(Buffer.input, src_addr, ENCODED_SUBFRAME_LENGTH);
    codecSetParameters(lpdsp32.codec, &lpdsp32.channels);

    lpdsp32.state = DSP_BUSY;
    codecDecode(lpdsp32.codec);

    /* turn off the DECODE_RESET if it is set */
    lpdsp32.channels.action &= ~DECODE_RESET;
}

/* ----------------------------------------------------------------------------
 * Function      : void app_process_incoming_data
 *                (uint8_t *data, uint8_t length)
 * ----------------------------------------------------------------------------
 * Description   : Process incoming data from the radio application layer
 * Inputs        : data - pointer to the input buffer
 *               : length - length of data in the buffer
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void App_Process_Incoming_Data(uint8_t *data, uint8_t length)
{
    static uint8_t outTempBuff[ENCODED_FRAME_LENGTH];

#if (CODEC_CONFIG == CODEC_CONFIG_CELT)
    if(lpdsp32.state == DSP_STARTING)
    {
        return;
    }
#endif

    if (length == 0)
    {
        /* The packet loss concealment for G722 should be applied as TX
         * hasn't sent data.
         */
        memset(data, 0xaa, ((app_env.rm_param.audio_rate *
                                    app_env.rm_param.interval_time) / 8000));
    }
    else
    {
        memcpy(outTempBuff, data, length);
#if CRY_AES_128_ECB
        Decry_AES_128(data, (uint8_t)(ENCODED_FRAME_LENGTH / AES128_KEY_LENGTH));
#endif    /* if CRY_AES_128_ECB */
        Renderer(outTempBuff);
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void app_process_link_disconnected(void)
 * ----------------------------------------------------------------------------
 * Description   : Disconnection notification from radio application layer
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void App_Process_Link_Disconnected(void)
{
    /* Stop audio transmission to avoid repeatedly processing same audio data */
    NVIC_DisableIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_DisableIRQ(AUDIOSINK_PERIOD_IRQn);

    /* Disable ASRC IN DMA interrupts */
    NVIC_DisableIRQ(DMA_IRQn(ASRC_IN_IDX));

    /* Disable LPDSP32 interrupt */
    NVIC_DisableIRQ(DSP0_IRQn);

    /* Disable Timer interrupt */
    NVIC_DisableIRQ(TIMER_IRQn(TIMER_REGUL));
    Sys_Timers_Stop(1 << TIMER_REGUL);

    /* Reset content of the output buffer to prevent the last packet over-run */
    ClearBufferOut();
}

/* ----------------------------------------------------------------------------
 * Function      : void app_process_link_connected(void)
 * ----------------------------------------------------------------------------
 * Description   : Connection notification from radio application layer
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void App_Process_Connected(void)
{
    /* Start audio transmission */
    audio_sink_cnt  = 0;
    flag_ascc_phase = false;

    Sys_ASRC_Reset();

    /* ASCC interrupts */
    NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

    /* Enable ASRC IN DMA interrupts */
    NVIC_EnableIRQ(DMA_IRQn(ASRC_IN_IDX));

#if (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT)
    /* Start the ASRC->PCM path. ch4 (ASRC_OUT -> pcm_tx_buf, 32-bit = two
       16-bit samples/word) drains the 24k ASRC output; ch5 streams pcm_tx_buf
       to the PCM, which reads the low 16 bits -> the 12k host rate. PCM
       enabled last. */
    NVIC_SetPriority(DMA_IRQn(ASRC_OUT_IDX), 3);
    NVIC_ClearPendingIRQ(DMA_IRQn(ASRC_OUT_IDX));
    NVIC_EnableIRQ(DMA_IRQn(ASRC_OUT_IDX));
    Sys_DMA_ClearChannelStatus(ASRC_OUT_IDX);
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);

    NVIC_SetPriority(DMA_IRQn(PCM_DMA_NUM), 3);
    NVIC_ClearPendingIRQ(DMA_IRQn(PCM_DMA_NUM));
    NVIC_EnableIRQ(DMA_IRQn(PCM_DMA_NUM));
    Sys_DMA_ClearChannelStatus(PCM_DMA_NUM);
    Sys_DMA_ChannelEnable(PCM_DMA_NUM);
    Sys_PCM_Enable();   /* enable PCM last */
#endif    /* if (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT) */

    /* Enable LPDSP32 interrupt */
    NVIC_EnableIRQ(DSP0_IRQn);

    /* Enable Timer interrupt */
    NVIC_EnableIRQ(TIMER_IRQn(TIMER_REGUL));
#if CRY_AES_128_ECB
    uint32_t key[4] = KEY_AES_128_ECB;
    memcpy(round_key, key, sizeof(uint32_t) * 4);

    /* any time the key is updated */
    aes128_key_expand(round_key);
#endif    /* if CRY_AES_128_ECB */
}
