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
 * app.h
 * - Application header file
 * ----------------------------------------------------------------------------
 * $Revision: 1.22 $
 * $Date: 2019/12/30 20:50:48 $
 * ------------------------------------------------------------------------- */

#ifndef APP_H_
#define APP_H_

/* ----------------------------------------------------------------------------
 * Include files
 * --------------------------------------------------------------------------*/
#include <rsl10.h>
#include <stdlib.h>
#include <rm_pkt.h>
#include "aes128.h"
#include "aesavs.h"
#include <codec.h>
#include "codecs/G722DSP/g722DSPCodec.h"
#include "codecs/celtDecDSP/celtDecDSPCodec.h"
/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

/* ----------------------------------------------------------------------------
 * Defines
 * --------------------------------------------------------------------------*/

/*  (Nordic channel/2 - 1)
 *  {1, 7, 13, 19, 25, 31, 37}
 *  {2, 8, 14, 20, 26, 35, 38}
 *  {3, 9, 15, 21, 24, 33, 36}
 */
#define RM_HOPLIST                      { 3, 9, 15, 21, 24, 33, 36 }
#define KEY_AES_128_ECB                 { 0x4138684C, \
                                          0xD874F539, \
                                          0x4EF3BC36, \
                                          0xBF01FB9D }

#define APP_RM_ROLE                     RM_SLAVE_ROLE
#define CRY_AES_128_ECB                 0

#define RM_LEFT                         0
#define RM_RIGHT                        1

/* Initial side channel */
#define APP_RM_AUDIO_CHANNEL            RM_LEFT

#define OUTPUT_POWER_6DBM               0

#define NO_TX_OUTPUT                    2
#define OD_TX_RAW_OUTPUT                3
#define SPI_TX_RAW_OUTPUT               4 //SPI only verified with G722
#define PCM_TX_RAW_OUTPUT               5

#ifndef OUTPUT_INTRF
#define OUTPUT_INTRF                    PCM_TX_RAW_OUTPUT
#endif

#define APP_RM_DATA_REQUEST_TYPE        RM_APP_REQUEST

#define THREE_BLOCK_APPN(x, y, z)       x##y##z
#define DMA_IRQn(x)                     THREE_BLOCK_APPN(DMA, x, _IRQn)
#define TIMER_IRQn(x)                   THREE_BLOCK_APPN(TIMER, x, _IRQn)
#define DMA_IRQ_FUNC(x)                 THREE_BLOCK_APPN(DMA, x, _IRQHandler)
#define TIMER_IRQ_FUNC(x)               THREE_BLOCK_APPN(TIMER, x, _IRQHandler)

/* DMA Channel numbers */
#define MEMCPY_DMA_NUM                  0
#define ASRC_IN_IDX                     3
#define ASRC_OUT_IDX                    4
#define OD_DMA_NUM                      1
#define PCM_DMA_NUM                     5

/* PCM output: mono 12 kHz, each sample in word0, word1 = 0.
   One frame = 120 mono samples = 240 words (word0/word1 pairs). */
#define PCM_FRAME_WORDS                 (3 * FRAME_LENGTH / 2)

/* pcm_tx_buf -> PCM->TX_DATA. One PCM_FRAME_WORDS transfer per arm; the
   complete interrupt swaps the double buffer. */
#define RX_DMA_PCM_STEREO               (DMA_DEST_PCM |             \
                                         DMA_TRANSFER_M_TO_P |      \
                                         DMA_LITTLE_ENDIAN |        \
                                         DMA_COMPLETE_INT_ENABLE |  \
                                         DMA_COUNTER_INT_DISABLE |  \
                                         DMA_DEST_WORD_SIZE_16 |    \
                                         DMA_SRC_WORD_SIZE_16 |     \
                                         DMA_SRC_ADDR_INC |         \
                                         DMA_DEST_ADDR_STATIC |     \
                                         DMA_ADDR_LIN |             \
                                         DMA_DISABLE)

/* Timer number */
#define TIMER_REGUL                     2

/* DMA for ASRC input on RX side */
#define RX_DMA_ASRC_IN                  (DMA_DEST_ASRC |            \
                                         DMA_TRANSFER_M_TO_P |      \
                                         DMA_LITTLE_ENDIAN |        \
                                         DMA_COMPLETE_INT_ENABLE | \
                                         DMA_COUNTER_INT_DISABLE |  \
                                         DMA_DEST_WORD_SIZE_16 |    \
                                         DMA_SRC_WORD_SIZE_32 |     \
                                         DMA_SRC_ADDR_INC |         \
                                         DMA_DEST_ADDR_STATIC |     \
                                         DMA_ADDR_LIN |             \
                                         DMA_DISABLE)

/* DMA for ASRC output on RX side */
#if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT)
#define RX_DMA_ASRC_OUT                 (DMA_SRC_ASRC |             \
                                         DMA_DEST_SPI0 |            \
                                         DMA_TRANSFER_P_TO_P |      \
                                         DMA_LITTLE_ENDIAN |        \
                                         DMA_COMPLETE_INT_DISABLE | \
                                         DMA_COUNTER_INT_DISABLE |  \
                                         DMA_DEST_WORD_SIZE_16 |    \
                                         DMA_SRC_WORD_SIZE_16 |     \
                                         DMA_SRC_ADDR_STATIC |      \
                                         DMA_DEST_ADDR_STATIC |     \
                                         DMA_ADDR_CIRC |            \
                                         DMA_DISABLE)
#elif (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT)
#define RX_DMA_ASRC_OUT                 (DMA_SRC_ASRC |             \
                                         DMA_DEST_PCM |             \
                                         DMA_TRANSFER_P_TO_P |      \
                                         DMA_LITTLE_ENDIAN |        \
                                         DMA_COMPLETE_INT_DISABLE | \
                                         DMA_COUNTER_INT_DISABLE |  \
                                         DMA_DEST_WORD_SIZE_16 |    \
                                         DMA_SRC_WORD_SIZE_16 |     \
                                         DMA_SRC_ADDR_STATIC |      \
                                         DMA_DEST_ADDR_STATIC |     \
                                         DMA_ADDR_CIRC |            \
                                         DMA_DISABLE)
#else    /* if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT) */

#define RX_DMA_ASRC_OUT                 (DMA_SRC_ASRC |             \
                                         DMA_TRANSFER_P_TO_M |      \
                                         DMA_LITTLE_ENDIAN |        \
                                         DMA_COMPLETE_INT_DISABLE | \
                                         DMA_COUNTER_INT_DISABLE |  \
                                         DMA_DEST_WORD_SIZE_32 |    \
                                         DMA_SRC_WORD_SIZE_16 |     \
                                         DMA_SRC_ADDR_STATIC |      \
                                         DMA_DEST_ADDR_INC |        \
                                         DMA_ADDR_CIRC |            \
                                         DMA_DISABLE)
#endif    /* if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT) */

#define RX_DMA_OD                      (DMA_LITTLE_ENDIAN |        \
                                        DMA_ENABLE |               \
                                        DMA_DISABLE_INT_DISABLE |  \
                                        DMA_ERROR_INT_DISABLE |    \
                                        DMA_COMPLETE_INT_DISABLE | \
                                        DMA_COUNTER_INT_DISABLE |  \
                                        DMA_START_INT_DISABLE |    \
                                        DMA_DEST_WORD_SIZE_16 |    \
                                        DMA_SRC_WORD_SIZE_32 |     \
                                        DMA_SRC_ADDR_INC |         \
                                        DMA_TRANSFER_M_TO_P |      \
                                        DMA_DEST_ADDR_STATIC |     \
                                        DMA_DEST_OD |              \
                                        DMA_PRIORITY_0 |           \
                                        DMA_ADDR_CIRC)

#define SIN_DMA_SPI                    (DMA_LITTLE_ENDIAN |        \
                                        DMA_ENABLE |               \
                                        DMA_DISABLE_INT_DISABLE |  \
                                        DMA_ERROR_INT_DISABLE |    \
                                        DMA_COMPLETE_INT_DISABLE | \
                                        DMA_COUNTER_INT_DISABLE |  \
                                        DMA_START_INT_DISABLE |    \
                                        DMA_DEST_WORD_SIZE_16 |    \
                                        DMA_SRC_WORD_SIZE_32 |     \
                                        DMA_SRC_ADDR_INC |         \
                                        DMA_TRANSFER_M_TO_P |      \
                                        DMA_DEST_ADDR_STATIC |     \
                                        DMA_DEST_SPI0 |            \
                                        DMA_PRIORITY_0 |           \
                                        DMA_ADDR_LIN)

#define DECIMATE_BY_200                 ((uint32_t)(0x11U << \
                                                    AUDIO_CFG_DEC_RATE_Pos))

/* Audio configurarion for OD */
#define AUDIO_CONFIG                    (OD_AUDIOCLK                        | \
                                         OD_UNDERRUN_PROTECT_ENABLE         | \
                                         OD_DMA_REQ_ENABLE                  | \
                                         OD_INT_GEN_DISABLE                 | \
                                         DECIMATE_BY_200                    | \
                                         OD_ENABLE)

#define CONCAT(x, y)                    x##y
#define DIO_SRC(x)                      CONCAT(DIO_SRC_DIO_, x)

/* DIO pin configuration for SPI */
#define SPI_SER_DI                      2
#define SPI_SER_DO                      1
#define SPI_CLK_DO                      3
#define SPI_CS_DO                       0

/* DIO pin configuration for OD */
#define OD_P_DIO                        0
#define OD_N_DIO                        1

/* DIO pin configuration for PCM output. The external device is the clock
   master (generates BCLK and FS), so RSL10 runs as PCM slave and only
   shifts data out on SERO. */
#define PCM_CLK_DO                      2
#define PCM_FRAME_SYNC                  3
#define PCM_SER_DI                      4    /* unused input; slave does not return data */
#define PCM_SER_DO                      14

/* PCM slave output: external master provides BCLK (384 kHz) and FS
   (12 kHz, 32 BCLK/frame). 16-bit x 2 words, short frame. */
#define PCM_CFG_TX                      (PCM_BIT_ORDER_MSB_FIRST | \
                                         PCM_TX_ALIGN_LSB |        \
                                         PCM_WORD_SIZE_16 |        \
                                         PCM_FRAME_ALIGN_LAST |    \
                                         PCM_FRAME_WIDTH_LONG |    \
                                         PCM_MULTIWORD_2 |         \
                                         PCM_SUBFRAME_DISABLE |    \
                                         PCM_CONTROLLER_DMA |      \
                                         PCM_DISABLE |             \
                                         PCM_SELECT_SLAVE)

/* Test switch: 1 = send a 1 kHz tone on word0 only (word1 = 0) to verify
   whether the host reads word0 (mono) or both words (stereo). */
#ifndef PCM_TEST_TONE
#define PCM_TEST_TONE                   0
#endif

/* Test switch: 1 = 1k->10k stepped frequency sweep, one tone per second,
   looping. Requires PCM_TEST_TONE = 1. */
#ifndef PCM_TEST_SWEEP
#define PCM_TEST_SWEEP                  0
#endif

/* Test switch: 1 = feed a synthetic 1 kHz sine into the Path B software
   resample (bypassing the decoded data). Isolates whether the noise/pitch
   problem is in the transport (resample + double buffer + PCM DMA) or in the
   decoded data. Requires the RM link (DSP0_IRQ drives the resample). */
#ifndef PCM_TEST_RESAMPLE_SINE
#define PCM_TEST_RESAMPLE_SINE          0
#endif

#if (PCM_TEST_SWEEP) && !(PCM_TEST_TONE)
#error "PCM_TEST_SWEEP requires PCM_TEST_TONE = 1"
#endif

#if (PCM_TEST_TONE)
#define PCM_TEST_BUF_LEN                24
/* 1 kHz tone on word0 only, word1 = 0. 24k word rate, 12 points per cycle.
   If the host reproduces a clean 1 kHz tone it reads word0 (mono). */
#define RX_DMA_PCM_TEST                 (DMA_DEST_PCM |            \
                                         DMA_TRANSFER_M_TO_P |     \
                                         DMA_LITTLE_ENDIAN |       \
                                         DMA_COMPLETE_INT_DISABLE |\
                                         DMA_COUNTER_INT_DISABLE | \
                                         DMA_DEST_WORD_SIZE_16 |   \
                                         DMA_SRC_WORD_SIZE_16 |    \
                                         DMA_SRC_ADDR_INC |        \
                                         DMA_DEST_ADDR_STATIC |    \
                                         DMA_ADDR_CIRC |           \
                                         DMA_DISABLE)
#endif    /* if (PCM_TEST_TONE) */

#if (PCM_TEST_SWEEP)
#define PCM_SWEEP_TONES                 10
/* TIMER3: free in this project. TIMER0/1 belong to the RM radio library,
   TIMER2 is the decode pacing timer. */
#define PCM_SWEEP_TIMER                 3
extern int16_t pcm_sweep_buf[PCM_SWEEP_TONES][PCM_TEST_BUF_LEN];
#endif    /* if (PCM_TEST_SWEEP) */

#define BUTTON_DIO                      5

#if (OUTPUT_INTRF == OD_TX_RAW_OUTPUT)
#define SAMPLING_CLK_SRC                AUDIOSINK_CLK_SRC_DMIC_OD
#elif (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT)
#define SAMPL_CLK                       PCM_FRAME_SYNC
#define SAMPLING_CLK_SRC                ((uint32_t)(SAMPL_CLK << \
                                                    DIO_AUDIOSINK_SRC_CLK_Pos))
#else    /* if (OUTPUT_INTRF == OD_TX_RAW_OUTPUT) */
#define SAMPL_CLK                       7
#define SAMPLING_CLK_SRC                ((uint32_t)(SAMPL_CLK << \
                                                    DIO_AUDIOSINK_SRC_CLK_Pos))
#endif    /* if (OUTPUT_INTRF == OD_TX_RAW_OUTPUT) */

/* DIO number that is used for easy re-flashing (recovery mode).
   Moved off DIO14, which is now the PCM serial data output (PCM_SER_DO). */
#define RECOVERY_DIO                    12

/* Charge pump clock prescale value. With SLOWCLK = 2 MHz, CPCLK = 166 kHz */
#define CPCLK_PRESCALE_12               ((uint32_t)(0XBU << \
                                        CLK_DIV_CFG2_CPCLK_PRESCALE_Pos))

#define DEBUG_DIO_FIRST                 15
#define DEBUG_DIO_SECOND                11
#define DEBUG_DIO_THIRD                 10

/* Codec configuration */
#define CODEC_CONFIG_G722               0
#define CODEC_CONFIG_CELT               1

#if (CODEC_CONFIG == CODEC_CONFIG_G722)
    #define POPULATE_CODEC_FUNCTION         populateG722DSPCodec
    #define CODEC_MODE                      3
    #define CODEC_ACTION                    (ALT_PACKED | DECODE_RESET | DECODE)
    #define CODEC_SAMPLE_RATE               0 //Not used in G722
    #define FRAME_LENGTH                    160
    #define CODEC_BLOCK_SIZE                4
    #define SUBFRAME_LENGTH                 8

    #if (CODEC_MODE == 3)
        #define ENCODED_FRAME_LENGTH            (3 * (FRAME_LENGTH / 8))
        #define ENCODED_SUBFRAME_LENGTH         (3 * (SUBFRAME_LENGTH / 8))
    #endif    /* if (CODEC_MODE == 3) */
#elif (CODEC_CONFIG == CODEC_CONFIG_CELT)
    #define POPULATE_CODEC_FUNCTION         populateCeltDecDSPCodec
    #define CODEC_MODE                      1
    #define SUBFRAME_LENGTH                 160
    #define SUBFRAME_LENGTH_BYTES           (SUBFRAME_LENGTH * 2)
    #define SUBFRAME_LENGTH_LEFT_AND_RIGHT  (SUBFRAME_LENGTH * 2)
    #define FRAME_LENGTH                    160 //160
    #define ENCODED_FRAME_LENGTH            40
    #define ENCODED_SUBFRAME_LENGTH         40
    #define CODEC_BLOCK_SIZE                40
    #define CODEC_SAMPLE_RATE               16000
    #define CODEC_ACTION                    (DECODE_RESET | DECODE)
    #if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT)
        #error "SPI output only verified with G722 codec option"
    #endif
#endif /* #if (CODEC_CONFIG == CODEC_CONFIG_G722) */

/* the number of shifts of ASRC registers for using fix point variables */
#define SHIFT_BIT                       20

/* ----------------------------------------------------------------------------
 * Data types
 * --------------------------------------------------------------------------*/
struct app_env_tag
{
    struct rm_param_tag rm_param;
    uint8_t rm_link_status;
    uint16_t rm_lostLink_counter;
    uint16_t rm_unsuccessLink_counter;
    uint8_t audio_streaming;
};

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

typedef struct
{
    CODEC codec;
    DSP_State state;
    OperationParameters channels;
    uint8_t* incoming;
    uint8_t* outgoing;
} LPDSP32Context;

/* ----------------------------------------------------------------------------
 * Global variables and types
 * --------------------------------------------------------------------------*/
extern void *ISR_Vector_Table;
extern uint8_t ear_side;
extern LPDSP32Context lpdsp32;
extern int16_t BufferOut[];

#if (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT)
extern int16_t pcm_tx_buf[2][PCM_FRAME_WORDS];
#endif    /* if (OUTPUT_INTRF == PCM_TX_RAW_OUTPUT) */

/* ----------------------------------------------------------------------------
 * Function prototype definitions
 * --------------------------------------------------------------------------*/
void App_Initialize(void);

void APP_RM_Init(uint8_t side);

uint8_t RM_Callback_TRX(uint8_t type, uint8_t *length, uint8_t *ptr);

uint8_t RM_Callback_StatusUpdate(uint8_t status);

void App_Process_Connected(void);

void App_Process_Link_Disconnected(void);

void App_Process_Incoming_Data(uint8_t *data, uint8_t len);

void ClearBufferOut(void);

extern struct app_env_tag app_env;

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* APP_H */
