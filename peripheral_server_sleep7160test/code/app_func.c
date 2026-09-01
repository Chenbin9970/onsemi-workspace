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
#ifndef APP_ASHA_ENABLE
void __attribute__ ((alias("Asrc_in_dma_isr")))
DMA_IRQ_FUNC(ASRC_IN_IDX)(void);

void __attribute__ ((alias("Ascc_phase_isr")))
AUDIOSINK_PHASE_IRQHandler(void);

void __attribute__ ((alias("Ascc_period_isr")))
AUDIOSINK_PERIOD_IRQHandler(void);
#endif

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

/* 测试：捕获 DSP 解码输出 160 个连续采样（16k，一帧）。
 * DspDec_isr 每次拷 SUBFRAME_LENGTH(8) 个，满 160 停止；
 * 调试器直接读 dsp_cap[]，想重新采样把 dsp_cap_idx 置 0。 */
int16_t dsp_cap[FRAME_LENGTH];
uint16_t dsp_cap_idx;

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
#ifdef OD_DIO12_OUTPUT
        asrc_inc_carrier  = ((((Cr - Ck) << 29) / Ck) << 0);
        asrc_inc_carrier &= 0xFFFFFFFF;
        Sys_ASRC_Config(asrc_inc_carrier, WIDE_BAND | ASRC_DEC_MODE1);
#else
        asrc_inc_carrier  = ((((Cr - Ck) << 28) / Ck) << 0);
        asrc_inc_carrier &= 0xFFFFFFFF;
        Sys_ASRC_Config(asrc_inc_carrier, WIDE_BAND | ASRC_DEC_MODE2);
#endif
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
   // PRINTF("S1\r\n");
    memcpy(Cm2DspBuff0dec, src_addr,
           ENCODED_SUBFRAME_LENGTH * sizeof(uint8_t));
   // PRINTF("S2\r\n");

    /* Decoding command */
    SYSCTRL_DSS_CMD->DSS_CMD_1_ALIAS = DSS_CMD_1_BITBAND;
   // PRINTF("S3\r\n");
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

/* PCM 双缓冲状态（ch4 填 / ch5 流） */
volatile uint8_t pcm_fill = 0;
volatile uint8_t pcm_ready = 0xFF;   /* 0xFF = 无待流出的 buf */
volatile uint8_t pcm_waiting = 0;

/* 数据内容抓数：cap_in(16k DSP 输出，进 ASRC 前) + cap_out(12k PCM 输出，
 * 出 ASRC 后)，两路各满 CAP_N 后 cap_done=1 冻结不再覆盖。
 * J-Link 读到 cap_done=1 即 dump 满快照；复位 cap_done=0 且两索引=0 重抓。
 * CAP_N=1024：64ms@16k / 85ms@12k，FFT 分辨率 ~15.6Hz（DRAM 放不下 2048×2）。 */
#define CAP_N   1024
int16_t cap_in[CAP_N];
int16_t cap_out[CAP_N];
volatile uint32_t cap_in_idx;
volatile uint32_t cap_out_idx;
volatile uint8_t  cap_done;

#ifdef RESAMP_SW
/* ================= 软件 16k→12k 多相 FIR 重采样（替换硬件 ASRC） =================
 * 测试金属尾音是否 ASRC 插值/相位伪影。固定 3:4 比例（阶段1，不跟踪时钟）。
 * 直接分数 sinc 插值，3 相位（frac 0/1/3/2/3）× 32 抽头，Q15 系数。
 * 每个 DspDec_isr（8 输入）产出 6 个 12k 输出，填 pcm_tx_buf[pcm_fill]，
 * 满 PCM_FRAME_WORDS(120) 换手 + 武装 ch5。 */
#define RESAMP_TAPS     32
#define RESAMP_PHASES   32
#define RESAMP_RING     64

static const int16_t resamp_coef[RESAMP_PHASES][RESAMP_TAPS] = {
    {0,-2,11,-20,0,75,-179,204,0,-467,964,-984,0,2130,-4894,7260,24576,7260,-4894,2130,0,-984,964,-467,0,204,-179,75,0,-20,11,-2},
    {0,-2,11,-22,4,70,-182,221,-33,-437,973,-1066,150,1994,-4968,8032,24553,6496,-4796,2250,-147,-899,951,-495,32,186,-176,79,-4,-18,10,-2},
    {0,-2,12,-24,9,65,-183,238,-67,-403,976,-1144,303,1843,-5017,8813,24485,5744,-4676,2356,-289,-810,932,-518,63,167,-172,82,-8,-16,10,-2},
    {0,-2,12,-25,13,60,-183,254,-101,-367,974,-1217,458,1678,-5040,9598,24373,5006,-4534,2445,-427,-719,909,-539,93,148,-167,85,-12,-14,10,-2},
    {0,-1,12,-27,18,53,-183,269,-135,-327,967,-1285,614,1499,-5035,10386,24216,4284,-4372,2519,-558,-627,881,-555,122,128,-161,87,-15,-12,9,-2},
    {0,-1,12,-29,23,47,-181,282,-170,-284,953,-1347,771,1307,-5001,11175,24015,3579,-4192,2577,-684,-533,849,-569,149,109,-155,89,-19,-10,8,-2},
    {0,-1,12,-31,28,40,-178,295,-205,-239,934,-1404,927,1101,-4938,11961,23771,2894,-3995,2620,-803,-438,813,-579,175,89,-148,90,-22,-8,8,-1},
    {0,-1,12,-32,33,32,-174,306,-239,-192,910,-1453,1081,884,-4845,12743,23484,2230,-3783,2647,-915,-343,774,-585,199,70,-140,90,-25,-6,7,-1},
    {0,-1,12,-33,38,24,-169,316,-273,-142,879,-1496,1234,656,-4721,13519,23156,1588,-3557,2659,-1020,-248,731,-588,222,50,-132,90,-27,-4,7,-1},
    {0,-1,12,-35,43,15,-162,324,-306,-90,843,-1531,1383,417,-4565,14285,22787,971,-3318,2657,-1116,-154,685,-588,242,31,-123,89,-29,-3,6,-1},
    {0,0,11,-36,47,6,-155,330,-338,-37,802,-1559,1528,170,-4378,15040,22380,379,-3070,2639,-1204,-61,636,-584,261,12,-114,88,-31,-1,6,-1},
    {0,0,11,-37,52,-3,-146,335,-369,19,755,-1578,1668,-86,-4158,15781,21934,-185,-2812,2608,-1284,30,585,-578,278,-6,-104,87,-33,1,5,-1},
    {0,0,10,-37,57,-13,-137,338,-399,75,702,-1589,1802,-349,-3905,16506,21453,-722,-2547,2564,-1354,119,532,-568,293,-24,-94,85,-35,2,4,-1},
    {0,1,10,-38,61,-23,-126,340,-428,132,645,-1592,1930,-618,-3620,17212,20936,-1229,-2276,2506,-1416,205,477,-555,306,-41,-84,82,-36,3,4,-1},
    {0,1,9,-38,65,-33,-114,339,-454,190,582,-1585,2050,-891,-3302,17897,20387,-1706,-2002,2437,-1468,288,421,-540,316,-57,-74,80,-37,5,3,-1},
    {0,2,8,-38,69,-43,-101,336,-479,248,515,-1570,2161,-1167,-2951,18559,19806,-2153,-1724,2356,-1511,368,364,-522,325,-73,-63,77,-37,6,3,-1},
    {-1,2,7,-38,73,-53,-88,332,-502,306,444,-1545,2263,-1446,-2568,19196,19196,-2568,-1446,2263,-1545,444,306,-502,332,-88,-53,73,-38,7,2,-1},
    {-1,3,6,-37,77,-63,-73,325,-522,364,368,-1511,2356,-1724,-2153,19806,18559,-2951,-1167,2161,-1570,515,248,-479,336,-101,-43,69,-38,8,2,0},
    {-1,3,5,-37,80,-74,-57,316,-540,421,288,-1468,2437,-2002,-1706,20387,17897,-3302,-891,2050,-1585,582,190,-454,339,-114,-33,65,-38,9,1,0},
    {-1,4,3,-36,82,-84,-41,306,-555,477,205,-1416,2506,-2276,-1229,20936,17212,-3620,-618,1930,-1592,645,132,-428,340,-126,-23,61,-38,10,1,0},
    {-1,4,2,-35,85,-94,-24,293,-568,532,119,-1354,2564,-2547,-722,21453,16506,-3905,-349,1802,-1589,702,75,-399,338,-137,-13,57,-37,10,0,0},
    {-1,5,1,-33,87,-104,-6,278,-578,585,30,-1284,2608,-2812,-185,21934,15781,-4158,-86,1668,-1578,755,19,-369,335,-146,-3,52,-37,11,0,0},
    {-1,6,-1,-31,88,-114,12,261,-584,636,-61,-1204,2639,-3070,379,22380,15040,-4378,170,1528,-1559,802,-37,-338,330,-155,6,47,-36,11,0,0},
    {-1,6,-3,-29,89,-123,31,242,-588,685,-154,-1116,2657,-3318,971,22787,14285,-4565,417,1383,-1531,843,-90,-306,324,-162,15,43,-35,12,-1,0},
    {-1,7,-4,-27,90,-132,50,222,-588,731,-248,-1020,2659,-3557,1588,23156,13519,-4721,656,1234,-1496,879,-142,-273,316,-169,24,38,-33,12,-1,0},
    {-1,7,-6,-25,90,-140,70,199,-585,774,-343,-915,2647,-3783,2230,23484,12743,-4845,884,1081,-1453,910,-192,-239,306,-174,32,33,-32,12,-1,0},
    {-1,8,-8,-22,90,-148,89,175,-579,813,-438,-803,2620,-3995,2894,23771,11961,-4938,1101,927,-1404,934,-239,-205,295,-178,40,28,-31,12,-1,0},
    {-2,8,-10,-19,89,-155,109,149,-569,849,-533,-684,2577,-4192,3579,24015,11175,-5001,1307,771,-1347,953,-284,-170,282,-181,47,23,-29,12,-1,0},
    {-2,9,-12,-15,87,-161,128,122,-555,881,-627,-558,2519,-4372,4284,24216,10386,-5035,1499,614,-1285,967,-327,-135,269,-183,53,18,-27,12,-1,0},
    {-2,10,-14,-12,85,-167,148,93,-539,909,-719,-427,2445,-4534,5006,24373,9598,-5040,1678,458,-1217,974,-367,-101,254,-183,60,13,-25,12,-2,0},
    {-2,10,-16,-8,82,-172,167,63,-518,932,-810,-289,2356,-4676,5744,24485,8813,-5017,1843,303,-1144,976,-403,-67,238,-183,65,9,-24,12,-2,0},
    {-2,10,-18,-4,79,-176,186,32,-495,951,-899,-147,2250,-4796,6496,24553,8032,-4968,1994,150,-1066,973,-437,-33,221,-182,70,4,-22,11,-2,0},
};

static int16_t resamp_ring[RESAMP_RING];
static uint32_t resamp_total;      /* 收到的 16k 输入总数 */
static uint32_t resamp_out_next;   /* 下一个要产生的 12k 输出序号 */
static int32_t  resamp_inc = 87381;   /* Q16: 输出位置增量=Cr/Ck≈4/3，audiosink 时钟跟踪 */
static uint16_t resamp_buf_cnt;    /* 当前 pcm_tx_buf[pcm_fill] 已填数 */
static int32_t rs_h12_y[TEST_HPF_12K_ORDER];
static int16_t rs_h12_xp[TEST_HPF_12K_ORDER];

static void Resamp_Process(int16_t *in, uint8_t n)
{
    uint8_t k;
    /* 推入输入环形缓冲 */
    for (k = 0; k < n; k++)
    {
        resamp_ring[resamp_total % RESAMP_RING] = in[k];
        resamp_total++;
    }
    /* 阶段2时钟跟踪：audiosink 测实际 12k，更新相位增量（Cr/Ck）。
       Ck=audio_sink_cnt≈120<<20（标称 12k），守卫取 80<<20 ~ 320<<20。 */
    if (Ck > ((int64_t)FRAME_LENGTH << SHIFT_BIT) / 2 &&
        Ck < ((int64_t)FRAME_LENGTH << SHIFT_BIT) * 2)
    {
        int32_t inc = (int32_t)(((int64_t)Cr << 16) / Ck);
        if (inc < 60000)  inc = 60000;    /* 限幅：sink 率 8k~16k 对应 inc 1.0~2.0 */
        if (inc > 110000) inc = 110000;
        resamp_inc = inc;
    }
    /* 产出所有可用的 12k 输出 */
    for (;;)
    {
        /* 输出位置 = out_next * inc（Q16），n0=整数部分，frac 取最近相位 */
        int64_t pos = (int64_t)resamp_out_next * resamp_inc;
        uint32_t n0 = (uint32_t)(pos >> 16);
        uint32_t frac = (uint32_t)(pos & 0xFFFFu);
        uint32_t r = frac >> 11;   /* 32 相位：frac/2048，0..31 */
        uint32_t j;

        /* 需要输入到 n0 + TAPS/2（j=0 读 x[n0+16]），不足则等下一子帧 */
        if (n0 + RESAMP_TAPS / 2 >= resamp_total)
        {
            break;
        }
        {
            int64_t acc = 0;
            for (j = 0; j < RESAMP_TAPS; j++)
            {
                /* y[m] = sum_j coef[r][j] * x[n0 - (j - TAPS/2)] = coef 权重 x[n0-j'] */
                int32_t g = (int32_t)n0 - (int32_t)j + (RESAMP_TAPS / 2);
                /* int16×int16 积不溢出 int32 → 单条 MUL，避免 int64 长乘拖慢 ISR */
                acc += (int64_t)((int32_t)resamp_coef[r][j] *
                                 resamp_ring[(uint32_t)g % RESAMP_RING]);
            }
            /* 12k HPF（同 TEST_HPF_12K，滤 47Hz 嗡嗡） */
            {
                int32_t x = (int32_t)((acc + 16384) >> 15);
                uint8_t s;
                for (s = 0; s < TEST_HPF_12K_ORDER; s++)
                {
                    int64_t a = (int64_t)31932 *
                                ((int64_t)x - rs_h12_xp[s] + rs_h12_y[s]);
                    rs_h12_xp[s] = (int16_t)x;
                    rs_h12_y[s] = (int32_t)((a + 16384) >> 15);
                    x = rs_h12_y[s];
                }
                pcm_tx_buf[pcm_fill][resamp_buf_cnt] =
                    (uint32_t)((uint16_t)(int16_t)x);
            }
        }
        resamp_buf_cnt++;
        resamp_out_next++;

        if (resamp_buf_cnt >= PCM_FRAME_WORDS)
        {
            /* 帧满：换手 + 抓 cap_out + 武装 ch5（同 Pcm_asrc_out_dma_isr 逻辑） */
            pcm_ready = pcm_fill;
            pcm_fill = 1 - pcm_fill;
            resamp_buf_cnt = 0;

            if (cap_out_idx < CAP_N)
            {
                uint32_t capn = CAP_N - cap_out_idx;
                if (capn > PCM_FRAME_WORDS) capn = PCM_FRAME_WORDS;
                for (k = 0; k < capn; k++)
                {
                    cap_out[cap_out_idx + k] =
                        (int16_t)(pcm_tx_buf[pcm_ready][k] & 0xFFFFu);
                }
                cap_out_idx += capn;
                if (cap_in_idx >= CAP_N && cap_out_idx >= CAP_N)
                {
                    cap_done = 1;
                }
            }

            if (pcm_waiting)
            {
                pcm_waiting = 0;
                Sys_DMA_ChannelConfig(PCM_DMA_NUM, RX_DMA_PCM_STEREO,
                                      PCM_FRAME_WORDS, 0,
                                      (uint32_t)&pcm_tx_buf[pcm_ready][0],
                                      (uint32_t)&PCM->TX_DATA);
                Sys_DMA_ClearChannelStatus(PCM_DMA_NUM);
                Sys_DMA_ChannelEnable(PCM_DMA_NUM);
                pcm_ready = 0xFF;
            }
        }
    }
}
#endif    /* ifdef RESAMP_SW */

#ifndef OD_DIO12_OUTPUT
/* ----------------------------------------------------------------------------
 * Function      : void Pcm_asrc_out_dma_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : ASRC 输出 DMA 完成。ch4 把 ASRC->OUT 采进 pcm_tx_buf[pcm_fill]；
 *                 标记 ready、切换 buf 并重新武装；若 ch5 在等则启动 ch5。
 * ------------------------------------------------------------------------- */
void Pcm_asrc_out_dma_isr(void)
{
    pcm_ready = pcm_fill;
    pcm_fill = 1 - pcm_fill;

#ifdef TEST_HPF_12K
    /* 12k 高通滤波：级联 TEST_HPF_12K_ORDER 个一阶 IIR，低切 ~100Hz（12k 采样），
       滤掉 DSP 输出里的 ~47Hz 底噪。pcm_tx_buf 是 CM 独占、DMA 填好，不会被 DSP
       覆盖（对比 16k 原位滤波 Dsp2CmBuff0dec 无效）。采样在 32-bit 字低 16 位。 */
    {
        static int32_t h12_y[TEST_HPF_12K_ORDER];
        static int16_t h12_xp[TEST_HPF_12K_ORDER];
        const int32_t h12_alpha = 31932;   /* Q15: α=0.97448, fc≈100Hz@12k */
        uint32_t j;
        for (j = 0; j < PCM_FRAME_WORDS; j++)
        {
            int32_t x = (int32_t)(int16_t)(pcm_tx_buf[pcm_ready][j] & 0xFFFFu);
            uint8_t s;
            for (s = 0; s < TEST_HPF_12K_ORDER; s++)
            {
                int64_t acc = (int64_t)h12_alpha *
                              ((int64_t)x - h12_xp[s] + h12_y[s]);
                h12_xp[s] = (int16_t)x;
                h12_y[s] = (int32_t)((acc + 16384) >> 15);   /* 四舍五入，避免向负截断引入 DC 偏置 */
                x = h12_y[s];
            }
            pcm_tx_buf[pcm_ready][j] =
                (uint32_t)((uint16_t)(int16_t)x) |
                (pcm_tx_buf[pcm_ready][j] & 0xFFFF0000u);
        }
    }
#endif    /* ifdef TEST_HPF_12K */

#ifdef TEST_LPF_12K
    /* 12k 低通滤波：级联 TEST_LPF_12K_ORDER 个一阶 IIR，截止 ~3k（12k 采样），
       狠压高频金属尾音（音乐高频成分）。采样在 32-bit 字低 16 位。 */
    {
        static int32_t l12_y[TEST_LPF_12K_ORDER];
        const int32_t l12_keep  = 6812;    /* Q15: 1-α = 0.2079 */
        const int32_t l12_alpha = 25956;   /* Q15: α = 0.7921, fc≈3k@12k */
        uint32_t j;
        for (j = 0; j < PCM_FRAME_WORDS; j++)
        {
            int32_t x = (int32_t)(int16_t)(pcm_tx_buf[pcm_ready][j] & 0xFFFFu);
            uint8_t s;
            for (s = 0; s < TEST_LPF_12K_ORDER; s++)
            {
                l12_y[s] = (l12_keep * l12_y[s] + l12_alpha * x + 16384) >> 15;
                x = l12_y[s];
            }
            pcm_tx_buf[pcm_ready][j] =
                (uint32_t)((uint16_t)(int16_t)x) |
                (pcm_tx_buf[pcm_ready][j] & 0xFFFF0000u);
        }
    }
#endif    /* ifdef TEST_LPF_12K */

#ifdef ASRC_OUT_DITHER
    /* 12k 输出 dither：给 ASRC 输出（pcm_tx_buf）加 ±8 LSB（xorshift32），
       打破重采样输出端量化/极限环（金属尾音候选）。加在滤波后、ch5 流之前。 */
    {
        static uint32_t lfsr = 0x9E3779B9;
        uint32_t j;
        for (j = 0; j < PCM_FRAME_WORDS; j++)
        {
            int16_t s = (int16_t)(pcm_tx_buf[pcm_ready][j] & 0xFFFFu);
            lfsr ^= lfsr << 13;
            lfsr ^= lfsr >> 17;
            lfsr ^= lfsr << 5;
            s += (lfsr & 1) ? 8 : -8;
            pcm_tx_buf[pcm_ready][j] =
                (uint32_t)((uint16_t)s) | (pcm_tx_buf[pcm_ready][j] & 0xFFFF0000u);
        }
    }
#endif    /* ifdef ASRC_OUT_DITHER */

    /* 抓 12k PCM 输出（出 ASRC + 12k 滤波 + 输出 dither 后，7100 实际收到的音频） */
    if (cap_out_idx < CAP_N)
    {
        uint32_t n = CAP_N - cap_out_idx;
        uint32_t i;
        if (n > PCM_FRAME_WORDS)
        {
            n = PCM_FRAME_WORDS;
        }
        for (i = 0; i < n; i++)
        {
            cap_out[cap_out_idx + i] =
                (int16_t)(pcm_tx_buf[pcm_ready][i] & 0xFFFFu);
        }
        cap_out_idx += n;
    }

    Sys_DMA_ChannelConfig(ASRC_OUT_IDX, RX_DMA_ASRC_OUT, PCM_FRAME_WORDS, 0,
                          (uint32_t)&ASRC->OUT, (uint32_t)&pcm_tx_buf[pcm_fill][0]);
    Sys_DMA_ClearChannelStatus(ASRC_OUT_IDX);
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);

    if (pcm_waiting)
    {
        pcm_waiting = 0;
        Sys_DMA_ChannelConfig(PCM_DMA_NUM, RX_DMA_PCM_STEREO, PCM_FRAME_WORDS, 0,
                              (uint32_t)&pcm_tx_buf[pcm_ready][0],
                              (uint32_t)&PCM->TX_DATA);
        Sys_DMA_ClearChannelStatus(PCM_DMA_NUM);
        Sys_DMA_ChannelEnable(PCM_DMA_NUM);
        pcm_ready = 0xFF;
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void Pcm_tx_dma_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : PCM TX DMA 完成。ch5 流完当前 buf；若 ch4 有新 buf 则继续流，
 *                 否则等 ch4 的完成中断启动。
 * ------------------------------------------------------------------------- */
void Pcm_tx_dma_isr(void)
{
    if (pcm_ready != 0xFF)
    {
        Sys_DMA_ChannelConfig(PCM_DMA_NUM, RX_DMA_PCM_STEREO, PCM_FRAME_WORDS, 0,
                              (uint32_t)&pcm_tx_buf[pcm_ready][0],
                              (uint32_t)&PCM->TX_DATA);
        Sys_DMA_ClearChannelStatus(PCM_DMA_NUM);
        Sys_DMA_ChannelEnable(PCM_DMA_NUM);
        pcm_ready = 0xFF;
    }
    else
    {
        pcm_waiting = 1;
    }
}

/* PCM DMA ISR 别名：向量表 DMA4/DMA5 -> 上述处理函数 */
void __attribute__ ((alias("Pcm_asrc_out_dma_isr")))
DMA_IRQ_FUNC(ASRC_OUT_IDX)(void);

void __attribute__ ((alias("Pcm_tx_dma_isr")))
DMA_IRQ_FUNC(PCM_DMA_NUM)(void);
#endif    /* ifndef OD_DIO12_OUTPUT */



#ifndef APP_ASHA_ENABLE
void __attribute__ ((alias("Asrc_in_dma_isr")))
DMA_IRQ_FUNC(ASRC_IN_IDX)(void);

void __attribute__ ((alias("Ascc_phase_isr")))
AUDIOSINK_PHASE_IRQHandler(void);

void __attribute__ ((alias("Ascc_period_isr")))
AUDIOSINK_PERIOD_IRQHandler(void);

#endif
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
    if (Ck != 0)
    {
#ifdef OD_DIO12_OUTPUT
        asrc_inc_carrier  = ((((Cr - Ck) << 29) / Ck) << 0);
        asrc_inc_carrier &= 0xFFFFFFFF;
        Sys_ASRC_Config(asrc_inc_carrier, WIDE_BAND | ASRC_DEC_MODE1);
#else
        asrc_inc_carrier  = ((((Cr - Ck) << 28) / Ck) << 0);
        asrc_inc_carrier &= 0xFFFFFFFF;
        Sys_ASRC_Config(asrc_inc_carrier, WIDE_BAND | ASRC_DEC_MODE2);
#endif
    }
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
#ifdef TEST_LPF
    /* 低通滤波：级联 TEST_LPF_ORDER 个一阶 IIR，每级截止 ~4k（16k 采样），
       阶数越高滚降越陡。in-place 处理 Dsp2CmBuff0dec，各阶状态跨子帧保持。
       cap_in 捕获的是滤波前原始值。
       换截止：α = 1-exp(-2π·fc/16000)，keep = 32768-α（Q15）。 */
    {
        static int32_t lpf_y[TEST_LPF_ORDER];
        const int32_t lpf_keep  = 6812;    /* Q15: 1-α = 0.2079 */
        const int32_t lpf_alpha = 25956;   /* Q15: α = 0.7921, fc=4k@16k */
        int16_t *p = (int16_t *)Dsp2CmBuff0dec;
        uint8_t i, s;
        for (i = 0; i < SUBFRAME_LENGTH; i++)
        {
            int32_t x = p[i];
            for (s = 0; s < TEST_LPF_ORDER; s++)
            {
                lpf_y[s] = (lpf_keep * lpf_y[s] + lpf_alpha * x) >> 15;
                x = lpf_y[s];
            }
            p[i] = (int16_t)x;
        }
    }
#endif    /* ifdef TEST_LPF */

#ifdef TEST_HPF
    /* 高通滤波：级联 TEST_HPF_ORDER 个一阶 IIR，每级低切 ~100Hz（16k 采样），
       滤掉解码音频里的 ~47Hz 底噪（静音时的嗡嗡）。in-place 处理 Dsp2CmBuff0dec，
       各阶状态跨子帧保持。cap_in 捕获的是滤波前原始值。
       换截止：α = 1/(1+tan(π·fc/16000))，Q15。 */
    {
        static int32_t hpf_y[TEST_HPF_ORDER];
        static int16_t hpf_xp[TEST_HPF_ORDER];
        const int32_t hpf_alpha = 32136;   /* Q15: α=0.98074, fc≈100Hz@16k */
        int16_t *p = (int16_t *)Dsp2CmBuff0dec;
        uint8_t i, s;
        for (i = 0; i < SUBFRAME_LENGTH; i++)
        {
            int32_t x = p[i];
            for (s = 0; s < TEST_HPF_ORDER; s++)
            {
                int64_t acc = (int64_t)hpf_alpha *
                              ((int64_t)x - hpf_xp[s] + hpf_y[s]);
                hpf_xp[s] = (int16_t)x;
                hpf_y[s] = (int32_t)(acc >> 15);
                x = hpf_y[s];
            }
            p[i] = (int16_t)x;
        }
    }
#endif    /* ifdef TEST_HPF */

#ifdef ASRC_DITHER
    /* Dither Dsp2CmBuff0dec（±8 LSB，xorshift32）打破 ASRC 极限环（静音蚊蚊/单频音）。
       加在滤波之后、ASRC 读取之前。 */
    {
        static uint32_t lfsr = 0x9E3779B9;
        int16_t *p = (int16_t *)Dsp2CmBuff0dec;
        uint8_t i;
        for (i = 0; i < SUBFRAME_LENGTH; i++)
        {
            lfsr ^= lfsr << 13;
            lfsr ^= lfsr >> 17;
            lfsr ^= lfsr << 5;
            p[i] += (lfsr & 1) ? 8 : -8;
        }
    }
#endif    /* ifdef ASRC_DITHER */

    /* 抓 16k DSP 解码输出（滤波+dither 后，实际喂给 ASRC 的信号） */
    if (cap_in_idx < CAP_N)
    {
        uint32_t n = CAP_N - cap_in_idx;
        if (n > SUBFRAME_LENGTH)
        {
            n = SUBFRAME_LENGTH;
        }
        memcpy(&cap_in[cap_in_idx], Dsp2CmBuff0dec, n * sizeof(int16_t));
        cap_in_idx += n;
    }
    /* 两路都满 1024：cap_done=1 冻结（不再覆盖），J-Link 读满快照后复位重抓 */
    if (cap_in_idx >= CAP_N && cap_out_idx >= CAP_N)
    {
        cap_done = 1;
    }

#if defined(TEST_AAF_16K) && !defined(RESAMP_SW)
    /* 16k anti-alias 低通（防混叠）：16k→12k 下采样前必须滤掉 >6k，否则折回 4-6k。
       拷 Dsp2CmBuff0dec → CM 缓冲 filt_buf 再滤（Dsp2CmBuff0dec 是 DSP/CM 共享内存，
       原位滤波被 DSP 覆盖无效），然后让 DMA ch3 从 filt_buf 读。
       RESAMP_SW 时禁用（软件重采样自带抗混叠）。 */
    {
        static int16_t filt_buf[SUBFRAME_LENGTH];
        static int32_t aaf_y[TEST_AAF_16K_ORDER];
        const int32_t aaf_keep  = 4600;    /* Q15: 1-α = 0.1404 */
        const int32_t aaf_alpha = 28168;   /* Q15: α = 0.8596, fc≈5k@16k */
        uint8_t i, s;
        memcpy(filt_buf, Dsp2CmBuff0dec, SUBFRAME_LENGTH * sizeof(int16_t));
        for (i = 0; i < SUBFRAME_LENGTH; i++)
        {
            int32_t x = filt_buf[i];
            for (s = 0; s < TEST_AAF_16K_ORDER; s++)
            {
                aaf_y[s] = (aaf_keep * aaf_y[s] + aaf_alpha * x + 16384) >> 15;
                x = aaf_y[s];
            }
            filt_buf[i] = (int16_t)x;
        }
        Sys_DMA_ChannelConfig(ASRC_IN_IDX, RX_DMA_ASRC_IN, SUBFRAME_LENGTH, 0,
                              (uint32_t)filt_buf, (uint32_t)&ASRC->IN);
    }
#endif    /* ifdef TEST_AAF_16K */

#ifdef RESAMP_SW
    /* 软件 16k→12k 重采样（替换 ASRC）：用 DSP 输出直接重采样喂 pcm_tx_buf */
    Resamp_Process((int16_t *)Dsp2CmBuff0dec, SUBFRAME_LENGTH);
#else
    /* Clear DMA channel status */
    Sys_DMA_ClearChannelStatus(ASRC_IN_IDX);

    /* Re-enable DMA for ASRC input */
    Sys_DMA_ChannelEnable(ASRC_IN_IDX);

    /* Enable ASRC block */
    Sys_ASRC_StatusConfig(ASRC_ENABLE);
#endif
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
        if (!phase_cnt_missed && audio_sink_period_cnt != 0)
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
	if(app_env.audio_streaming)
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
