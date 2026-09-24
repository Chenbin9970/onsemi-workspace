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
 * $Revision: 1.13 $
 * $Date: 2019/12/27 18:50:38 $
 * ------------------------------------------------------------------------- */

#ifndef APP_H_
#define APP_H_

/* ----------------------------------------------------------------------------
 * Include files
 * --------------------------------------------------------------------------*/
#include <rsl10.h>
#include <stdlib.h>
#include <stdbool.h>
#include <rsl10_ke.h>
#include <rsl10_ble.h>
#include <rsl10_profiles.h>
#include <rsl10_map_nvr.h>
#include <rm_pkt.h>
#include "ble_std.h"
#include "ble_custom.h"
#include "ble_basc.h"

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

/*  (nordic channel/2 - 1)
 *  {1, 7, 13, 19, 25, 31, 37}
 *  {2, 8, 14, 20, 26, 35, 38}
 *  {3, 9, 15, 21, 24, 33, 36}
 */
#define RM_HOPLIST                      { 37, 9, 16, 20, 29, 32, 17 }

#define RM_LEFT                         0
#define RM_RIGHT                        1

#ifndef APP_RM_AUDIO_CHANNEL
#define APP_RM_AUDIO_CHANNEL            RM_LEFT
#endif

#define OUTPUT_POWER_6DBM               0

#define NO_RX_INPUT                     0
#define SPI_RX_CODED_INPUT              1    /*with bi_directional_master in E7100 */
#define SPI_RX_RAW_INPUT                2    /*with RSL10_RemoteDongle in E7100 */
#define PCM_RX_RAW_INPUT                3

#define NO_TX_OUTPUT                    2
#define SPI_TX_CODED_OUTPUT             3    /*with RSL10_RM_HearingAid in E7100 */
#define SPI_TX_RAW_OUTPUT               4    /*with audio_spi_slave in E7100 */

#define INPUT_INTRF                     SPI_RX_CODED_INPUT    /*PCM_RX_RAW_INPUT//SPI_RX_CODED_INPUT// */
#define OUTPUT_INTRF                    NO_TX_OUTPUT    /*Fixed */
#define SIMUL                           0    /*Fixed */

#if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT)
#define APP_RM_DATA_REQUEST_TYPE        RM_PRO_REQUEST
#else    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT) */
#define APP_RM_DATA_REQUEST_TYPE        RM_APP_REQUEST
#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT) */

#if (INPUT_INTRF == PCM_RX_RAW_INPUT)
#define EZAIRO_7100                     0
#define AUDIO_CODEC_SHIELD              1
#define PCM_RX_RAW_SOURCE               EZAIRO_7100    /* EZAIRO_7100 */
#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT) */

#define DEBUG_UART_LOG                  0

#define THREE_BLOCK_APPN(x, y, z)       x##y##z
#define DMA_IRQn(x)                     THREE_BLOCK_APPN(DMA, x, _IRQn)    /*DMAx_IRQn */
#define TIMER_IRQn(x)                   THREE_BLOCK_APPN(TIMER, x, _IRQn)    /*TIMERx_IRQn */
#define DMA_IRQ_FUNC(x)                 THREE_BLOCK_APPN(DMA, x, _IRQHandler)    /*DMAx_IRQHandler */
#define TIMER_IRQ_FUNC(x)               THREE_BLOCK_APPN(TIMER, x, _IRQHandler)    /*TIMERx_IRQHandler */

#define MEMCPY_DMA_NUM                  0
#if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT)
#define MEMCPY_RESTORE_STATE_MEM        1
#define MEMCPY_SAVE_STATE_MEM           2
#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT) */
#define ASRC_IN_IDX                     3
#define ASRC_OUT_IDX                    4
#define RX_DMA_NUM                      5
#define TX_DMA_NUM                      6
#define UART_TX_NUM                     7

#define UART_BAUD_RATE                  115200
#define UART_RX                         4
#define UART_TX                         5

#if (SIMUL == 1)
#define TIMER_SIMUL                     3
#endif    /* if (SIMUL == 1) */
#define TIMER_REGUL                     2

#define PCM_CFG_RX                      (PCM_BIT_ORDER_MSB_FIRST | \
                                         PCM_TX_ALIGN_LSB |        \
                                         PCM_WORD_SIZE_24 |        \
                                         PCM_FRAME_ALIGN_LAST |    \
                                         PCM_FRAME_WIDTH_LONG |    \
                                         PCM_MULTIWORD_2 |         \
                                         PCM_SUBFRAME_DISABLE |    \
                                         PCM_CONTROLLER_DMA |      \
                                         PCM_DISABLE |             \
                                         PCM_SELECT_SLAVE)

#define TX_DMA_SPI                      (DMA_DEST_SPI0 |            \
                                         DMA_TRANSFER_M_TO_P |      \
                                         DMA_LITTLE_ENDIAN |        \
                                         DMA_COMPLETE_INT_DISABLE | \
                                         DMA_COUNTER_INT_DISABLE |  \
                                         DMA_DEST_WORD_SIZE_8 |     \
                                         DMA_SRC_WORD_SIZE_32 |     \
                                         DMA_SRC_ADDR_INC |         \
                                         DMA_DEST_ADDR_STATIC |     \
                                         DMA_ADDR_LIN |             \
                                         DMA_DISABLE)

#define UART_TX_CFG                      (DMA_DISABLE               |  \
                                          DMA_ADDR_LIN               | \
                                          DMA_TRANSFER_M_TO_P        | \
                                          DMA_PRIORITY_0             | \
                                          DMA_DISABLE_INT_DISABLE    | \
                                          DMA_ERROR_INT_DISABLE      | \
                                          DMA_COMPLETE_INT_DISABLE   | \
                                          DMA_COUNTER_INT_DISABLE    | \
                                          DMA_START_INT_DISABLE      | \
                                          DMA_LITTLE_ENDIAN          | \
                                          DMA_SRC_ADDR_INC           | \
                                          DMA_SRC_WORD_SIZE_32       | \
                                          DMA_DEST_WORD_SIZE_8       | \
                                          DMA_SRC_ADDR_STEP_SIZE_1   | \
                                          DMA_DEST_ADDR_STATIC       | \
                                          DMA_DEST_UART)

#if (INPUT_INTRF == SPI_RX_RAW_INPUT)
#define DMA_RX_CONFIG                   (DMA_LITTLE_ENDIAN |       \
                                         DMA_DISABLE |             \
                                         DMA_DISABLE_INT_DISABLE | \
                                         DMA_ERROR_INT_DISABLE |   \
                                         DMA_COMPLETE_INT_ENABLE | \
                                         DMA_COUNTER_INT_ENABLE |  \
                                         DMA_START_INT_DISABLE |   \
                                         DMA_DEST_WORD_SIZE_32 |   \
                                         DMA_SRC_WORD_SIZE_16 |    \
                                         DMA_SRC_SPI0 |            \
                                         DMA_PRIORITY_0 |          \
                                         DMA_TRANSFER_P_TO_M |     \
                                         DMA_DEST_ADDR_INC |       \
                                         DMA_SRC_ADDR_STATIC |     \
                                         DMA_ADDR_CIRC)
#elif (INPUT_INTRF == SPI_RX_CODED_INPUT)
#define DMA_RX_CONFIG                   (DMA_LITTLE_ENDIAN |       \
                                         DMA_DISABLE |             \
                                         DMA_DISABLE_INT_DISABLE | \
                                         DMA_ERROR_INT_DISABLE |   \
                                         DMA_COMPLETE_INT_ENABLE | \
                                         DMA_COUNTER_INT_DISABLE | \
                                         DMA_START_INT_DISABLE |   \
                                         DMA_DEST_WORD_SIZE_32 |   \
                                         DMA_SRC_WORD_SIZE_8 |     \
                                         DMA_SRC_SPI0 |            \
                                         DMA_PRIORITY_0 |          \
                                         DMA_TRANSFER_P_TO_M |     \
                                         DMA_DEST_ADDR_INC |       \
                                         DMA_SRC_ADDR_STATIC |     \
                                         DMA_ADDR_CIRC)
#elif (INPUT_INTRF == PCM_RX_RAW_INPUT)
#define DMA_RX_CONFIG                   (DMA_LITTLE_ENDIAN |       \
                                         DMA_DISABLE |             \
                                         DMA_DISABLE_INT_DISABLE | \
                                         DMA_COMPLETE_INT_ENABLE | \
                                         DMA_COUNTER_INT_ENABLE |   \
                                         DMA_START_INT_DISABLE |   \
                                         DMA_DEST_WORD_SIZE_32 |   \
                                         DMA_SRC_WORD_SIZE_32 |    \
                                         DMA_SRC_PCM |             \
                                         DMA_PRIORITY_0 |          \
                                         DMA_TRANSFER_P_TO_M |     \
                                         DMA_DEST_ADDR_INC |       \
                                         DMA_SRC_ADDR_STATIC |     \
                                         DMA_ADDR_CIRC)
#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT) */

#define DMA_SAVE_STATE_MEM_CONFIG       (DMA_LITTLE_ENDIAN |       \
                                         DMA_DISABLE |             \
                                         DMA_DISABLE_INT_DISABLE | \
                                         DMA_ERROR_INT_DISABLE |   \
                                         DMA_COMPLETE_INT_ENABLE | \
                                         DMA_COUNTER_INT_DISABLE | \
                                         DMA_START_INT_DISABLE |   \
                                         DMA_DEST_WORD_SIZE_32 |   \
                                         DMA_SRC_WORD_SIZE_32 |    \
                                         DMA_PRIORITY_0 |          \
                                         DMA_SRC_PBUS |            \
                                         DMA_TRANSFER_P_TO_M |     \
                                         DMA_DEST_ADDR_INC |       \
                                         DMA_SRC_ADDR_INC |        \
                                         DMA_ADDR_LIN)

#define DMA_RESTORE_STATE_MEM_CONFIG    (DMA_LITTLE_ENDIAN |       \
                                         DMA_DISABLE |             \
                                         DMA_DISABLE_INT_DISABLE | \
                                         DMA_ERROR_INT_DISABLE |   \
                                         DMA_COMPLETE_INT_ENABLE | \
                                         DMA_COUNTER_INT_DISABLE | \
                                         DMA_START_INT_DISABLE |   \
                                         DMA_DEST_WORD_SIZE_32 |   \
                                         DMA_SRC_WORD_SIZE_32 |    \
                                         DMA_PRIORITY_0 |          \
                                         DMA_DEST_PBUS |           \
                                         DMA_TRANSFER_M_TO_P |     \
                                         DMA_DEST_ADDR_INC |       \
                                         DMA_SRC_ADDR_INC |        \
                                         DMA_ADDR_LIN)

#define DMA_MEMCPY_CONFIG               (DMA_DEST_PBUS |        \
                                         DMA_ENABLE |           \
                                         DMA_ADDR_LIN |         \
                                         DMA_SRC_ADDR_INC |     \
                                         DMA_DEST_ADDR_STATIC | \
                                         DMA_TRANSFER_M_TO_P |  \
                                         DMA_SRC_WORD_SIZE_32 | \
                                         DMA_DEST_WORD_SIZE_8 | \
                                         DMA_COMPLETE_INT_ENABLE)

/* DMA for ASRC input on TX side */
#define TX_DMA_ASRC_IN                  (DMA_DEST_ASRC |           \
                                         DMA_TRANSFER_M_TO_P |     \
                                         DMA_LITTLE_ENDIAN |       \
                                         DMA_COMPLETE_INT_ENABLE | \
                                         DMA_COUNTER_INT_DISABLE | \
                                         DMA_DEST_WORD_SIZE_16 |   \
                                         DMA_SRC_WORD_SIZE_32 |    \
                                         DMA_SRC_ADDR_INC |        \
                                         DMA_DEST_ADDR_STATIC |    \
                                         DMA_ADDR_LIN |            \
                                         DMA_DISABLE)

/* DMA for ASRC output on TX side */
#define TX_DMA_ASRC_OUT                 (DMA_SRC_ASRC |            \
                                         DMA_TRANSFER_P_TO_M |     \
                                         DMA_LITTLE_ENDIAN |       \
                                         DMA_COMPLETE_INT_ENABLE | \
                                         DMA_COUNTER_INT_ENABLE |  \
                                         DMA_DEST_WORD_SIZE_32 |   \
                                         DMA_SRC_WORD_SIZE_16 |    \
                                         DMA_SRC_ADDR_STATIC |     \
                                         DMA_DEST_ADDR_STATIC |    \
                                         DMA_ADDR_LIN |            \
                                         DMA_DISABLE)

/* DMA for ASRC input on RX side */
#define RX_DMA_ASRC_IN                  (DMA_DEST_ASRC |            \
                                         DMA_TRANSFER_M_TO_P |      \
                                         DMA_LITTLE_ENDIAN |        \
                                         DMA_COMPLETE_INT_DISABLE | \
                                         DMA_COUNTER_INT_DISABLE |  \
                                         DMA_DEST_WORD_SIZE_16 |    \
                                         DMA_SRC_WORD_SIZE_32 |     \
                                         DMA_SRC_ADDR_INC |         \
                                         DMA_DEST_ADDR_STATIC |     \
                                         DMA_ADDR_LIN |             \
                                         DMA_DISABLE)

/* DMA for ASRC output on RX side */
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

#define STABLE_THR                      400

#define AUDIO_FRAME_SIZE                60

#define CONCAT(x, y)                    x##y
#define DIO_SRC(x)                      CONCAT(DIO_SRC_DIO_, x)

/* DIO pin configuration for SPI */
#define SPI_SER_DI                      2
#define SPI_SER_DO                      1
#define SPI_CLK_DO                      3
#define SPI_CS_DO                       0

/* DIO pin configuration for PCM interface */
#if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD)
#define PCM_SER_DI                      11
#define PCM_SER_DO                      10
#define PCM_CLK_DO                      12
#define PCM_FRAME_SYNC                  9
#else    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD) */
#define PCM_SER_DI                      2
#define PCM_SER_DO                      1
#define PCM_CLK_DO                      3
#define PCM_FRAME_SYNC                  0
#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD) */

#define BUTTON_DIO                      5
#define DIO_SYNC_PULSE                  8
#if (INPUT_INTRF == PCM_RX_RAW_INPUT)
#define SAMPL_CLK                       PCM_FRAME_SYNC
#else    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT) */
#define SAMPL_CLK                       7
#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT) */

/* DIO number that is used for easy re-flashing (recovery mode) */
#define RECOVERY_DIO                    13

/* Charge pump clock prescale value. With SLOWCLK = 2 MHz, CPCLK = 166 kHz */
#define CPCLK_PRESCALE_12               ((uint32_t)(0XBU << \
                                        CLK_DIV_CFG2_CPCLK_PRESCALE_Pos))

#define DEBUG_DIO_FIRST                 15
#if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD)
#define DEBUG_DIO_SECOND                1
#else    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD) */
#define DEBUG_DIO_SECOND                11
#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD) */

/* LPDSP32 CODEC related defines */
#define MEM_CM2DSP_ADDR0_ENC            (uint8_t *)(DSP_DRAM5_BASE)
#define MEM_CM2DSP_ADDR1_ENC            (uint8_t *)(DSP_DRAM5_BASE + 160 * 2)
#define MEM_DSP2CM_ADDR0_ENC            (uint8_t *)(DSP_DRAM4_BASE)
#define MEM_DSP2CM_ADDR1_ENC            (uint8_t *)(DSP_DRAM4_BASE + 80)
#define MEM_CM2DSP_ADDR0_DEC            (uint8_t *)(DSP_DRAM5_BASE + 160 * 4)
#define MEM_CM2DSP_ADDR1_DEC            (uint8_t *)(DSP_DRAM5_BASE + 160 * 4 + 80)
#define MEM_DSP2CM_ADDR0_DEC            (uint8_t *)(DSP_DRAM4_BASE + 160 * 4)
#define MEM_DSP2CM_ADDR1_DEC            (uint8_t *)(DSP_DRAM4_BASE + 160 * 6)
#define MEM_MESSAGE                     (uint8_t *)(DSP_DRAM4_BASE + 12 * 160)
#define CODEC_MODE                      3

/* Subframe length in uint16_t */
#if (INPUT_INTRF == PCM_RX_RAW_INPUT)
#define SUBFRAME_LENGTH                 16
#else    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT) */
#define SUBFRAME_LENGTH                 8
#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT) */

/* Total subframe length accounting for left and right channel samples in uint16_t */
#define SUBFRAME_LENGTH_LEFT_AND_RIGHT  (SUBFRAME_LENGTH * 2)

/* Frmae length in uint16_t */
#define FRAME_LENGTH                    160

/* Encoeded frame length in uint8_t */
#if (CODEC_MODE == 3)
#define ENCODED_FRAME_LENGTH            (3 * (FRAME_LENGTH / 8))

/* Encoded subframe length in uint8_t */
#define ENCODED_SUBFRAME_LENGTH         (3 * (SUBFRAME_LENGTH / 8))
#endif    /* if (CODEC_MODE == 3) */

/* Threshold for valid Cr/Ck distance */
#define ASRC_CFG_THR                    16

/* Threshold for allowed read/write pointer crossing */
#define PTR_RST_THR                     10

/* the number of shifts of ASRC registers for using fix point variables */
#define SHIFT_BIT                       20

/* TX data fifo related defines */
#define TX_DATA_FIFO_LENGTH             (2 * ENCODED_FRAME_LENGTH)
#define TX_FIFO_RWPTR_INIT              (ENCODED_FRAME_LENGTH)
#define TX_FIFO_W2R_THR                 (ENCODED_SUBFRAME_LENGTH)
#define TX_FIFO_R2W_THR                 (ENCODED_SUBFRAME_LENGTH)

#if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD)
#define I2C_SCL_DIO                                 2
#define I2C_SDA_DIO                                 3
#define I2C_BUFFER_SIZE                             2

#define WM8731_I2C_SLAVE_ADDRESS                    0x1A
#define WM8731_I2C_MESSAGE_BUFFER_SIZE              11

#define WM8731_LLINE_IN_REG_ADDR                    0x00
#define WM8731_RLINE_IN_REG_ADDR                    0x02
#define WM8731_LLINE_OUT_REG_ADDR                   0x04
#define WM8731_RLINE_OUT_REG_ADDR                   0x06
#define WM8731_ANALOG_AUDIO_PATH_CTRL_REG_ADDR      0x08
#define WM8731_DIGITAL_AUDIO_PATH_CTRL_REG_ADDR     0x0A
#define WM8731_POWER_DOWN_CTRL_REG_ADDR             0x0C
#define WM8731_DAI_REG_ADDR                         0x0E
#define WM8731_SAMPLING_CTRL_REG_ADDR               0x10
#define WM8731_ACTIVE_CTRL_REG_ADDR                 0x12
#define WM8731_RESET_REG_ADDR                       0x1E

#define WM8731_LLINE_IN_REG_VAL                     0x17
#define WM8731_RLINE_IN_REG_VAL                     0x17
#define WM8731_LLINE_OUT_REG_VAL                    0x79
#define WM8731_RLINE_OUT_REG_VAL                    0x79
#define WM8731_ANALOG_AUDIO_PATH_CTRL_REG_VAL       0x13
#define WM8731_DIGITAL_AUDIO_PATH_CTRL_REG_VAL      0x00
#define WM8731_POWER_DOWN_CTRL_REG_VAL              0x00
#define WM8731_DAI_REG_VAL                          0x49
#define WM8731_SAMPLING_CTRL_REG_VAL                0x99
#define WM8731_ACTIVE_CTRL_REG_VAL                  0x01
#define WM8731_RESET_REG_VAL                        0x00
#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD) */

/* Minimum and maximum VBAT measurements */
#define VBAT_1p1V_MEASURED              0x1200
#define VBAT_1p4V_MEASURED              0x16cc

/* DIO number that is connected to LED of EVB */
#define LED_DIO_NUM                     6

/* Set timer to 200 ms (20 times the 10 ms kernel timer resolution) */
#define TIMER_200MS_SETTING             20
typedef void (*appm_add_svc_func_t)(void);
#define DEFINE_SERVICE_ADD_FUNCTION(func) (appm_add_svc_func_t)func
#define DEFINE_MESSAGE_HANDLER(message, handler) { message, \
                                                   (ke_msg_func_t)handler }

/* List of message handlers that are used by the different profiles/services */
#define APP_MESSAGE_HANDLER_LIST \
    DEFINE_MESSAGE_HANDLER(APP_TEST_TIMER, APP_Timer)

/* List of functions used to create the database */
#define SERVICE_ADD_FUNCTION_LIST \
    DEFINE_SERVICE_ADD_FUNCTION(Batt_ServiceAdd_Client)

typedef void (*appm_enable_svc_func_t)(uint8_t);
#define DEFINE_SERVICE_ENABLE_FUNCTION(func) (appm_enable_svc_func_t)func

/* List of functions used to enable client services */
#define SERVICE_ENABLE_FUNCTION_LIST                           \
    DEFINE_SERVICE_ENABLE_FUNCTION(Batt_ServiceEnable_Client), \
    DEFINE_SERVICE_ENABLE_FUNCTION(CustomService_ServiceEnable)

/* The number of services that are not custom and are added */
#define BLE_NUM_SVC                     1

/* ----------------------------------------------------------------------------
 * Data types
 * --------------------------------------------------------------------------*/
struct app_env_tag
{
    /* Battery service */
    uint8_t send_batt_req;
    uint8_t RM_on_off;
    uint8_t volume;
    struct rm_param_tag rm_param;
    uint8_t rm_link_status;
    uint16_t rm_lostLink_counter;
    uint16_t rm_unsuccessLink_cunter;
    uint8_t audio_streaming;
};

#if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD)
struct wm8731_i2c_message
{
    uint8_t register_address;
    uint8_t register_data;
};
#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD) */

typedef enum
{
    PKT_LEFT = 0,
    PKT_RIGHT = 1
} PacketSide;

/* APP Task messages */
enum appm_msg
{
    APPM_DUMMY_MSG = TASK_FIRST_MSG(TASK_ID_APP),

    /* Timer used to have a tick periodically for application */
    APP_TEST_TIMER,
};

/* ----------------------------------------------------------------------------
 * Global variables and types
 * --------------------------------------------------------------------------*/
extern void *ISR_Vector_Table;

extern uint8_t payload_left_1[120];
extern uint8_t payload_right_1[120];
extern uint8_t payload_left_2[120];
extern uint8_t payload_right_2[120];

extern uint8_t audio_left[120];
extern uint8_t audio_right[120];

extern uint8_t ear_side;

#if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT)
extern int16_t spi_buf[];
extern int32_t pcm_buf[];
extern int16_t asrc_in_buf[];
extern int32_t data_fifo_rec;
extern uint32_t asrc_state_mem_rx[2][31];
extern uint8_t *Dsp2CmBuff0dec;
extern uint8_t UARTTXBuffer[];

extern bool asrc_stable;
extern uint32_t cntr_stability;
extern bool asrc_stable;
extern bool flag_ascc_phase;
extern int64_t audio_sink_cnt;

#if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD)
extern struct wm8731_i2c_message wm8731_i2c_message_buffer[];
extern volatile uint8_t i2c_tx_buffer_data[];
extern volatile uint8_t i2c_tx_buffer_index;
#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD) */

#else    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT) */
extern int8_t spi_buf[];
void port_rx_coded_dma_isr(void);

#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT) */

extern const struct ke_task_desc TASK_DESC_APP;

/* Support for the application manager and the application environment */
extern struct app_env_tag app_env;

/* List of functions used to create the database */
extern const appm_add_svc_func_t appm_add_svc_func_list[];

extern void *ISR_Vector_Table;

extern uint8_t payload_left_1[120];
extern uint8_t payload_right_1[120];
extern uint8_t payload_left_2[120];
extern uint8_t payload_right_2[120];

extern uint8_t audio_left[120];
extern uint8_t audio_right[120];

/* ----------------------------------------------------------------------------
 * Function prototype definitions
 * --------------------------------------------------------------------------*/
extern void App_Initialize(void);

extern void App_Env_Initialize(void);

extern int APP_Timer(ke_msg_id_t const msg_id, void const *param,
                     ke_task_id_t const dest_id,
                     ke_task_id_t const src_id);

extern int Msg_Handler(ke_msg_id_t const msgid, void *param,
                       ke_task_id_t const dest_id,
                       ke_task_id_t const src_id);

extern void APP_RM_Init(uint8_t side);

extern uint8_t RM_Callback_TRX(uint8_t type, uint8_t *length, uint8_t *ptr);

extern uint8_t RM_Callback_StatusUpdate(uint8_t status);

extern void DIO0_IRQHandler(void);

#if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT)
void Start_Dec_Lpdsp32(uint8_t *src_addr);

void Start_Enc_Lpdsp32(uint32_t src_addr, uint8_t side);

void UartLogInit(void);

void UartLogTx(uint8_t *str);

uint32_t * Read_buffer(uint8_t side);

void Asrc_reconfig(void);

void Asrc_in_dma_isr(void);

void Asrc_out_dma_isr(void);

void DspEnc_isr(uint8_t side);

void DspDec_isr(void);

void Ascc_phase_isr(void);

void Ascc_period_isr(void);

void Packet_regulator_timer_isr(void);

void Rendering_func(uint8_t *src_addr);

void Simulation_timer_isr(void);

void Asrc_reconfig(void);

#if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD)
extern void I2C_IRQHandler(void);

#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD) */
#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT) */

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* APP_H */
