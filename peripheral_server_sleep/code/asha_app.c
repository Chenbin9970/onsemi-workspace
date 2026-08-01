/* ----------------------------------------------------------------------------
 * asha_app.c — ASHA audio pipeline (G.722 PLC, OD output)
 * ------------------------------------------------------------------------- */

#include <rsl10.h>
#include <rsl10_ke.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "asha_audio.h"
#include "asha_app.h"
#include "ble_asha.h"
#include "asha_queue.h"

/* Globals */
bool                asha_active = false;
asha_sync_param_t   asha_sync;
asha_audio_frame_param_t asha_audio_env;
AshaLPDSP32Context  asha_lpdsp32;

static queue_t      audio_queue;
static int8_t       volume_shift = 0;
static bool         sequence_started = false;
static uint8_t      expected_seq_num = 0;

extern int16_t      BufferOut[];

/* ----------------------------------------------------------------------------
 * Volume
 * ------------------------------------------------------------------------- */
void Volume_Set(int8_t vol)
{
    if (vol > 0) vol = 0;
    if (vol < -16) vol = -16;
    volume_shift = (uint8_t)(-vol);
}

static void Volume_Shift_Subframe(int16_t *src)
{
    if (volume_shift == 0) return;
    for (int i = 0; i < ASHA_SUBFRAME_LENGTH; i++)
        src[i] >>= volume_shift;
}

/* ----------------------------------------------------------------------------
 * ASRC reconfig
 * ------------------------------------------------------------------------- */
static void asha_asrc_reconfig(void)
{
    int64_t Cr = asha_sync.Cr;
    int64_t Ck = asha_sync.Ck;
    if (Cr <= 0 || Ck <= 0) return;

    int64_t diff = Cr - Ck;
    if (diff < 0) diff = -diff;
    if (diff > (ASHA_ASRC_CFG_THR << ASHA_SHIFT_BIT)) return;

    uint32_t inc = (uint32_t)((Cr << ASHA_SHIFT_BIT) / Ck);
    ASRC->PHASE_INC = inc;
}

/* ----------------------------------------------------------------------------
 * DSP0_IRQHandler
 * ------------------------------------------------------------------------- */
void DSP0_IRQHandler(void)
{
    if (asha_lpdsp32.state == ASHA_DSP_IDLE)
    {
        asha_lpdsp32.state = ASHA_DSP_BUSY;
        codecSetOutputBuffer(asha_lpdsp32.codec, asha_lpdsp32.outgoing,
                             ASHA_FRAME_LENGTH * 2);
        codecDecode(asha_lpdsp32.codec);
        return;
    }

    Volume_Shift_Subframe((int16_t *)asha_lpdsp32.outgoing);
    asha_audio_env.frame_dec = (int16_t *)asha_lpdsp32.outgoing;

    Sys_DMA_ChannelDisable(ASRC_IN_IDX);
    Sys_DMA_ChannelConfig(ASRC_IN_IDX, RX_DMA_ASRC_IN,
                          ASHA_SUBFRAME_LENGTH, 0,
                          (uint32_t)asha_lpdsp32.outgoing,
                          (uint32_t)&ASRC->IN);
    Sys_DMA_ChannelEnable(ASRC_IN_IDX);

    Sys_ASRC_StatusConfig(ASRC_ENABLE);
    asha_audio_env.proc = true;
}

/* ----------------------------------------------------------------------------
 * Timer ISR (TIMER3)
 * ------------------------------------------------------------------------- */
void TIMER3_IRQHandler(void)
{
    uint8_t seq_num;
    AshaPacket_State pkt_state;
    uint8_t frag_num;
    uint8_t *frame_data = AshaQueueFront(&audio_queue, &seq_num, &pkt_state, &frag_num);

    if (frame_data == NULL)
    {
        memcpy(asha_lpdsp32.incoming, asha_audio_env.frame_in_prev,
               ASHA_ENCODED_FRAME_LENGTH);
        asha_audio_env.packet_state = ASHA_BAD_PACKET;
    }
    else
    {
        memcpy(asha_lpdsp32.incoming, frame_data, ASHA_ENCODED_FRAME_LENGTH);
        memcpy(asha_audio_env.frame_in_prev, frame_data, ASHA_ENCODED_FRAME_LENGTH);
        asha_audio_env.packet_state = pkt_state;
        AshaQueueFree(&audio_queue);
    }

    asha_audio_env.frame_in = asha_lpdsp32.incoming;
    asha_audio_env.frame_idx = 0;
    asha_audio_env.proc = false;

    if (asha_audio_env.packet_state == ASHA_BAD_PACKET)
        asha_lpdsp32.channels.action = PACKED | DECODE_PLC;
    else
        asha_lpdsp32.channels.action = PACKED | DECODE;

    codecSetParameters(asha_lpdsp32.codec, &asha_lpdsp32.channels);
    codecSetInputBuffer(asha_lpdsp32.codec, asha_lpdsp32.incoming,
                        ASHA_ENCODED_FRAME_LENGTH);
    asha_lpdsp32.state = ASHA_DSP_IDLE;
    codecDecode(asha_lpdsp32.codec);
}

/* ----------------------------------------------------------------------------
 * DMA3_IRQHandler
 * ------------------------------------------------------------------------- */
void DMA3_IRQHandler(void)
{
    if (!asha_audio_env.proc) return;
    asha_audio_env.frame_idx++;

    if (asha_audio_env.frame_idx < (ASHA_FRAME_LENGTH / ASHA_SUBFRAME_LENGTH))
    {
        int16_t *next_sub = &asha_audio_env.frame_dec[asha_audio_env.frame_idx *
                                                       ASHA_SUBFRAME_LENGTH];
        Volume_Shift_Subframe(next_sub);

        Sys_DMA_ChannelDisable(ASRC_IN_IDX);
        Sys_DMA_ChannelConfig(ASRC_IN_IDX, RX_DMA_ASRC_IN,
                              ASHA_SUBFRAME_LENGTH, 0,
                              (uint32_t)next_sub,
                              (uint32_t)&ASRC->IN);
        Sys_DMA_ChannelEnable(ASRC_IN_IDX);
    }
    else
    {
        asha_audio_env.proc = false;
        Sys_Timer_Set_Control(ASHA_TIMER_FRAME_TX_END,
            TIMER_SHOT_MODE | (FRAME_TX_END_TIME_US - 1));
    }
}

/* ----------------------------------------------------------------------------
 * AUDIOSINK handlers
 * ------------------------------------------------------------------------- */
void AUDIOSINK_PHASE_IRQHandler(void)
{
    asha_sync.audio_sink_phase_cnt_prev = asha_sync.audio_sink_phase_cnt;
    asha_sync.audio_sink_phase_cnt = AUDIOSINK->PHASE_CNT;

    if (!asha_sync.phase_cnt_missed)
        asha_sync.audio_sink_cnt +=
            (asha_sync.audio_sink_phase_cnt - asha_sync.audio_sink_phase_cnt_prev);
    asha_sync.phase_cnt_missed = false;
    asha_sync.flag_ascc_phase  = true;
}

void AUDIOSINK_PERIOD_IRQHandler(void)
{
    asha_sync.audio_sink_period_cnt = AUDIOSINK->PERIOD_CNT;
}

/* ----------------------------------------------------------------------------
 * ASHA callback
 * ------------------------------------------------------------------------- */
void APP_ASHA_CallbackHandler(uint8_t op_code, void *param)
{
    switch (op_code)
    {
    case ASHA_AUDIO_START: {
        struct asha_audio_start *start = (struct asha_audio_start *)param;
        Volume_Set(start->volume);
        APP_ResetPrevSeqNumber();
        asha_audio_env.state = ASHA_LINK_TRANSIENT;
        asha_sync.cntr_transient = 0;
        break;
    }
    case ASHA_AUDIO_STOP:
        APP_Audio_Disconnect();
        break;
    case ASHA_AUDIO_RCVD: {
        struct asha_audio_received *rcv = (struct asha_audio_received *)param;
        APP_Audio_Transfer(rcv->data, rcv->length, rcv->seq_number);
        break;
    }
    case ASHA_VOLUME_CHANGE: {
        struct asha_volume_change *vol = (struct asha_volume_change *)param;
        Volume_Set(vol->volume);
        break;
    }
    case ASHA_AUDIO_STATUS:
        break;
    }
}

void APP_ResetPrevSeqNumber(void)
{
    sequence_started = false;
    expected_seq_num = 0;
}

void APP_Audio_Transfer(uint8_t *data, uint16_t length, uint8_t seq_num)
{
    ASHA_AddCredits(1);
    if (asha_audio_env.state == ASHA_LINK_DISCONNECTED) return;

    if (sequence_started)
    {
        int16_t missed = (int16_t)seq_num - (int16_t)expected_seq_num;
        if (missed < 0) missed += 256;
        while (missed > 0)
        {
            AshaQueueInsert(&audio_queue, NULL, ASHA_BAD_PACKET, expected_seq_num, 0);
            expected_seq_num++;
            missed--;
        }
    }

    AshaPacket_State pkt_state = (length == ASHA_ENCODED_FRAME_LENGTH) ?
                                  ASHA_GOOD_PACKET : ASHA_BAD_PACKET;
    AshaQueueInsert(&audio_queue, data, pkt_state, seq_num, 0);

    if (!sequence_started) { sequence_started = true; expected_seq_num = seq_num; }
    expected_seq_num++;

    if (asha_audio_env.state == ASHA_LINK_TRANSIENT &&
        AshaQueueCount(&audio_queue) >= AUDIO_START_BUFFERED_FRAMES)
        APP_Audio_Start();
}

void APP_Audio_Start(void)
{
    Sys_ASRC_StatusConfig(ASRC_DISABLE);

    NVIC_ClearPendingIRQ(DSP0_IRQn);
    NVIC_EnableIRQ(DSP0_IRQn);
    NVIC_ClearPendingIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_ClearPendingIRQ(AUDIOSINK_PERIOD_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);
    NVIC_ClearPendingIRQ(DMA_IRQn(ASRC_IN_IDX));
    NVIC_EnableIRQ(DMA_IRQn(ASRC_IN_IDX));

    Sys_Timer_Set_Control(ASHA_TIMER_RENDER, TIMER_SHOT_MODE | (RENDER_TIME_US - 1));
    NVIC_ClearPendingIRQ(TIMER_IRQn(ASHA_TIMER_RENDER));
    NVIC_EnableIRQ(TIMER_IRQn(ASHA_TIMER_RENDER));

    asha_audio_env.state = ASHA_LINK_ESTABLISHED;
    asha_sync.cntr_connection = 0;
    asha_sync.timer_free_run = false;
}

void APP_Audio_Disconnect(void)
{
    NVIC_DisableIRQ(DSP0_IRQn);
    NVIC_DisableIRQ(TIMER_IRQn(ASHA_TIMER_RENDER));
    NVIC_DisableIRQ(DMA_IRQn(ASRC_IN_IDX));
    NVIC_DisableIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_DisableIRQ(AUDIOSINK_PERIOD_IRQn);

    Sys_ASRC_StatusConfig(ASRC_DISABLE);
    AshaQueueFlush(&audio_queue);
    AshaQueueInit(&audio_queue);

    asha_audio_env.state     = ASHA_LINK_DISCONNECTED;
    asha_audio_env.proc      = false;
    asha_audio_env.frame_idx = 0;
    sequence_started         = false;
}

/* ----------------------------------------------------------------------------
 * ASHA_App_Init
 * ------------------------------------------------------------------------- */
void ASHA_App_Init(void)
{
    memset(&asha_lpdsp32, 0, sizeof(asha_lpdsp32));
    asha_lpdsp32.state = ASHA_DSP_STARTING;

    asha_lpdsp32.channels.frameSize      = ASHA_FRAME_LENGTH;
    asha_lpdsp32.channels.blockSize      = ASHA_SUBFRAME_LENGTH;
    asha_lpdsp32.channels.modeAndChannel = 0x00 | ASHA_CODEC_MODE;
    asha_lpdsp32.channels.sampleRate     = 0;

    asha_lpdsp32.codec = populateG722PLCDSPCodec(Buffer.configuration, 0x200);
    codecSetOutputBuffer(asha_lpdsp32.codec, Buffer.output,
                         CODEC_OUTPUT_SIZE);
    codecSetInputBuffer(asha_lpdsp32.codec, Buffer.input[0],
                        CODEC_INPUT_SIZE);
    codecSetStatusBuffer(asha_lpdsp32.codec, Buffer.configuration,
                         CODEC_CONFIGURATION_SIZE);

    asha_lpdsp32.incoming = Buffer.input[0];
    asha_lpdsp32.outgoing = Buffer.output;

    codecInitialise(asha_lpdsp32.codec);
    dspHandshake(asha_lpdsp32.codec);

    asha_lpdsp32.state = ASHA_DSP_IDLE;
    asha_lpdsp32.channels.action = PACKED | DECODE_RESET | DECODE;
    codecSetParameters(asha_lpdsp32.codec, &asha_lpdsp32.channels);

    /* OD config */
    Sys_Clocks_SystemClkPrescale1(AUDIOCLK_PRESCALE_5);
    Sys_Audio_Set_Config(AUDIO_CONFIG);
    AUDIO->OD_CFG = DCRM_CUTOFF_240HZ | DITHER_ENABLE;
    AUDIO->OD_GAIN = 0x200;
    Sys_DIO_Config(OD_P_DIO, DIO_MODE_OD_P | DIO_6X_DRIVE);
    Sys_DIO_Config(OD_N_DIO, DIO_MODE_OD_N | DIO_6X_DRIVE);

    /* Audio Sink */
    Sys_Audiosink_InputClock(0, ((uint32_t)(SAMPL_CLK << DIO_AUDIOSINK_SRC_CLK_Pos)));
    AUDIOSINK_CTRL->PHASE_CNT_START_ALIAS  = PHASE_CNT_START_BITBAND;
    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = PERIOD_CNT_START_BITBAND;

    /* Interrupt priorities */
    NVIC_SetPriority(DSP0_IRQn,                    4);
    NVIC_SetPriority(TIMER_IRQn(ASHA_TIMER_RENDER), 1);
    NVIC_SetPriority(DMA_IRQn(ASRC_IN_IDX),        0);
    NVIC_SetPriority(AUDIOSINK_PHASE_IRQn,         0);
    NVIC_SetPriority(AUDIOSINK_PERIOD_IRQn,        0);

    /* OD DMA: BufferOut → OD_DATA */
    Sys_DMA_ChannelDisable(OD_DMA_NUM);
    Sys_DMA_ChannelConfig(OD_DMA_NUM, RX_DMA_OD,
                          2 * ASHA_FRAME_LENGTH, 0,
                          (uint32_t)&BufferOut[0],
                          (uint32_t)&AUDIO->OD_DATA);
    Sys_DMA_ChannelEnable(OD_DMA_NUM);

    /* ASRC_OUT DMA: ASRC → BufferOut */
    Sys_DMA_ChannelDisable(ASRC_OUT_IDX);
    Sys_DMA_ChannelConfig(ASRC_OUT_IDX, RX_DMA_ASRC_OUT,
                          2 * ASHA_FRAME_LENGTH, 0,
                          (uint32_t)&ASRC->OUT,
                          (uint32_t)&BufferOut[0]);
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);

    AshaQueueInit(&audio_queue);
    memset(&asha_sync, 0, sizeof(asha_sync));
    memset(&asha_audio_env, 0, sizeof(asha_audio_env));
    asha_audio_env.state = ASHA_LINK_DISCONNECTED;

    asha_active = true;
}

void ASHA_App_Deinit(void)
{
    APP_Audio_Disconnect();

    Sys_DMA_ChannelDisable(OD_DMA_NUM);
    Sys_DMA_ChannelDisable(ASRC_OUT_IDX);

    AUDIO->OD_CFG = 0;
    Sys_Audio_Set_Config(0);
    SYSCTRL->DSS_CTRL = DSS_LPDSP32_PAUSE;

    asha_active = false;
}

void ASHA_App_Process(void) { }
