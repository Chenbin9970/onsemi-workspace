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
 * app_audio.h
 * - Audio application header file
 * ------------------------------------------------------------------------- */

#ifndef APP_AUDIO_H_
#define APP_AUDIO_H_

/* ----------------------------------------------------------------------------
 * Include files
 * --------------------------------------------------------------------------*/
#include <rsl10.h>
#include <stdlib.h>
#include <stdbool.h>
#include <dsp_pm_dm.h>
#include "codecs/codec.h"
#include "codecs/baseDSP/baseDSPCodec.h"
#include "codecs/G722PLCDSP/g722_PLC_DSPCodec.h"
#include "sharedBuffers.h"

/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

#define THREE_BLOCK_APPN(x, y, z)       x##y##z
#define DMA_IRQn(x)                     THREE_BLOCK_APPN(DMA, x, _IRQn)
#define TIMER_IRQn(x)                   THREE_BLOCK_APPN(TIMER, x, _IRQn)
#define DMA_IRQ_FUNC(x)                 THREE_BLOCK_APPN(DMA, x, _IRQHandler)
#define TIMER_IRQ_FUNC(x)               THREE_BLOCK_APPN(TIMER, x, _IRQHandler)

/* Sub-frame length in uint16_t */
#define SUBFRAME_LENGTH                 4

/* Frame length in uint16_t */
#define FRAME_LENGTH                    160

/* Valid for 64 kbps @ 10 ms intervals */

/* Encoded frame length in uint8_t */
#define ENCODED_FRAME_LENGTH            (FRAME_LENGTH / 2)

/* Encoded sub-frame length in uint8_t */
#define ENCODED_SUBFRAME_LENGTH         (SUBFRAME_LENGTH / 2)

#define NO_TX_OUTPUT                    0
#define SPI_TX_OUTPUT                   1

#define OUTPUT_INTRF                    SPI_TX_OUTPUT

#if (OUTPUT_INTRF == SPI_TX_OUTPUT)
#else
#error "Not Implemented"
#endif    /* if (OUTPUT_INTRF == SPI_TX_OUTPUT) */

#define CONCAT(x, y)                    x##y
#define DIO_SRC(x)                      CONCAT(DIO_SRC_DIO_, x)
#define BUTTON_DIO                      5

#if EZAIRO_71XX_DIO_CFG == 7100
	#define SER_DI                      2
	#define SER_DO                      1
	#define CLK_DO                      3
	#define CS_DO                       0
	#define SAMPL_CLK                   13
	#define RF_INT                      14

    /* DIO that is used to check ASCC phase ISR */
    #define ASCC_PHASE_ISR_DIO          10

    /* Audio event DIO */
    #define ASHA_EVENT_DIO              11

#elif EZAIRO_71XX_DIO_CFG == 7160
	#define SER_DI                      14
	#define SER_DO                      1
	#define CLK_DO                      3
	#define CS_DO                       0
	#define SAMPL_CLK                   2
	#define RF_INT                      13

    /* The following two DIOs are re-mapped for 7160 because
     * DIOs 10 and 11 are connected directly to Ezairo DIOs,
     * which can cause problems.
     */

    /* DIO that is used to check ASCC phase ISR */
    #define ASCC_PHASE_ISR_DIO          5

    /* DIO that is used to check ASHA events */
    #define ASHA_EVENT_DIO              4
#else
#error "EZAIRO_71XX_DIO_CFG not defined."
#endif /* EZAIRO_71XX_DIO_CFG */


/* Connection interval times used to change the state of the audio path
 * Disconnect -> Transient -> Established */
#define CONN_TIMES                      50

#define PACK_NUM_QUEUE                  4

/* Number of frames to buffer before enabling the audio path for processing */
#define AUDIO_START_BUFFERED_FRAMES     16

/* DMA channels */
#define ASRC_IN_IDX                     3
#if (OUTPUT_INTRF == SPI_TX_OUTPUT)
#define ASRC_OUT_IDX                    4
#endif

typedef enum
{
    PKT_LEFT = 0,
    PKT_RIGHT = 1
} PacketSide;

typedef enum
{
    DSP_STARTING = 0,
    DSP_IDLE,
    DSP_BUSY
} DSP_State;

typedef enum
{
    LINK_DISCONNECTED = 0,
    LINK_TRANSIENT,
    LINK_ESTABLISHED
} Link_State;

typedef enum
{
    GOOD_PACKET = 0,
    BAD_PACKET,
    BAD_CONSECUTIVE_PACKET
} Packet_State;

typedef struct
{
    CODEC codec;
    DSP_State state;
    OperationParameters channels;
    unsigned char *incoming;
    unsigned char *outgoing;
} LPDSP32Context;

typedef struct
{
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
} sync_param_t;

typedef struct
{
    uint8_t frame_idx;
    uint8_t frame_in_prev[ENCODED_FRAME_LENGTH];
    uint8_t *frame_in;
    Link_State state;
    int16_t *frame_dec;
    Packet_State packet_state;
    bool proc;
} audio_frame_param_t;

extern sync_param_t env_sync;
extern audio_frame_param_t env_audio;

/* Define a LPDSP32 Context */
extern LPDSP32Context lpdsp32;

/* LPDSP32 CODEC related defines */
#define CODEC_MODE              1

/* Audio interval period in micro-seconds */
#define AUDIO_INTV_PERIOD       10000

/* Time to wait before disabling ASRC and resetting SPI CS after the ASRC
 * has complete a frame
 */
#define FRAME_TX_END_TIME_US    200

/* Rendering time uses in micro-seconds */
#define RENDER_TIME_US          7500

/* Audio interval simulation time uses in micro-seconds */
#define SIMUL_TIME_US           10000

/* Threshold for valid Cr/Ck distance */
#define ASRC_CFG_THR            20

#define SIMUL                   0

#define TIMER_SIMUL             1
#define TIMER_FRAME_TX_END      2
#define TIMER_RENDER            3

/* DMA for ASRC input on RX side */
#define RX_DMA_ASRC_IN          (DMA_DEST_ASRC                  | \
                                 DMA_TRANSFER_M_TO_P            | \
                                 DMA_LITTLE_ENDIAN              | \
                                 DMA_COMPLETE_INT_ENABLE        | \
                                 DMA_COUNTER_INT_DISABLE        | \
                                 DMA_DEST_WORD_SIZE_16          | \
                                 DMA_SRC_WORD_SIZE_32           | \
                                 DMA_SRC_ADDR_INC               | \
                                 DMA_DEST_ADDR_STATIC           | \
                                 DMA_ADDR_LIN                   | \
                                 DMA_DISABLE)

/* DMA for ASRC output on RX side */
#define RX_DMA_ASRC_OUT         (DMA_SRC_ASRC                   | \
                                 DMA_DEST_SPI0                  | \
                                 DMA_TRANSFER_P_TO_P            | \
                                 DMA_LITTLE_ENDIAN              | \
                                 DMA_COMPLETE_INT_DISABLE       | \
                                 DMA_COUNTER_INT_DISABLE        | \
                                 DMA_DEST_WORD_SIZE_16          | \
                                 DMA_SRC_WORD_SIZE_16           | \
                                 DMA_SRC_ADDR_STATIC            | \
                                 DMA_DEST_ADDR_STATIC           | \
                                 DMA_ADDR_CIRC                  | \
                                 DMA_DISABLE)

/* DMA for ASRC input on RX side */
#define TX_DMA_SPI              (DMA_DEST_SPI0                  | \
                                 DMA_TRANSFER_M_TO_P            | \
                                 DMA_LITTLE_ENDIAN              | \
                                 DMA_COMPLETE_INT_DISABLE       | \
                                 DMA_COUNTER_INT_DISABLE        | \
                                 DMA_DEST_WORD_SIZE_16          | \
                                 DMA_SRC_WORD_SIZE_32           | \
                                 DMA_SRC_ADDR_INC               | \
                                 DMA_DEST_ADDR_STATIC           | \
                                 DMA_ADDR_LIN                   | \
                                 DMA_DISABLE)

/* the number of shifts of ASRC registers for using fix point variables */
#define SHIFT_BIT                20

/* ----------------------------------------------------------------------------
 * Function prototype definitions
 * --------------------------------------------------------------------------*/
void Reset_Audio_Sync_Param(sync_param_t *sync_param, audio_frame_param_t *audio_param);

void asrc_reconfig(sync_param_t *sync_param);

void LEA_Event_Handler(uint8_t *rxBuf, uint8_t *txBuf, bool invalid_rx,
                       bool eventType);

void Audio_Initialize_System(void);

void DMA_IRQ_FUNC(ASRC_IN_IDX)(void);

void TIMER_IRQ_FUNC(TIMER_RENDER)(void);

void TIMER_IRQ_FUNC(TIMER_FRAME_TX_END)(void);

void TIMER_IRQ_FUNC(TIMER_SIMUL)(void);

void AUDIOSINK_PHASE_IRQHandler(void);

void AUDIOSINK_PERIOD_IRQHandler(void);

void ASRC_ERROR_IRQHandler(void);

void DSP0_IRQHandler(void);

//void DIO0_IRQHandler(void);

void Volume_Set(int8_t volume);

void Volume_Shift_Subframe(int16_t *src);

void APP_ResetPrevSeqNumber(void);

void APP_Audio_Transfer(uint8_t *audio_buff, uint16_t audio_length, uint8_t seqNum);

void APP_Audio_Start(void);

void APP_Audio_Disconnect(void);

void APP_Audio_FlushQueue(void);

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* APP_AUDIO_H_ */
