/* ----------------------------------------------------------------------------
 * asha_audio.h — ASHA G.722 PLC audio pipeline definitions
 * ------------------------------------------------------------------------- */

#ifndef ASHA_AUDIO_H
#define ASHA_AUDIO_H

#include <rsl10.h>
#include <stdlib.h>
#include <stdbool.h>
#include "codecs/codec.h"
#include "codecs/baseDSP/baseDSPCodec.h"
#include "codecs/G722PLCDSP/g722_PLC_DSPCodec.h"
#include "sharedBuffers.h"

#define THREE_BLOCK_APPN(x, y, z)  x##y##z
#define DMA_IRQn(x)                THREE_BLOCK_APPN(DMA, x, _IRQn)
#define TIMER_IRQn(x)              THREE_BLOCK_APPN(TIMER, x, _IRQn)
#define ASHA_DMA_IRQ_FUNC(x)       THREE_BLOCK_APPN(DMA, x, _IRQHandler)
#define ASHA_TIMER_IRQ_FUNC(x)     THREE_BLOCK_APPN(TIMER, x, _IRQHandler)

/* G.722 PLC codec parameters (64 kbps, MODE 1) */
#ifndef ASHA_SUBFRAME_LENGTH
#define ASHA_SUBFRAME_LENGTH             4
#endif
#ifndef ASHA_FRAME_LENGTH
#define ASHA_FRAME_LENGTH                160
#endif
#ifndef ASHA_ENCODED_FRAME_LENGTH
#define ASHA_ENCODED_FRAME_LENGTH        (ASHA_FRAME_LENGTH / 2)
#endif
#ifndef ASHA_ENCODED_SUBFRAME_LENGTH
#define ASHA_ENCODED_SUBFRAME_LENGTH     (ASHA_SUBFRAME_LENGTH / 2)
#endif
#ifndef ASHA_CODEC_MODE
#define ASHA_CODEC_MODE                  1
#endif

#define AUDIO_INTV_PERIOD           10000
#define RENDER_TIME_US              7500
#define FRAME_TX_END_TIME_US        200
#define ASHA_ASRC_CFG_THR           20
#define ASHA_SHIFT_BIT              20
#define ASHA_SIMUL                  0

#define AUDIO_START_BUFFERED_FRAMES 16
#define CONN_TIMES                  50

/* DMA channels */
#define ASRC_IN_IDX                 3
#define ASRC_OUT_IDX                4

/* Timers — avoid RM's TIMER_REGUL=2. Use TIMER3 instead of TIMER4 */
#define ASHA_TIMER_FRAME_TX_END     3
#define ASHA_TIMER_RENDER           3  /* Reuse TIMER3 for both roles (shot + free-run handled by TIMER_CFG) */

/* Enums — prefixed ASHA_ to avoid conflict with rm_pkt.h */
typedef enum { PKT_LEFT = 0, PKT_RIGHT = 1 } AshaPacketSide;
typedef enum { ASHA_DSP_STARTING = 0, ASHA_DSP_IDLE, ASHA_DSP_BUSY } AshaDSP_State;
typedef enum { ASHA_LINK_DISCONNECTED = 0, ASHA_LINK_TRANSIENT, ASHA_LINK_ESTABLISHED } AshaLink_State;
typedef enum { ASHA_GOOD_PACKET = 0, ASHA_BAD_PACKET, ASHA_BAD_CONSECUTIVE_PACKET } AshaPacket_State;

typedef struct {
    CODEC codec;
    AshaDSP_State state;
    OperationParameters channels;
    unsigned char *incoming;
    unsigned char *outgoing;
} AshaLPDSP32Context;

typedef struct {
    int64_t Cr;
    int64_t Ck;
    int64_t Ck_prev;
    int64_t audio_sink_cnt;
    int64_t audio_sink_period_cnt;
    int64_t audio_sink_phase_cnt;
    int64_t audio_sink_phase_cnt_prev;
    bool flag_ascc_phase;
    int64_t avg_ck_outputcnt;
    uint32_t asrc_cnt_cnst;
    bool phase_cnt_missed;
    uint8_t cntr_connection;
    uint8_t cntr_transient;
    bool timer_free_run;
    uint16_t samples_per_packet;
} asha_sync_param_t;

typedef struct {
    uint8_t frame_idx;
    uint8_t frame_in_prev[ASHA_ENCODED_FRAME_LENGTH];
    uint8_t *frame_in;
    AshaLink_State state;
    int16_t *frame_dec;
    AshaPacket_State packet_state;
    bool proc;
} asha_audio_frame_param_t;

extern asha_sync_param_t asha_sync;
extern asha_audio_frame_param_t asha_audio_env;
extern AshaLPDSP32Context asha_lpdsp32;

#endif /* ASHA_AUDIO_H */
