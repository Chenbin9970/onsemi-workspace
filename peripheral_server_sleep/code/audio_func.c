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

uint8_t ear_side = APP_RM_AUDIO_CHANNEL;

#if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT)
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

#if (DEBUG_UART_LOG)
uint8_t UARTTXBuffer[60];

/* ----------------------------------------------------------------------------
 * Function      : void UartLogInit(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize UART for debugging
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void UartLogInit(void)
{
    Sys_UART_DIOConfig(DIO_6X_DRIVE | DIO_WEAK_PULL_UP | DIO_LPF_ENABLE,
                       UART_TX, UART_RX);
    Sys_UART_Enable(SystemCoreClock, UART_BAUD_RATE, UART_DMA_MODE_ENABLE);

    Sys_DMA_ChannelConfig(UART_TX_NUM, UART_TX_CFG, 1, 0,
                          (uint32_t)&UARTTXBuffer[0],
                          (uint32_t)&UART->TX_DATA);
}

/* ----------------------------------------------------------------------------
 * Function      : void UartLogInit(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize UART for debugging
 * Inputs        : - str: Pointer to the string you want to pass out
 * Outputs       : None
 * Assumptions   : UartLogInit() had to be called
 * ------------------------------------------------------------------------- */
void UartLogTx(uint8_t *str)
{
    Sys_DMA_Set_ChannelSourceAddress(UART_TX_NUM, (uint32_t)str);
    DMA_CTRL1[UART_TX_NUM].TRANSFER_LENGTH_SHORT = strlen((const char *)str);
    Sys_DMA_ChannelEnable(UART_TX_NUM);
}

#endif    /* if (DEBUG_UART_LOG) */

void __attribute__ ((alias("Asrc_in_dma_isr"))) DMA_IRQ_FUNC(ASRC_IN_IDX)(void);

void __attribute__ ((alias("Ascc_phase_isr")))
AUDIOSINK_PHASE_IRQHandler(void);

/* AUDIOSINK_PERIOD_IRQHandler merged into app_process.c */

void __attribute__ ((alias("DspDec_isr"))) DSP1_IRQHandler(void);

void __attribute__ ((alias("Packet_regulator_timer_isr")))
TIMER_IRQ_FUNC(TIMER_REGUL)(void);

#if (SIMUL == 1)
void __attribute__ ((alias("Simulation_timer_isr"))) TIMER_IRQ_FUNC(
    TIMER_SIMUL)(void);

uint8_t frame_simul[ENCODED_FRAME_LENGTH];
extern uint8_t coded_sample[];
uint8_t coded_cntr_simul = 0;
#endif    /* if (SIMUL == 1) */

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
#if (SIMUL == 1)
    static uint32_t sim_missed = 0;
    sim_missed = (sim_missed + 1) % 200;
    if (sim_missed < 10)
#else    /* if (SIMUL == 1) */
    if (AUDIOSINK_CTRL->PHASE_CNT_MISSED_STATUS_ALIAS)
#endif    /* if (SIMUL == 1) */
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
void Ascc_period_isr(void)
{
    audio_sink_period_cnt = Sys_Audiosink_PeriodCounter() / \
                            (AUDIOSINK->CFG + 1);
    AUDIOSINK->PERIOD_CNT = 0;
    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = PERIOD_CNT_START_BITBAND;
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

#if (SIMUL == 1)
/* ----------------------------------------------------------------------------
 * Function      : void Simulation_timer_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : Simulation timer interrupt routine. It's for debugging the
 *                 audio sync mechanism in RX side
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Simulation_timer_isr(void)
{
    frame_in = frame_simul;

    /* memcpy( frame_in,
     *        &coded_sample[coded_cntr],
     *        sizeof(uint8_t)*ENCODED_FRAME_LENGTH); */
    coded_cntr_simul = (coded_cntr_simul + ENCODED_FRAME_LENGTH) % (4 * 60);

    if (coded_cntr_simul == 0)
    {
        Sys_GPIO_Toggle(DEBUG_DIO_THIRD);
    }

    Rendering_func((uint8_t *)&coded_sample[coded_cntr_simul]);

    BBIF->SYNC_CFG = (ACTIVE | SYNC_ENABLE | SYNC_SOURCE_RF_RX);
    BBIF->SYNC_CFG = (ACTIVE | RX_ACTIVE | SYNC_ENABLE | SYNC_SOURCE_RF_RX);

    DIO->BB_RX_SRC = (BB_RF_SYNC_P_SRC_CONST_HIGH | (DIO->BB_RX_SRC &
                                                     ~
                                                     DIO_BB_RX_SRC_RF_SYNC_P_Mask));
    DIO->BB_RX_SRC = (BB_RF_SYNC_P_SRC_CONST_LOW | (DIO->BB_RX_SRC &
                                                    ~
                                                    DIO_BB_RX_SRC_RF_SYNC_P_Mask));

    BBIF->SYNC_CFG = (ACTIVE | RX_IDLE | SYNC_ENABLE | SYNC_SOURCE_RF_RX);
    BBIF->SYNC_CFG = (IDLE | RX_IDLE | SYNC_DISABLE | SYNC_SOURCE_RF_RX);
}

#endif    /* if (SIMUL == 1) */

#endif    /* if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT) */

/* ----------------------------------------------------------------------------
 * Function      : void DIO0_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : Toggle selection for left or right channel
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DIO0_IRQHandler(void)
{
    static uint8_t ignore_next_dio_int = 0;
    if (ignore_next_dio_int)
    {
        ignore_next_dio_int = 0;
    }
    else if (DIO_DATA->ALIAS[BUTTON_DIO] == 0)
    {
        /* Button is pressed: Ignore next interrupt.
         * This is required to deal with the debounce circuit limitations. */
        ignore_next_dio_int = 1;

        ear_side = !ear_side;
#if (SIMUL != 1)
        APP_RM_Init(ear_side);
#endif    /* if (SIMUL != 1) */
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void OD_Init(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize sigma-delta Output Driver on DIO8/DIO9
 * ------------------------------------------------------------------------- */
void OD_Init(void)
{
    /* Configure OD DIO pins */
    Sys_DIO_Config(OD_P_DIO, DIO_MODE_OD_P | DIO_LPF_DISABLE | DIO_NO_PULL | DIO_6X_DRIVE);
    Sys_DIO_Config(OD_N_DIO, DIO_MODE_OD_N | DIO_LPF_DISABLE | DIO_NO_PULL | DIO_6X_DRIVE);

    /* Configure AUDIO block: OD enabled, AUDIOSLOWCLK source, 64x decimation */
    Sys_Audio_Set_Config(OD_AUDIOSLOWCLK | OD_UNDERRUN_PROTECT_ENABLE |
                         OD_DATA_MSB_ALIGNED | OD_DMA_REQ_DISABLE |
                         OD_INT_GEN_ENABLE | OD_ENABLE |
                         DMIC0_ENABLE | DMIC0_DMA_REQ_DISABLE | DMIC0_INT_GEN_DISABLE |
                         DMIC1_DISABLE | DECIMATE_BY_64);

    /* Configure OD sigma-delta parameters */
    Sys_Audio_Set_ODConfig(DCRM_CUTOFF_20HZ | DITHER_ENABLE | OD_RISING_EDGE);

    /* Set nominal OD gain (Q11 = 1.0) */
    AUDIO->OD_GAIN = 0x800;
}
