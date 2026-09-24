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
 * $Revision: 1.8 $
 * $Date: 2018/02/27 15:46:07 $
 * ------------------------------------------------------------------------- */

#include "app.h"
#include "queue.h"
#include <stdio.h>

uint8_t ear_side = APP_RM_AUDIO_CHANNEL;

#if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT)
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

/* Bring-up counters. Read them over J-Link while audio is playing: a stage that
 * stays at 0 is the stage that is not running. Remove once the PCM input path
 * is verified. */
volatile uint32_t dbg_cnt_pcm_isr  = 0;
volatile uint32_t dbg_cnt_asrc_out = 0;
volatile uint32_t dbg_cnt_enc      = 0;

/* Peak |sample| of the most recently decoded PCM block. 0 means the PCM data is
 * arriving as zeros; a value pinned near 32767 means the unpacked word is
 * misaligned and clipping. Only updated when TX_TONE_TEST is 0. */
volatile int32_t dbg_pcm_peak = 0;

int16_t sample_in[FRAME_LENGTH]    = {
#if 1
    0, 16, 31, 47, 63, 78, 93, 109, 124, 138, 153, 167, 182, 195, 209, 222,
    235, 248, 260, 272, 283, 294, 304, 314, 324, 333, 341, 349, 356, 363,
    370, 375, 380, 385, 389, 392, 395, 397, 399, 400, 400, 400, 399, 397,
    395, 392, 389, 385, 380, 375, 370, 363, 356, 349, 341, 333, 324, 314,
    304, 294, 283, 272, 260, 248, 235, 222, 209, 195, 182, 167, 153, 138,
    124, 109, 93, 78, 63, 47, 31, 16, 0, -16, -31, -47, -63, -78, -93, -109,
    -124, -138, -153, -167, -182, -195, -209, -222, -235, -248, -260, -272,
    -283, -294, -304, -314, -324, -333, -341, -349, -356, -363, -370, -375,
    -380, -385, -389, -392, -395, -397, -399, -400, -400, -400, -399, -397,
    -395, -392, -389, -385, -380, -375, -370, -363, -356, -349, -341, -333,
    -324, -314, -304, -294, -283, -272, -260, -248, -235, -222, -209, -195,
    -182, -167, -153, -138, -124, -109, -93, -78, -63, -47, -31, -16,
#else    /* if 1 */
    0, 168, 364, 85, -605, -959, -335, 920, 1583, 742, -1088, -2200, -1288,
    1092, 2774, 1951, -921, -3269, -2704, 570, 3652, 3514, -39, -3893,
    -4347, -662, 3965, 5164, 1517, -3849, -5928, -2505, 3529, 6599, 3598,
    -2997, -7141, -4762, 2252, 7518, 5961, -1301, -7699, -7153, 156, 7658,
    8296, 1162, -7375, -9347, -2624, 6836, 10261, 4197, -6035, -10997,
    -5841, 4972, 11517, 7514, -3658, -11787, -9166, 2109, 11776, 10749,
    -350, -11464, -12212, -1584, 10834, 13505, 3655, -9881, -14580, -5817,
    8605, 15393, 8018, -7017, -15902, -10204, 5137, 16075, 12318, -2993,
    -15883, -14300, 623, 15309, 16092, 1929, -14342, -17638, -4611, 12982,
    18885, 7364, -11238, -19784, -10127, 9131, 20294, 12834, -6690, -20381,
    -15415, 3954, 20019, 17805, -973, -19193, -19936, -2196, 17898, 21745,
    5491, -16139, -23174, -8839, 13935, 24171, 12169, -11314, -24693,
    -15401, 8316, 24705, 18459, -4992, -24183, -21264, 1401, 23114, 23743,
    2386, -21500, -25825, -6294, 19351, 27447, 10241, -16693, -28553,
    -14141, 13565, 29097, 17906, -10015, -29045, -21446, 6105, 28373, 24676,
    -1907, -27073, -27512, -2498, 25148, 29877,
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

void __attribute__ ((alias("Ascc_period_isr")))
AUDIOSINK_PERIOD_IRQHandler(void);

#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT) */

#if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT)
void __attribute__ ((alias("Asrc_out_dma_isr"))) DMA_IRQ_FUNC(ASRC_OUT_IDX)(
    void);

void __attribute__ ((alias("DspEnc0_isr"))) DSP0_IRQHandler(void);

void __attribute__ ((alias("DspEnc1_isr"))) DSP1_IRQHandler(void);

void __attribute__ ((alias("Port_rx_raw_dma_isr"))) DMA_IRQ_FUNC(RX_DMA_NUM)(
    void);

bool lpdsp_rdy = true;
int32_t data_fifo_rec;
int16_t asrc_out_buf[2][FRAME_LENGTH];
int16_t asrc_in_buf[SUBFRAME_LENGTH_LEFT_AND_RIGHT];
uint8_t ptr_rst_cnt = 0;
uint8_t error = 0;

uint8_t tx_data_fifo[2][TX_DATA_FIFO_LENGTH];
int16_t data_wr_idx[2]   = { 0, 0 };
int16_t data_rd_idx[2]   = { 0, 0 };
int16_t w2r[2] = { 0, 0 };
int16_t r2w[2] = { 0, 0 };
uint16_t cntr_asrc_out[2] = { 0, 0 };
uint32_t asrc_state_mem_rx[2][31];
uint32_t cntr_sample = 0;
PacketSide flag_packet_side = PKT_LEFT;
int16_t spi_buf[2 * SUBFRAME_LENGTH];
int32_t pcm_buf[PCM_DMA_BLOCK_WORDS];
struct queue_t queue_tx[2] = { { NULL, NULL }, { NULL, NULL } };
uint8_t cntr_wav    = 0;
int16_t left_data[SUBFRAME_LENGTH];
int16_t right_data[SUBFRAME_LENGTH];
extern const uint8_t coded_sample[60];
uint8_t cntr_enc0 = 0, cntr_enc1 = 0;

#if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD)
struct wm8731_i2c_message
    wm8731_i2c_message_buffer[WM8731_I2C_MESSAGE_BUFFER_SIZE] =
{ { WM8731_RESET_REG_ADDR, WM8731_RESET_REG_VAL },
  { WM8731_POWER_DOWN_CTRL_REG_ADDR, WM8731_POWER_DOWN_CTRL_REG_VAL },
  { WM8731_DAI_REG_ADDR, WM8731_DAI_REG_VAL },
  { WM8731_LLINE_IN_REG_ADDR, WM8731_LLINE_IN_REG_VAL },
  { WM8731_RLINE_IN_REG_ADDR, WM8731_RLINE_IN_REG_VAL },
  { WM8731_LLINE_OUT_REG_ADDR, WM8731_LLINE_OUT_REG_VAL },
  { WM8731_RLINE_OUT_REG_ADDR, WM8731_RLINE_OUT_REG_VAL },
  { WM8731_DIGITAL_AUDIO_PATH_CTRL_REG_ADDR,
    WM8731_DIGITAL_AUDIO_PATH_CTRL_REG_VAL },
  { WM8731_ANALOG_AUDIO_PATH_CTRL_REG_ADDR,
    WM8731_ANALOG_AUDIO_PATH_CTRL_REG_VAL },
  { WM8731_SAMPLING_CTRL_REG_ADDR, WM8731_SAMPLING_CTRL_REG_VAL },
  { WM8731_ACTIVE_CTRL_REG_ADDR, WM8731_ACTIVE_CTRL_REG_VAL } };
volatile uint8_t i2c_tx_buffer_data[I2C_BUFFER_SIZE];
volatile uint8_t i2c_tx_buffer_index;
#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD) */

/* ----------------------------------------------------------------------------
 * Function      : uint32_t * Read_buffer(uint8_t side)
 * ----------------------------------------------------------------------------
 * Description   : Get pointer to the stored coded data
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
uint32_t * Read_buffer(uint8_t side)
{
    uint32_t *temp;
    w2r[side] = data_wr_idx[side] - data_rd_idx[side];

    if (w2r[side] < 0)
    {
        w2r[side] += TX_DATA_FIFO_LENGTH;
    }
    r2w[side] = TX_DATA_FIFO_LENGTH - w2r[side];

#if (DEBUG_UART_LOG)
    if (side == PKT_LEFT)
    {
        sprintf((char *)UARTTXBuffer, "%d %d %d %d\r\n", r2w[side], w2r[side],
                data_wr_idx[side], data_rd_idx[side]);
        UartLogTx(UARTTXBuffer);
    }
#endif    /* if (DEBUG_UART_LOG) */

    if ((w2r[side] < TX_FIFO_W2R_THR) || (r2w[side] < TX_FIFO_R2W_THR))
    {
        /* Reinitialize read pointer based on write pointer */
        data_rd_idx[side] = data_wr_idx[side] - TX_FIFO_RWPTR_INIT;
        data_rd_idx[side] = (data_rd_idx[side] / ENCODED_FRAME_LENGTH) * \
                            ENCODED_FRAME_LENGTH;
        if (data_rd_idx[side] < 0)
        {
            data_rd_idx[side] += TX_DATA_FIFO_LENGTH;
        }
        ptr_rst_cnt += ((w2r[side] < TX_FIFO_W2R_THR) || (r2w[side] <
                                                          TX_FIFO_R2W_THR));
    }

    temp = (uint32_t *)&tx_data_fifo[side][data_rd_idx[side]];
    data_rd_idx[side] = (data_rd_idx[side] + ENCODED_FRAME_LENGTH) %
                        TX_DATA_FIFO_LENGTH;

    return (temp);
}

/* ----------------------------------------------------------------------------
 * Function      : void DMA_IRQ_FUNC(MEMCPY_SAVE_STATE_MEM)(void)
 * ----------------------------------------------------------------------------
 * Description   : DMA channel interrupt used for saving the ASRC data into the
 *                 memory
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DMA_IRQ_FUNC(MEMCPY_SAVE_STATE_MEM)(void)
{
    if (DMA->DEST_BASE_ADDR[MEMCPY_SAVE_STATE_MEM] == \
        (uint32_t)&asrc_state_mem_rx[PKT_LEFT][0])
    {
        Sys_DMA_Set_ChannelSourceAddress(MEMCPY_RESTORE_STATE_MEM,
                                         (uint32_t)&asrc_state_mem_rx[PKT_RIGHT][0]);
    }
    else
    {
        Sys_DMA_Set_ChannelSourceAddress(MEMCPY_RESTORE_STATE_MEM,
                                         (uint32_t)&asrc_state_mem_rx[PKT_LEFT][0]);
    }
    Sys_DMA_ChannelEnable(MEMCPY_RESTORE_STATE_MEM);
}

/* ----------------------------------------------------------------------------
 * Function      : void DMA_IRQ_FUNC(MEMCPY_SAVE_STATE_MEM)(void)
 * ----------------------------------------------------------------------------
 * Description   : DMA channel interrupt used for restoring the ASRC data from
 *                 the memory
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DMA_IRQ_FUNC(MEMCPY_RESTORE_STATE_MEM)(void)
{
    if (DMA->SRC_BASE_ADDR[MEMCPY_RESTORE_STATE_MEM] == \
        (uint32_t)&asrc_state_mem_rx[PKT_RIGHT][0])
    {
        /* Re-enable DMA for ASRC output */
        Sys_DMA_ClearChannelStatus(ASRC_OUT_IDX);
        Sys_DMA_ChannelEnable(ASRC_OUT_IDX);

        /*Sys_GPIO_Set_High(9); */

        /* Re-enable ASRC input DMA and start ASRC */
        Sys_DMA_ChannelEnable(ASRC_IN_IDX);
        Sys_ASRC_StatusConfig(ASRC_ENABLE);
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void Start_Enc_Lpdsp32_Channel (PacketSide side)
 * ----------------------------------------------------------------------------
 * Description   : If the LPDSP32 is not busy look at the queues and send it to
 *                 to LPDSP32 to encode
 * Inputs        : side: The side queue to look first
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Start_Enc_Lpdsp32_Channel(PacketSide side)
{
    if (lpdsp_rdy)
    {
        uint16_t *temp = QueueFront(&queue_tx[side]);

        /* Check if the first queue has a packet */
        if (temp == NULL)
        {
            /* Switch to the other queue if this current one is empty */
            side = !side;
            temp = QueueFront(&queue_tx[side]);
        }

        /* Check if there is a packet in one of the queues send it to the
         * encoder */
        if (temp != NULL)
        {
            Start_Enc_Lpdsp32((uint32_t)temp, side);
            QueueFree(&queue_tx[side]);
        }
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void StoreDspEncData(void)
 * ----------------------------------------------------------------------------
 * Description   : Store encoded data in memory
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void StoreDspEncData(uint8_t *src_addr, PacketSide side)
{
    lpdsp_rdy = true;

    Start_Enc_Lpdsp32_Channel(!side);

    data_wr_idx[side] = (data_wr_idx[side] +
                         ENCODED_SUBFRAME_LENGTH) % TX_DATA_FIFO_LENGTH;

    /* Store LPDSP output */
    memcpy(&tx_data_fifo[side][data_wr_idx[side]],
           src_addr,
           ENCODED_SUBFRAME_LENGTH * sizeof(uint8_t));
}

/* ----------------------------------------------------------------------------
 * Function      : void DspEnc_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : DSP0 interrupt handler
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DspEnc0_isr(void)
{
    dbg_cnt_enc++;

    StoreDspEncData(    /*(uint8_t *)&coded_sample[cntr_enc0]*/ Dsp2CmBuff0enc,
                                                                PKT_LEFT);
    cntr_enc0 = (cntr_enc0 + ENCODED_SUBFRAME_LENGTH) % (4 * 60);
}

/* ----------------------------------------------------------------------------
 * Function      : void DspEnc_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : DSP0 interrupt handler
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DspEnc1_isr(void)
{
    StoreDspEncData(    /*(uint8_t *)&coded_sample[cntr_enc1]*/ Dsp2CmBuff1enc,
                                                                PKT_RIGHT);
    cntr_enc1 = (cntr_enc1 + ENCODED_SUBFRAME_LENGTH) % (4 * 60);
}

/* ----------------------------------------------------------------------------
 * Function      : void Start_Enc_Lpdsp32(uint32_t src_addr)
 * ----------------------------------------------------------------------------
 * Description   : Issue encoding start command to LPDSP32 (TX/RX)
 * Inputs        : src_addr - source address of LPDSP32 input
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Start_Enc_Lpdsp32(uint32_t src_addr, PacketSide side)
{
    lpdsp_rdy = false;

    if (side == PKT_LEFT)
    {
        memcpy(Cm2DspBuff0enc, (uint8_t *)src_addr,
               SUBFRAME_LENGTH * sizeof(uint16_t));
        SYSCTRL_DSS_CMD->DSS_CMD_0_ALIAS = DSS_CMD_0_BITBAND;
    }
    else
    {
        memcpy(Cm2DspBuff1enc, (uint8_t *)src_addr,
               SUBFRAME_LENGTH * sizeof(uint16_t));
        SYSCTRL_DSS_CMD->DSS_CMD_1_ALIAS = DSS_CMD_1_BITBAND;
    }
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
    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = PERIOD_CNT_START_BITBAND;

    /* Get ASRC output count */
    asrc_out_cnt = Sys_ASRC_OutputCount();
    Sys_ASRC_ResetOutputCount();

    /* Get audio sink phase count */
    audio_sink_phase_cnt = Sys_Audiosink_PhaseCounter();

    /* Get audio sink count */
    audio_sink_cnt  = Sys_Audiosink_Counter() << SHIFT_BIT;

    AUDIOSINK_CTRL->CNT_RESET_ALIAS = CNT_RESET_BITBAND;
    audio_sink_cnt +=
        ((((audio_sink_phase_cnt_prev - audio_sink_phase_cnt))
          << SHIFT_BIT) / audio_sink_period_cnt);

    /* store audio sink count phase for the next time */
    audio_sink_phase_cnt_prev = audio_sink_phase_cnt;

    AUDIOSINK->PHASE_CNT = 0;
    AUDIOSINK_CTRL->PHASE_CNT_START_ALIAS = PHASE_CNT_START_BITBAND;
    flag_ascc_phase = true;
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
    audio_sink_period_cnt = Sys_Audiosink_PeriodCounter() / (AUDIOSINK->CFG +
                                                             1);
    AUDIOSINK->PERIOD_CNT = 0;
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
    diff_ck_outputcnt = Cr - (asrc_out_cnt << SHIFT_BIT);
    avg_ck_outputcnt  = ((diff_ck_outputcnt - avg_ck_outputcnt) >> 10) +
                        avg_ck_outputcnt;

#if (INPUT_INTRF == PCM_RX_RAW_INPUT)
    /* audio_sink_cnt counts PCM frame-sync periods over the measurement window;
     * scale it down to the 16 kHz encoder rate the ASRC output must match. */
    Cr = audio_sink_cnt / PCM_DECIM_RATIO;
#else    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT) */
    Cr = audio_sink_cnt;
#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT) */
    Ck = FRAME_LENGTH << SHIFT_BIT;

    /* Update ASRC */
    if ((Cr <= (FRAME_LENGTH - ASRC_CFG_THR) << SHIFT_BIT) ||
        (Cr >= (FRAME_LENGTH + ASRC_CFG_THR) << SHIFT_BIT))
    {
        Cr = FRAME_LENGTH << SHIFT_BIT;
        avg_ck_outputcnt = 0;
        ptr_rst_cnt = 0;
    }

    if (asrc_cnt_prev == ASRC->PHASE_CNT)
    {
        asrc_cnt_cnst++;
        if (asrc_cnt_cnst >= 20)
        {
            Sys_ASRC_Reset();
            asrc_cnt_cnst   = 0;
            ASRC->PHASE_CNT = 0;
        }
    }
    else
    {
        asrc_cnt_cnst = 0;
    }

    asrc_cnt_prev     = ASRC->PHASE_CNT;

    /* Configure ASRC base on new Cr Ck */
    asrc_inc_carrier  = ((((Cr - Ck) << 29) / Ck) << 0);    /* - (avg_ck_outputcnt >> SHIFT_BIT); */
    asrc_inc_carrier &= 0xFFFFFFFF;
    Sys_ASRC_Config(asrc_inc_carrier, LOW_DELAY | ASRC_DEC_MODE1);
}

#if (INPUT_INTRF == PCM_RX_RAW_INPUT)
/* ----------------------------------------------------------------------------
 * Function      : int16_t Pcm_unpack_sample(uint32_t word)
 * ----------------------------------------------------------------------------
 * Description   : Convert one 32-bit PCM channel slot into a 16-bit sample.
 *                 The source sends 16-bit data MSB first, one SCLK after the
 *                 frame edge, so the sample occupies bits 30..15 of the slot.
 * Inputs        : word - one PCM slot as received
 * Outputs       : return value - the 16-bit sample
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
static int16_t Pcm_unpack_sample(uint32_t word)
{
    return ((int16_t)(word >> 15));
}

/* ----------------------------------------------------------------------------
 * Function      : void Pcm_decode_half(uint32_t word_offset, uint8_t out_offset)
 * ----------------------------------------------------------------------------
 * Description   : Average one half block of PCM slots down to PCM_HALF_SAMPLES
 *                 encoder samples per channel. Called once per half so that the
 *                 DMA can fill the other half meanwhile — the buffer is
 *                 circular, and reading the half the DMA is writing would
 *                 corrupt samples.
 * Inputs        : word_offset - first slot of the half in pcm_buf
 *                 out_offset  - first sample slot to fill in left/right_data
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
static void Pcm_decode_half(uint32_t word_offset, uint8_t out_offset)
{
    uint8_t i;
    uint8_t j;
    int32_t sum_left;
    int32_t sum_right;
    int32_t peak = 0;
    int32_t mag;

    for (i = 0; i < PCM_HALF_SAMPLES; i++)
    {
        sum_left  = 0;
        sum_right = 0;

        for (j = 0; j < PCM_DECIM_RATIO; j++)
        {
            sum_left += Pcm_unpack_sample(
                pcm_buf[word_offset + (2 * ((i * PCM_DECIM_RATIO) + j))]);
            sum_right += Pcm_unpack_sample(
                pcm_buf[word_offset + (2 * ((i * PCM_DECIM_RATIO) + j)) + 1]);
        }

        left_data[out_offset + i]  = (int16_t)(sum_left / PCM_DECIM_RATIO);
        right_data[out_offset + i] = (int16_t)(sum_right / PCM_DECIM_RATIO);

        mag = left_data[out_offset + i] < 0
              ? -left_data[out_offset + i] : left_data[out_offset + i];
        if (mag > peak)
        {
            peak = mag;
        }
        mag = right_data[out_offset + i] < 0
              ? -right_data[out_offset + i] : right_data[out_offset + i];
        if (mag > peak)
        {
            peak = mag;
        }
    }

    dbg_pcm_peak = peak;
}
#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT) */

#if (TX_TONE_TEST)
/* One full cycle of a 1 kHz tone at 16 kHz — exactly one encoder subframe, so
 * every subframe queued is identical and the concatenation stays continuous
 * (the last sample is -3061 and the next subframe starts at 0). Amplitude is
 * about -12 dBFS; raise it if the tone comes out too quiet. */
const int16_t tone_1k[SUBFRAME_LENGTH] = {
       0,   3061,   5657,   7391,
    8000,   7391,   5657,   3061,
       0,  -3061,  -5657,  -7391,
   -8000,  -7391,  -5657,  -3061
};

/* ----------------------------------------------------------------------------
 * Function      : void Tone_feed_encoder(uint8_t side, uint8_t subframes)
 * ----------------------------------------------------------------------------
 * Description   : Queue `subframes` copies of the 1 kHz tone and let the encoder
 *                 drain them. Called once per RM payload request, so what the
 *                 encoder produces is what the next request reads.
 * Inputs        : side      - PKT_LEFT or PKT_RIGHT
 *                 subframes - number of subframes to queue
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Tone_feed_encoder(uint8_t side, uint8_t subframes)
{
    uint8_t i;

    for (i = 0; i < subframes; i++)
    {
        QueueInsert(&queue_tx[side], (uint16_t *)&tone_1k[0]);
    }

    Start_Enc_Lpdsp32_Channel(side);
}
#endif    /* if (TX_TONE_TEST) */

/* ----------------------------------------------------------------------------
 * Function      : void Port_rx_raw_dma_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : SPI and PCM receive interrupt handler
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Port_rx_raw_dma_isr(void)
{
    dbg_cnt_pcm_isr++;

#if (INPUT_INTRF == SPI_RX_RAW_INPUT)
    uint8_t i;

    flag_packet_side = PKT_LEFT;

    /* E7100 sends the audio stream in 4 samples blocks.
     * The left and right data is seperated here*/
    for (i = 0; i < SUBFRAME_LENGTH_LEFT_AND_RIGHT; i += 2 * 4)
    {
        memcpy(&asrc_in_buf[SUBFRAME_LENGTH + (i >> 1)],
               &spi_buf[i + 4],
               4 * sizeof(int16_t));
        memcpy(&asrc_in_buf[i >> 1],
               &spi_buf[i],
               4 * sizeof(int16_t));
    }

    Sys_DMA_Set_ChannelSourceAddress(ASRC_IN_IDX,
                                     (uint32_t)&asrc_in_buf[0]);

    /* Re-enable DMA for ASRC output */
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);

    /* Re-enable ASRC input DMA and start ASRC */
    Sys_DMA_ChannelEnable(ASRC_IN_IDX);
    Sys_ASRC_StatusConfig(ASRC_ENABLE);
#else    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT) */

#if (TX_TONE_TEST)
    /* The tone test feeds the encoder from the RM payload callback instead, so
     * the PCM data must not be queued as well. */
#else    /* if (TX_TONE_TEST) */
    /* The DMA buffer is circular, so each half is decoded while the DMA fills
     * the other one: the counter interrupt marks the end of the first half, the
     * complete interrupt the end of the second. Only the second half completes
     * the subframe, so the encoder is fed there. The ASRC is deliberately not
     * used — it shares queue_tx with this path and would interleave its own
     * output into the encoder queue. */
    if ((Sys_DMA_Get_ChannelStatus(RX_DMA_NUM) & DMA_COUNTER_INT_STATUS) != 0)
    {
        Pcm_decode_half(0, 0);
    }
    else
    {
        Pcm_decode_half(PCM_DMA_HALF_WORDS, PCM_HALF_SAMPLES);

        QueueInsert(&queue_tx[PKT_LEFT], (uint16_t *)&left_data[0]);
        Start_Enc_Lpdsp32_Channel(PKT_LEFT);

        QueueInsert(&queue_tx[PKT_RIGHT], (uint16_t *)&right_data[0]);
        Start_Enc_Lpdsp32_Channel(PKT_RIGHT);
    }

    cntr_wav = (cntr_wav + SUBFRAME_LENGTH) % 160;
#endif    /* if (TX_TONE_TEST) */
#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT) */
    Sys_DMA_ClearChannelStatus(RX_DMA_NUM);
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
    Sys_ASRC_StatusConfig(ASRC_DISABLE);
    Sys_DMA_ClearChannelStatus(ASRC_IN_IDX);
}

/* ----------------------------------------------------------------------------
 * Function      : void Asrc_out_dma_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : ASRC output DMA interrupt handler (TX/RX)
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Asrc_out_dma_isr(void)
{
    /*static uint8_t cntr_asrc[2] = {0, 0}; */
    dbg_cnt_asrc_out++;
    asrc_out_buf[flag_packet_side][cntr_asrc_out[flag_packet_side]]
        = data_fifo_rec;

    /* sample_in[cntr_asrc[flag_packet_side]];
     * cntr_asrc[flag_packet_side] = (cntr_asrc[flag_packet_side] + 1) % 160; */

    cntr_asrc_out[flag_packet_side]++;

    if (cntr_asrc_out[flag_packet_side] == SUBFRAME_LENGTH)
    {
        cntr_asrc_out[flag_packet_side] = 0;
        QueueInsert(&queue_tx[flag_packet_side],
                    (uint16_t *)&asrc_out_buf[flag_packet_side][0]);

        Start_Enc_Lpdsp32_Channel(flag_packet_side);
    }

    Sys_DMA_ClearChannelStatus(ASRC_OUT_IDX);
    if (ASRC_CTRL->ASRC_PROC_STATUS_ALIAS == ASRC_BUSY_BITBAND)
    {
        /* Re-enable DMA for ASRC output */
        Sys_DMA_ChannelEnable(ASRC_OUT_IDX);
    }
    else
    {
        /* Configure ASRC if ASCC phase interrupt has been handled */
        if (flag_ascc_phase)
        {
            Asrc_reconfig();
            asrc_state_mem_rx[PKT_LEFT][0]  = ASRC->PHASE_INC;
            asrc_state_mem_rx[PKT_RIGHT][0] = ASRC->PHASE_INC;
            flag_ascc_phase = false;
        }

        if (flag_packet_side == PKT_LEFT)
        {
            Sys_DMA_Set_ChannelSourceAddress(ASRC_IN_IDX,
                                             (uint32_t)&asrc_in_buf[
                                                 SUBFRAME_LENGTH]);
        }

        Sys_DMA_Set_ChannelDestAddress(MEMCPY_SAVE_STATE_MEM,
                                       (uint32_t)&asrc_state_mem_rx[
                                           flag_packet_side][0]);
        Sys_DMA_ChannelEnable(MEMCPY_SAVE_STATE_MEM);
        flag_packet_side = PKT_RIGHT;
    }
}

#if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD)
/* ----------------------------------------------------------------------------
 * Function      : void I2C_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : Handle the interrupts of I2C
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void I2C_IRQHandler(void)
{
    uint32_t i2c_status = Sys_I2C_Get_Status();

    if ((i2c_status & (1 << I2C_STATUS_READ_WRITE_Pos)) == I2C_IS_WRITE)
    {
        if ((i2c_status & (1 << I2C_STATUS_ACK_STATUS_Pos)) == I2C_HAS_ACK)
        {
            if (i2c_tx_buffer_index < I2C_BUFFER_SIZE)
            {
                I2C->DATA = i2c_tx_buffer_data[i2c_tx_buffer_index++];
            }
            else
            {
                I2C_CTRL1->LAST_DATA_ALIAS = I2C_LAST_DATA_BITBAND;
            }
        }
    }
}

#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD) */

#elif (INPUT_INTRF == SPI_RX_CODED_INPUT)

void __attribute__ ((alias("port_rx_coded_dma_isr"))) DMA_IRQ_FUNC(RX_DMA_NUM)(
    void);

int8_t spi_buf[AUDIO_FRAME_SIZE * 2];
/* ----------------------------------------------------------------------------
 * Function      : void port_rx_coded_dma_isr(void)
 * ----------------------------------------------------------------------------
 * Description   : SPI receive interrupt handler
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void port_rx_coded_dma_isr(void)
{
    uint8_t length = AUDIO_FRAME_SIZE;
    Sys_DMA_ClearChannelStatus(RX_DMA_NUM);
    RM_EventHandler(RM_TX_PAYLOAD_READY_LEFT, &length, (uint8_t *)&spi_buf[0]);
    RM_EventHandler(RM_TX_PAYLOAD_READY_RIGHT, &length,
                    (uint8_t *)&spi_buf[length]);
}

#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT) */
