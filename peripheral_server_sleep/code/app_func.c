#if 0
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
 * - Audio pipeline from remote_mic_rx_coex (SPI_TX_RAW_OUTPUT)
 * ------------------------------------------------------------------------- */

#include "app.h"
#include "queue.h"
#include <stdio.h>

uint8_t ear_side = APP_RM_AUDIO_CHANNEL;

int16_t *Cm2DspBuff0enc = (int16_t *)MEM_CM2DSP_ADDR0_ENC;
int16_t *Cm2DspBuff1enc = (int16_t *)MEM_CM2DSP_ADDR1_ENC;
uint8_t *Dsp2CmBuff0enc = MEM_DSP2CM_ADDR0_ENC;
uint8_t *Dsp2CmBuff1enc = MEM_DSP2CM_ADDR1_ENC;
uint8_t *Cm2DspBuff0dec = MEM_CM2DSP_ADDR0_DEC;
uint8_t *Cm2DspBuff1dec = MEM_CM2DSP_ADDR1_DEC;
uint8_t *Dsp2CmBuff0dec = MEM_DSP2CM_ADDR0_DEC;
uint8_t *Dsp2CmBuff1dec = MEM_DSP2CM_ADDR1_DEC;

int64_t Cr = 0;
int64_t Ck = 0;
int64_t asrc_out_cnt              = 0;
int64_t audio_sink_cnt            = 0;
int64_t avg_ck_outputcnt          = 0;
int64_t diff_ck_outputcnt         = 0;
int64_t audio_sink_period_cnt     = 0;
int64_t audio_sink_phase_cnt      = 0;
int64_t audio_sink_phase_cnt_prev = 0;
int64_t asrc_inc_carrier;
uint32_t asrc_cnt_prev;
uint32_t asrc_cnt_cnst             = 0;
bool flag_ascc_phase           = false;

/* ISR aliases — map hardware IRQ names to readable function names */
void __attribute__ ((alias("Asrc_in_dma_isr")))
DMA_IRQ_FUNC(ASRC_IN_IDX)(void);

void __attribute__ ((alias("Ascc_phase_isr")))
AUDIOSINK_PHASE_IRQHandler(void);

void __attribute__ ((alias("Ascc_period_isr")))
AUDIOSINK_PERIOD_IRQHandler(void);

void __attribute__ ((alias("DspDec_isr"))) DSP1_IRQHandler(void);

void __attribute__ ((alias("Packet_regulator_timer_isr")))
TIMER_IRQ_FUNC(TIMER_REGUL)(void);

uint8_t frame_idx        = 0;
bool frame_decoded    = true;
bool asrc_stable      = false;
uint32_t cntr_stability   = 0;
int64_t Ck_prev = FRAME_LENGTH << SHIFT_BIT;
bool phase_cnt_missed = false;
uint8_t *frame_in;

/* ----------------------------------------------------------------------------
 * Function      : void Packet_regulator_timer_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : Regulator timer interrupt — feeds subframes to LPDSP32.
 * ------------------------------------------------------------------------- */
void Packet_regulator_timer_isr(void)
{
    Start_Dec_Lpdsp32(&frame_in[frame_idx]);
    frame_idx += ENCODED_SUBFRAME_LENGTH;
}

/* ----------------------------------------------------------------------------
 * Function      : void Asrc_reconfig(void)
 * ----------------------------------------------------------------------------
 * Description   : Reconfigure ASRC based on audio clock drift.
 * ------------------------------------------------------------------------- */
void Asrc_reconfig(void)
{
    asrc_out_cnt = Sys_ASRC_OutputCount();
    Sys_ASRC_ResetOutputCount();

    diff_ck_outputcnt = Ck - (asrc_out_cnt << SHIFT_BIT);
    avg_ck_outputcnt  = ((diff_ck_outputcnt - avg_ck_outputcnt) >> 10) +
                        avg_ck_outputcnt;

    Cr = FRAME_LENGTH << SHIFT_BIT;
    Ck = audio_sink_cnt;

    if ((Ck <= (FRAME_LENGTH - ASRC_CFG_THR) << SHIFT_BIT) ||
        (Ck >= (FRAME_LENGTH + ASRC_CFG_THR) << SHIFT_BIT))
    {
        avg_ck_outputcnt = 0;
    }
    else
    {
        if (!asrc_stable)
        {
            cntr_stability++;
            if (cntr_stability > STABLE_THR)
            {
                asrc_stable = true;
            }
        }
    }

    if (asrc_cnt_prev == ASRC->PHASE_CNT)
    {
        asrc_cnt_cnst++;
        if (asrc_cnt_cnst >= 20)
        {
            Sys_ASRC_Reset();
            asrc_cnt_cnst = 0;
        }
    }
    else
    {
        asrc_cnt_cnst = 0;
    }

    Ck_prev = Ck;

    if (Ck != 0)
    {
        asrc_inc_carrier  = ((((Cr - Ck) << 29) / Ck) << 0);
        asrc_inc_carrier &= 0xFFFFFFFF;
        Sys_ASRC_Config(asrc_inc_carrier, WIDE_BAND | ASRC_DEC_MODE1);
    }
    asrc_cnt_prev = ASRC->PHASE_CNT;
}

/* ----------------------------------------------------------------------------
 * Function      : void DspDec_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : LPDSP32 decoder interrupt handler.
 * ------------------------------------------------------------------------- */
void DspDec_isr(void)
{
    if (flag_ascc_phase)
    {
        Asrc_reconfig();
        flag_ascc_phase = false;
    }

    if (frame_idx < ENCODED_FRAME_LENGTH)
    {
        uint8_t subframe_avoid;

        Sys_Timers_Stop(1 << TIMER_REGUL);

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

    SPI0_CTRL1->SPI0_CS_ALIAS = SPI0_CS_0_BITBAND;

    Sys_DMA_ClearChannelStatus(ASRC_IN_IDX);
    Sys_DMA_ChannelEnable(ASRC_IN_IDX);

    Sys_ASRC_StatusConfig(ASRC_ENABLE);
}

/* ----------------------------------------------------------------------------
 * Function      : void Ascc_phase_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : ASCC phase interrupt handler.
 * ------------------------------------------------------------------------- */
void Ascc_phase_isr(void)
{
    if (AUDIOSINK_CTRL->PHASE_CNT_MISSED_STATUS_ALIAS)
    {
        phase_cnt_missed = true;
    }
    else
    {
        audio_sink_phase_cnt = Sys_Audiosink_PhaseCounter();
        if (!phase_cnt_missed && audio_sink_period_cnt != 0)
        {
            audio_sink_cnt  = Sys_Audiosink_Counter() << SHIFT_BIT;
            audio_sink_cnt +=
                ((((audio_sink_phase_cnt_prev - audio_sink_phase_cnt))
                  << SHIFT_BIT) / audio_sink_period_cnt);
        }

        audio_sink_phase_cnt_prev = audio_sink_phase_cnt;
        phase_cnt_missed = false;
    }
    flag_ascc_phase = true;

    AUDIOSINK_CTRL->CNT_RESET_ALIAS = CNT_RESET_BITBAND;
    AUDIOSINK->PHASE_CNT = 0;
    AUDIOSINK_CTRL->PHASE_CNT_START_ALIAS = PHASE_CNT_START_BITBAND;
}

/* ----------------------------------------------------------------------------
 * Function      : void Ascc_period_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : ASCC period interrupt — audio measurement or RC OSC.
 * ------------------------------------------------------------------------- */
void Ascc_period_isr(void)
{
    if (app_env.audio_streaming)
    {
        /* Audio clock measurement for ASRC */
        audio_sink_period_cnt = Sys_Audiosink_PeriodCounter() /
                                (AUDIOSINK->CFG + 1);
    }
    else
    {
        /* RC oscillator measurement (original project code) */
        static uint32_t num_measurement = LOW_POWER_CLK_INITIAL_MEASUREMENT;
        static uint32_t audiosink_period = 0;
        static uint32_t audiosink_period_cnt = 0;
        static uint32_t audiosink_period_sum = 0;
        float average_period;
        uint8_t i;
        float measure_buf[5];
        static uint8_t buf_cnt = 0;

        audiosink_period = Sys_Audiosink_PeriodCounter();
        audiosink_period_cnt++;
        audiosink_period_sum += audiosink_period;

#if (LOW_POWER_CLK_UPDATE == LOW_POWER_CLK_UPDATE_DISABLE)
        if (low_power_clk_param.dynamic_measurement_enable == false)
#endif
        {
            if (audiosink_period_cnt == num_measurement)
            {
                average_period = (audiosink_period_sum /
                                 (audiosink_period_cnt * LOW_POWER_CLK_SCALE_AVERAGE_PERIOD));
                audiosink_period_cnt = 0;
                audiosink_period_sum = 0;

                if (low_power_clk_param.dynamic_measurement_enable == false)
                {
                    measure_buf[buf_cnt] = average_period;
                    buf_cnt = ((buf_cnt + 1) % 5);
                    for (i = 0; i < 5; i++)
                    {
                        measure_buf[i] = average_period;
                    }
                }
                else
                {
                    measure_buf[buf_cnt] = average_period;
                    buf_cnt = ((buf_cnt + 1) % 5);

                    float max = measure_buf[0];
                    float min = measure_buf[0];
                    for (i = 1; i < 5; i++)
                    {
                        if (measure_buf[i] > max) max = measure_buf[i];
                        else if (measure_buf[i] < min) min = measure_buf[i];
                    }

                    average_period = 0;
                    for (i = 0; i < 5; i++)
                    {
                        average_period = (average_period + measure_buf[i]);
                    }
                    average_period = (average_period - min - max);
                    average_period = (average_period / (5 - 2));
                }

                NVIC_DisableIRQ(AUDIOSINK_PERIOD_IRQn);

                if (RTC_CLK_SRC == RTC_CLK_SRC_RC_OSC)
                {
                    RTCCLK_Period_Value_Set(average_period * 1.00035);
                }
                else
                {
                    RTCCLK_Period_Value_Set(average_period);
                }

                low_power_clk_param.low_power_enable = true;
#if (LOW_POWER_CLK_UPDATE == LOW_POWER_CLK_UPDATE_DISABLE)
                low_power_clk_param.dynamic_measurement_enable = true;
#endif
            }
        }
    }

    AUDIOSINK->PERIOD_CNT = 0;
    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = PERIOD_CNT_START_BITBAND;
}

/* ----------------------------------------------------------------------------
 * Function      : void Rendering_func(uint8_t * src_addr)
 * ----------------------------------------------------------------------------
 * Description   : Send received data to the decoder.
 * ------------------------------------------------------------------------- */
void Rendering_func(uint8_t *src_addr)
{
    frame_in = src_addr;

    /* De-assert SPI_CS */
    SPI0_CTRL1->SPI0_CS_ALIAS = SPI0_CS_1_BITBAND;

    /* Call the decoder for the first sub-frame */
    Start_Dec_Lpdsp32(&frame_in[0]);
    frame_idx     = ENCODED_SUBFRAME_LENGTH;
    frame_decoded = false;
}

/* ----------------------------------------------------------------------------
 * Function      : void Start_Dec_Lpdsp32(uint8_t * src_addr)
 * ----------------------------------------------------------------------------
 * Description   : Issue a decode start command to LPDSP32.
 * ------------------------------------------------------------------------- */
void Start_Dec_Lpdsp32(uint8_t *src_addr)
{
    PRINTF("S1\r\n");
    memcpy(Cm2DspBuff0dec, src_addr,
           ENCODED_SUBFRAME_LENGTH * sizeof(uint8_t));
    PRINTF("S2\r\n");

    /* Decoding command */
    SYSCTRL_DSS_CMD->DSS_CMD_1_ALIAS = DSS_CMD_1_BITBAND;
    PRINTF("S3\r\n");
}

/* ----------------------------------------------------------------------------
 * Function      : void Asrc_in_dma_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : ASRC input DMA interrupt handler.
 * ------------------------------------------------------------------------- */
void Asrc_in_dma_isr(void)
{
    /* Stop ASRC if complete frame has been handled */
    Sys_ASRC_StatusConfig(ASRC_DISABLE);
}
#else
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
 * $Revision: 1.10 $
 * $Date: 2018/02/27 15:46:06 $
 * ------------------------------------------------------------------------- */

#include "app.h"
#include "queue.h"
#include <stdio.h>
#if 1
uint8_t ear_side = APP_RM_AUDIO_CHANNEL;


int16_t *Cm2DspBuff0enc = (int16_t *)MEM_CM2DSP_ADDR0_ENC;
int16_t *Cm2DspBuff1enc = (int16_t *)MEM_CM2DSP_ADDR1_ENC;
uint8_t *Dsp2CmBuff0enc = MEM_DSP2CM_ADDR0_ENC;
uint8_t *Dsp2CmBuff1enc = MEM_DSP2CM_ADDR1_ENC;
uint8_t *Cm2DspBuff0dec = MEM_CM2DSP_ADDR0_DEC;
uint8_t *Cm2DspBuff1dec = MEM_CM2DSP_ADDR1_DEC;
uint8_t *Dsp2CmBuff0dec = MEM_DSP2CM_ADDR0_DEC;
uint8_t *Dsp2CmBuff1dec = MEM_DSP2CM_ADDR1_DEC;

int64_t Cr = 0;
int64_t Ck = 0;
int64_t asrc_out_cnt              = 0;
int64_t audio_sink_cnt            = 0;
int64_t avg_ck_outputcnt          = 0;
int64_t diff_ck_outputcnt         = 0;
int64_t audio_sink_period_cnt     = 0;
int64_t audio_sink_phase_cnt      = 0;
int64_t audio_sink_phase_cnt_prev = 0;
int64_t asrc_inc_carrier;
uint32_t asrc_cnt_prev;
uint32_t asrc_cnt_cnst             = 0;
bool flag_ascc_phase           = false;

int16_t sample_in[FRAME_LENGTH]    = {
#if 1
    0,      16,     31,     47,     63,     78,
    93,
    109,
    124,    138,
    153,    167,    182,    195,    209,    222,
    235,    248,    260,    272,    283,    294,
    304,
    314,
    324,    333,
    341,    349,    356,    363,
    370,    375,    380,    385,    389,    392,
    395,
    397,
    399,    400,
    400,    400,    399,    397,
    395,    392,    389,    385,    380,    375,
    370,
    363,
    356,    349,
    341,    333,    324,    314,
    304,    294,    283,    272,    260,    248,
    235,
    222,
    209,    195,
    182,    167,    153,    138,
    124,    109,    93,     78,     63,     47,
    31,
    16,
    0,      -16,
    -31,    -47,    -63,    -78,    -93,    -109,
    -124,   -138,   -153,   -167,   -182,   -195,
    -209,
    -222,
    -235,
    -248,   -260,   -272,
    -283,   -294,   -304,   -314,   -324,   -333,
    -341,
    -349,
    -356,
    -363,   -370,   -375,
    -380,   -385,   -389,   -392,   -395,   -397,
    -399,
    -400,
    -400,
    -400,   -399,   -397,
    -395,   -392,   -389,   -385,   -380,   -375,
    -370,
    -363,
    -356,
    -349,   -341,   -333,
    -324,   -314,   -304,   -294,   -283,   -272,
    -260,
    -248,
    -235,
    -222,   -209,   -195,
    -182,   -167,   -153,   -138,   -124,   -109,
    -93,
    -78,
    -63,    -47,
    -31,    -16,
#else    /* if 1 */
    0,      168,    364,    85,     -605,   -959,
    -335,
    920,
    1583,   742,
    -1088,  -2200,  -1288,
    1092,   2774,   1951,   -921,   -3269,  -2704,
    570,
    3652,
    3514,   -39,
    -3893,
    -4347,  -662,   3965,   5164,   1517,   -3849,
    -5928,
    -2505,
    3529,
    6599,   3598,
    -2997,  -7141,  -4762,  2252,   7518,   5961,
    -1301,
    -7699,
    -7153,  156,
    7658,
    8296,   1162,   -7375,  -9347,  -2624,  6836,
    10261,
    4197,
    -6035,
    -10997,
    -5841,  4972,   11517,  7514,   -3658,  -11787,
    -9166,
    2109,
    11776,
    10749,
    -350,   -11464, -12212, -1584,  10834,  13505,
    3655,
    -9881,
    -14580,
    -5817,
    8605,   15393,  8018,   -7017,  -15902, -10204,
    5137,
    16075,
    12318,
    -2993,
    -15883, -14300, 623,    15309,  16092,  1929,
    -14342,
    -17638,
    -4611,
    12982,
    18885,  7364,   -11238, -19784, -10127, 9131,
    20294,
    12834,
    -6690,
    -20381,
    -15415, 3954,   20019,  17805,  -973,   -19193,
    -19936,
    -2196,
    17898,
    21745,
    5491,   -16139, -23174, -8839,  13935,  24171,
    12169,
    -11314,
    -24693,
    -15401, 8316,   24705,  18459,  -4992,  -24183,
    -21264,
    1401,
    23114,
    23743,
    2386,   -21500, -25825, -6294,  19351,  27447,
    10241,
    -16693,
    -28553,
    -14141, 13565,  29097,  17906,  -10015, -29045,
    -21446,
    6105,
    28373,
    24676,
    -1907,  -27073, -27512, -2498,  25148,  29877,
#endif    /* if 1 */
};



void __attribute__ ((alias("Asrc_in_dma_isr")))
DMA_IRQ_FUNC(ASRC_IN_IDX)(void);

void __attribute__ ((alias("Ascc_phase_isr")))
AUDIOSINK_PHASE_IRQHandler(void);

void __attribute__ ((alias("Ascc_period_isr")))
AUDIOSINK_PERIOD_IRQHandler(void);

void __attribute__ ((alias("DspDec_isr"))) DSP1_IRQHandler(void);

void __attribute__ ((alias("Packet_regulator_timer_isr")))
TIMER_IRQ_FUNC(TIMER_REGUL)(void);


int16_t cailleftbuf[64]={0,};
int16_t cailrightbuf[64]={0,};
#if 0
void DMA_IRQ_FUNC(RX_DMA_NUM)(void)
{
	uint8_t i;
	/* Pack the incoming DMIC data into left and right buffers */
	for (i = 0; i < 64; i++)
	{
		cailleftbuf[i]  = dmic_buffer_in[i] & 0xFFFF;
		cailrightbuf[i] = dmic_buffer_in[i] >> 16;
	}

    Sys_DMA_ClearChannelStatus(RX_DMA_NUM);
}
#endif

uint8_t frame_idx        = 0;
bool frame_decoded    = true;
bool asrc_stable      = false;
uint32_t cntr_stability   = 0;
int64_t Ck_prev = FRAME_LENGTH << SHIFT_BIT;
bool phase_cnt_missed = false;
uint8_t *frame_in;

/* ----------------------------------------------------------------------------
 * Function      : void Packet_regulator_timer_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : regulator timer interrupt handler
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Packet_regulator_timer_isr(void)
{
    /* Continue transfer to LPDSP if not complete otherwise set flag */
    Start_Dec_Lpdsp32(&frame_in[frame_idx]);
    frame_idx += ENCODED_SUBFRAME_LENGTH;
}

/* ----------------------------------------------------------------------------
 * Function      : void Asrc_reconfig(void)
 * ----------------------------------------------------------------------------
 * Description   : Configure ASRC (TX/RX)
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Asrc_reconfig(void)
{
    /* Get ASRC output count */
    asrc_out_cnt = Sys_ASRC_OutputCount();
    Sys_ASRC_ResetOutputCount();

    diff_ck_outputcnt = Ck - (asrc_out_cnt << SHIFT_BIT);
    avg_ck_outputcnt  = ((diff_ck_outputcnt - avg_ck_outputcnt) >> 10) +
                        avg_ck_outputcnt;

    Cr = FRAME_LENGTH << SHIFT_BIT;
    Ck = audio_sink_cnt;

    if ((Ck <= (FRAME_LENGTH - ASRC_CFG_THR) << SHIFT_BIT) ||
        (Ck >= (FRAME_LENGTH + ASRC_CFG_THR) << SHIFT_BIT))
    {
        avg_ck_outputcnt = 0;
    }
    else
    {
        if (!asrc_stable)
        {
            cntr_stability++;
            if (cntr_stability > STABLE_THR)
            {
                asrc_stable = true;
            }
        }
    }

    /* Reset ASRC if the behavior is unexpected */
    if (asrc_cnt_prev == ASRC->PHASE_CNT)
    {
        asrc_cnt_cnst++;
        if (asrc_cnt_cnst >= 20)
        {
            Sys_ASRC_Reset();
            asrc_cnt_cnst = 0;
        }
    }
    else
    {
        asrc_cnt_cnst = 0;
    }

    /* store Ck to apply on the next packet if the audio sink value is out of
     * range
     */
    Ck_prev = Ck;

    /* Configure ASRC base on new Ck */
    asrc_inc_carrier  = ((((Cr - Ck) << 29) / Ck) << 0);
    asrc_inc_carrier &= 0xFFFFFFFF;
    Sys_ASRC_Config(asrc_inc_carrier, WIDE_BAND | ASRC_DEC_MODE1);
    asrc_cnt_prev     = ASRC->PHASE_CNT;
}

/* ----------------------------------------------------------------------------
 * Function      : void DspDec_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : LPDSP32 decoder interrupt handler (RX)
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DspDec_isr(void)
{
    if (flag_ascc_phase)
    {
        Asrc_reconfig();
        flag_ascc_phase = false;
    }

    /* Uncomment these three lines if you need a predefined signal to generate
     * for debug */

    /* static uint8_t cntr = 0;
     * memcpy(Dsp2CmBuff0dec, &sample_in[cntr], SUBFRAME_LENGTH*sizeof(int16_t));
     * cntr = (cntr + SUBFRAME_LENGTH) % (160); */

    if (frame_idx < ENCODED_FRAME_LENGTH)
    {
        uint8_t subframe_avoid;

        Sys_Timers_Stop(1 << TIMER_REGUL);

        /* To avoid radio transactions */

        /*TODO: this setting is not general yet and only work for SUBFRAME= 8 */
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
            Sys_Timer_Set_Control(TIMER_REGUL, TIMER_SHOT_MODE | \
                                  (1000 - 1) |                   \
                                  TIMER_SLOWCLK_DIV2);
        }
        else
        {
            Sys_Timer_Set_Control(TIMER_REGUL, TIMER_SHOT_MODE | \
                                  (200 - 1) |                    \
                                  TIMER_SLOWCLK_DIV2);
        }
        Sys_Timers_Start(1 << TIMER_REGUL);
    }
    else if (frame_idx == ENCODED_FRAME_LENGTH)
    {
        frame_decoded = true;
    }

    /* Assert SPI_CS */
    SPI0_CTRL1->SPI0_CS_ALIAS = SPI0_CS_0_BITBAND;

    /* Clear DMA channel status */
    Sys_DMA_ClearChannelStatus(ASRC_IN_IDX);

    /* Re-enable DMA for ASRC input */
    Sys_DMA_ChannelEnable(ASRC_IN_IDX);

    /* Enable ASRC block */
    Sys_ASRC_StatusConfig(ASRC_ENABLE);
}

/* ----------------------------------------------------------------------------
 * Function      : void Ascc_phase_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : ASCC phase interrupt handler (TX/RX)
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Ascc_phase_isr(void)
{

    if (AUDIOSINK_CTRL->PHASE_CNT_MISSED_STATUS_ALIAS)
    {
        phase_cnt_missed = true;
    }
    else
    {
        /* Get audio sink phase count */
        audio_sink_phase_cnt = Sys_Audiosink_PhaseCounter();
        if (!phase_cnt_missed)
        {
            /* Get audio sink count */
            audio_sink_cnt  = Sys_Audiosink_Counter() << SHIFT_BIT;
            audio_sink_cnt +=
                ((((audio_sink_phase_cnt_prev - audio_sink_phase_cnt))
                  << SHIFT_BIT) / audio_sink_period_cnt);
        }

        /* store audio sink count phase for the next time */
        audio_sink_phase_cnt_prev = audio_sink_phase_cnt;
        phase_cnt_missed = false;
    }
    flag_ascc_phase = true;

    AUDIOSINK_CTRL->CNT_RESET_ALIAS = CNT_RESET_BITBAND;
    AUDIOSINK->PHASE_CNT = 0;
    AUDIOSINK_CTRL->PHASE_CNT_START_ALIAS = PHASE_CNT_START_BITBAND;
}

/* ----------------------------------------------------------------------------
 * Function      : void Ascc_period_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : ASCC period interrupt handler (TX/RX)
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
#define MAX_BUF_CNT                     5
float measure_buf[MAX_BUF_CNT];
uint8_t buf_cnt = 0;
void Ascc_period_isr(void)
{
	if(1)
	{

    audio_sink_period_cnt = Sys_Audiosink_PeriodCounter() / \
                            (AUDIOSINK->CFG + 1);
    AUDIOSINK->PERIOD_CNT = 0;
    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = PERIOD_CNT_START_BITBAND;
    }
	else
	{
    /* Parameters for RC oscillator period measurements */
    static uint32_t num_measurement = LOW_POWER_CLK_INITIAL_MEASUREMENT;
    static uint32_t audiosink_period = 0;
    static uint32_t audiosink_period_cnt = 0;
    static uint32_t audiosink_period_sum = 0;
    float average_period;
    uint8_t i;

    /* Record period count value and add it to the total sum*/
    audiosink_period = Sys_Audiosink_PeriodCounter();
    audiosink_period_cnt++;
    audiosink_period_sum += audiosink_period;

#if (LOW_POWER_CLK_UPDATE == LOW_POWER_CLK_UPDATE_DISABLE)

    /* Allow the RC clock period to be set once */
    if (low_power_clk_param.dynamic_measurement_enable == false)

#endif    /* if LOW_POWER_CLK_UPDATE */
    {
        if (audiosink_period_cnt == num_measurement)
        {
            /* Calculate the average period for the number of audiosink cycles,
             * each taking audiosink_period_cnt samples */
            average_period = (audiosink_period_sum /
                             (audiosink_period_cnt * LOW_POWER_CLK_SCALE_AVERAGE_PERIOD));

            /* Reset our total sum and count */
            audiosink_period_cnt = 0;
            audiosink_period_sum = 0;

            /* On first iteration make the previous average period value the
             * same as the current average value */
            if (low_power_clk_param.dynamic_measurement_enable == false)
            {
                measure_buf[buf_cnt] = average_period;
                buf_cnt = ((buf_cnt + 1) % MAX_BUF_CNT);


                for (i = 0; i < MAX_BUF_CNT; i++)
                {
                    measure_buf[i] = average_period;
                }
            }

            else
            {
                measure_buf[buf_cnt] = average_period;
                buf_cnt = ((buf_cnt + 1) % MAX_BUF_CNT);

                float max = measure_buf[0];
                float min = measure_buf[0];
                for (i = 1; i < MAX_BUF_CNT; i++ )
                {
                    if(measure_buf[i] > max)
                    {
                        max = measure_buf[i];
                    }
                    else if (measure_buf[i] < min)
                    {
                        min = measure_buf[i];
                    }
                }

                average_period = 0;
                for (i = 0; i < MAX_BUF_CNT; i++ )
                {
                    average_period = (average_period + measure_buf[i]);
                }

                average_period = (average_period - min - max);
                average_period = (average_period / (MAX_BUF_CNT - 2));
            }

            NVIC_DisableIRQ(AUDIOSINK_PERIOD_IRQn);

            if (RTC_CLK_SRC == RTC_CLK_SRC_RC_OSC)
            {
                RTCCLK_Period_Value_Set(average_period * 1.00035);
            }
            else
            {
               RTCCLK_Period_Value_Set(average_period);
            }

            /* Allow the device to go into sleep mode */
            low_power_clk_param.low_power_enable = true;

            /* Enable dynamic measurements */
#if (LOW_POWER_CLK_UPDATE == LOW_POWER_CLK_UPDATE_DISABLE)
            low_power_clk_param.dynamic_measurement_enable = true;
#endif

        }
    }

    AUDIOSINK->PERIOD_CNT = 0;

    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = 1;
   }
}

/* ----------------------------------------------------------------------------
 * Function      : void Rendering_func(uint8_t * src_addr)
 * ----------------------------------------------------------------------------
 * Description   : rendering function sends the received data to the decoder
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Rendering_func(uint8_t *src_addr)
{
    frame_in = src_addr;

    /* De-assert SPI_CS */
    SPI0_CTRL1->SPI0_CS_ALIAS = SPI0_CS_1_BITBAND;

    /* Call the decoder for the first time for a full frame */
    Start_Dec_Lpdsp32(&frame_in[0]);
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
void Start_Dec_Lpdsp32(uint8_t *src_addr)
{
    memcpy(Cm2DspBuff0dec, src_addr,
           ENCODED_SUBFRAME_LENGTH * sizeof(uint8_t));

    /* Decoding command */
    SYSCTRL_DSS_CMD->DSS_CMD_1_ALIAS = DSS_CMD_1_BITBAND;
}

/* ----------------------------------------------------------------------------
 * Function      : void Asrc_in_dma_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : ASRC input DMA interrupt handler (TX/RX)
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Asrc_in_dma_isr(void)
{
    /* Stop ASRC if complete frame has been handled */
    Sys_ASRC_StatusConfig(ASRC_DISABLE);
}
#endif


#endif
