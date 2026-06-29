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
 * rm_pkt.h
 * - Remote microphone protocol header file
 * ----------------------------------------------------------------------------
 * $Revision: 1.15 $
 * $Date: 2017/11/28 22:55:50 $
 * ------------------------------------------------------------------------- */

#ifndef RM_PKT_H
#define RM_PKT_H

/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif

/* ----------------------------------------------------------------------------
 * Include files
 * --------------------------------------------------------------------------*/
#include <rsl10.h>

/* ----------------------------------------------------------------------------
 * Defines
 * --------------------------------------------------------------------------*/
#define RM_DEBUG                        1

#define RFREG_BASE                      0x40010000

/* GCS1 register addresses */
#define MODE                            0x00
#define MODE_2                          0x01
#define FOURFSK_CODING                  0x02
#define DATAWHITE_BTLE                  0x03
#define FIFO                            0x0B
#define FIFO_2                          0x0C
#define IRQ_CONF                        0x0D
#define PAD_CONF_1                      0x0E
#define PAD_CONF_2                      0x0F
#define PAD_CONF_3                      0x10
#define PAD_CONF_4                      0x11
#define PAD_CONF_5                      0x12
#define MAC_CONF                        0x13
#define RX_MAC_TIMER                    0x14
#define TX_MAC_TIMER                    0x15
#define BANK                            0x16
#define CHANNEL                         0x17
#define CENTER_FREQ                     0x18
#define CHANNELS_1                      0x1E
#define CHANNELS_2                      0x20
#define CODING                          0x21
#define PACKET_HANDLING                 0x22
#define PACKET_LENGTH                   0x23
#define PACKET_LENGTH_OPTS              0x24
#define PREAMBLE                        0x25
#define PREAMBLE_LENGTH                 0x26
#define ADDRESS_CONF                    0x27
#define ADDRESS                         0x28
#define ADDRESS_BROADCAST               0x2A
#define PATTERN                         0x2C
#define PACKET_EXTRA                    0x30
#define CONV_CODES_CONF                 0x31
#define CONV_CODES_POLY                 0x32
#define CRC_POLYNOMIAL                  0x34
#define CRC_RST                         0x38
#define CONV_CODES_PUNCT                0x3C
#define TX_MULT                         0x41
#define FILTER_GAIN                     0x58
#define PA_PWR                          0x65
#define RSSI_CTRL                       0x6A
#define TIMINGS_1                       0x81
#define TIMINGS_2                       0x82
#define TIMINGS_3                       0x83
#define AGC_ATT_1                       0x84
#define AGC_ATT_2                       0x88
#define TIMINGS_4                       0x89
#define TIMEOUT                         0x8C
#define SWCAP_FSM                       0xA2
#define XTAL_TRIM                       0xA7
#define SUBBAND_CONF                    0xB1
#define SUBBAND_CORR                    0xB7
#define RSSI_DETECT                     0xB8
#define FSM_MODE                        0xC0
#define FSM_STATUS                      0xC1
#define TXFIFO_STATUS                   0xC2
#define RXFIFO_STATUS                   0xC3
#define TXFIFO_COUNT                    0xC4
#define RXFIFO_COUNT                    0xC5
#define RSSI_MIN                        0xC6
#define RSSI_MAX                        0xC7
#define RSSI_PKT                        0xC8
#define RSSI_AVG                        0xCA
#define TXFIFO                          0xCC
#define RXFIFO                          0xD0
#define DESER_STATUS                    0xD4
#define IRQ_STATUS                      0xD8
#define CARRIER_RECOVERY_EXTRA          0x5C
#define RX_ATT_LEVEL                    0xCB

#define RF_TX_FIFO                      0x400100CC
#define RF_RX_FIFO                      0x400100D0

#define RM_PACKETS_PER_INTERVAL         4

#define RM_AUDIO_LEFT                   1
#define RM_AUDIO_RIGHT                  2
#define RM_TRANS_ID_N                   0
#define RM_TRANS_ID_N_1                 1
#define RM_TRANS_ID_N_RETRY             2
#define RM_TRANS_ID_N_1_RETRY           3

#define RM_RESERVED                     0
#define RM_CODEC                        2

#define N_LEFT                          ( (RM_RESERVED << 6) | (RM_CODEC << 4) | (RM_AUDIO_LEFT << 2) | (RM_TRANS_ID_N) )
#define N_1_LEFT                        ( (RM_RESERVED << 6) | (RM_CODEC << 4) | (RM_AUDIO_LEFT << 2) | (RM_TRANS_ID_N_1) )
#define N_LEFT_RETRY                    ( (RM_RESERVED << 6) | (RM_CODEC << 4) | (RM_AUDIO_LEFT << 2) | (RM_TRANS_ID_N_RETRY) )
#define N_1_LEFT_RETRY                  ( (RM_RESERVED << 6) | (RM_CODEC << 4) | (RM_AUDIO_LEFT << 2) | (RM_TRANS_ID_N_1_RETRY) )

#define N_RIGHT                         ( (RM_RESERVED << 6) | (RM_CODEC << 4) | (RM_AUDIO_RIGHT << 2) | (RM_TRANS_ID_N) )
#define N_1_RIGHT                       ( (RM_RESERVED << 6) | (RM_CODEC << 4) | (RM_AUDIO_RIGHT << 2) | (RM_TRANS_ID_N_1) )
#define N_RIGHT_RETRY                   ( (RM_RESERVED << 6) | (RM_CODEC << 4) | (RM_AUDIO_RIGHT << 2) | (RM_TRANS_ID_N_RETRY) )
#define N_1_RIGHT_RETRY                 ( (RM_RESERVED << 6) | (RM_CODEC << 4) | (RM_AUDIO_RIGHT << 2) | (RM_TRANS_ID_N_1_RETRY) )

#define RF_REG_READ(addr)               (*(volatile uint8_t *)(RFREG_BASE + addr))
#define RF_REG_WRITE(addr, value)       (*(volatile uint8_t *)(RFREG_BASE + addr)) = (value)

#define BLE_MOD_IDX                     0
#define BT_MOD_IDX                      1

#define RM_DMA_MEMCPY_CONFIG_TX          (DMA_DEST_PBUS |\
                                         DMA_ENABLE |\
                                         DMA_ADDR_LIN |\
                                         DMA_SRC_ADDR_INC |\
                                         DMA_DEST_ADDR_STATIC |\
                                         DMA_TRANSFER_M_TO_P |\
                                         DMA_SRC_WORD_SIZE_32 |\
                                         DMA_DEST_WORD_SIZE_8 |\
                                         DMA_PRIORITY_3 |\
                                         DMA_COMPLETE_INT_DISABLE)

#define RM_DMA_MEMCPY_CONFIG_RX          (DMA_SRC_PBUS |\
                                         DMA_ENABLE |\
                                         DMA_ADDR_LIN |\
                                         DMA_DEST_ADDR_INC |\
                                         DMA_SRC_ADDR_STATIC |\
                                         DMA_TRANSFER_P_TO_M |\
                                         DMA_SRC_WORD_SIZE_8 |\
                                         DMA_DEST_WORD_SIZE_32 |\
                                         DMA_PRIORITY_3 |\
                                         DMA_COMPLETE_INT_DISABLE)

#define MAX_PAYLOAD_SIZE                120

/* Protocol device role */
#define RM_SLAVE_ROLE                   0
#define RM_MASTER_ROLE                  1

#define RM_PRO_REQUEST                  0
#define RM_APP_REQUEST                  1

/* ----------------------------------------------------------------------------
 * Global variables and types
 * --------------------------------------------------------------------------*/
/* Protocol state machine */
enum rm_state
{
    RM_IDLE = 0,
    RM_SEARCH,
    RM_READY,
    RM_WAIT_FOR_TX_START_FIRST,
    RM_WAIT_FOR_RX_START_FIRST,
    RM_TX_FIRST,
    RM_RX_FIRST,
    RM_WAIT_FOR_TX_START_SECOND,
    RM_WAIT_FOR_RX_START_SECOND,
    RM_TX_SECOND,
    RM_RX_SECOND,
    RM_WAIT_FOR_RX_START_SECOND_DELAYED,
};

struct rm_pkt_tag
{
    uint8_t trans_id : 2;
    uint8_t audio_ch : 2;
    uint8_t codec    : 2;
    uint8_t reserved : 2;
};

/* Call back function structure to be in APIs */
struct rm_callback
{
    uint8_t (* trx_event)(uint8_t type, uint8_t *length, uint8_t *ptr);
    uint8_t (* status_update)(uint8_t status);

};

struct rm_payload_buf_tx
{
    uint32_t align;
    uint8_t rm_payload_left_1[MAX_PAYLOAD_SIZE];
    uint8_t rm_payload_right_1[MAX_PAYLOAD_SIZE];
    uint8_t rm_payload_left_2[MAX_PAYLOAD_SIZE];
    uint8_t rm_payload_right_2[MAX_PAYLOAD_SIZE];
};

struct rm_payload_buf_rx
{
    uint32_t align;
    uint8_t rm_payload_X[MAX_PAYLOAD_SIZE];
    uint8_t rm_payload_Y[MAX_PAYLOAD_SIZE];
    uint8_t rm_payload_Z[MAX_PAYLOAD_SIZE];
};

union rm_paylaod_tag
{
    struct rm_payload_buf_tx tx;
    struct rm_payload_buf_rx rx;
};

struct rm_env_tag
{
    /* In us */
    uint32_t interval_time;
    uint32_t retrans_time;
    uint16_t ifs;
    uint32_t ble_offset;
    uint32_t scan_time;
    uint32_t eventProcessTime;

    uint16_t renderDelay;
    uint16_t preFetchDelay;

    uint8_t packet_length;
    uint8_t role;
    uint8_t errorHndl;
    uint32_t interval_counter;

    uint16_t audio_rate;
    uint16_t radio_rate;

    uint8_t numChnlInHopList;
    uint8_t hoplist[16];
    uint8_t stepSize;

    /* In ppm */
    uint16_t clkAccuracy;

    uint8_t audioChnl;

    uint8_t state;
    uint8_t mod_idx;
    uint8_t payloadFlowRequest;
    uint8_t dma_memcpy_num;
    uint8_t debug_dio_num[4];
    uint8_t gi;
    uint8_t *ptr;
    uint8_t **ptr_ptr;
    uint8_t ble_request;
    uint32_t gtmp;
    uint8_t rxLen;
    uint8_t txLen;
    uint8_t rxStatus;
    uint32_t rxErrCnt;
    uint32_t crcErrCnt;
    uint32_t pktLenErrCnt;
    uint32_t adrsErrCnt;
    uint32_t proErrCnt;
    uint32_t rxPktCnt;
    uint32_t rxPktNCnt;
    uint32_t rxPktN_1Cnt;
    uint32_t rxPktNReCnt;
    uint32_t rxPktN_1ReCnt;
    uint32_t txPktCnt;
    uint32_t inputCnt;
    uint8_t trackCnt;

    uint8_t event;
    uint8_t eventType;
    uint8_t statusChange;
    uint8_t oldLinkStatus;
    uint8_t linkStatus;

    uint8_t preamble;
    uint32_t accessword;
    uint32_t crc_poly;
    uint8_t gchnl;
    uint8_t oldChnl;
    uint8_t currentChnlIndx;
    uint8_t bleChnl;
    uint16_t pktLostCnt;
    uint16_t pktLostHighThrshld;
    uint16_t pktLostLowThrshld;
    uint16_t pktLostLowThrshldSlow;
    uint16_t linkLost_count;
    uint16_t searchTryCnt;
    uint16_t waitCnt;
    uint16_t searchTryCntThrshld;
    uint16_t waitCntGranularity;

    struct rm_pkt_tag rxBuff;

    union rm_paylaod_tag payload;

    struct rm_callback intf;

    uint8_t *ptr_payload_new;
    uint8_t *ptr_payload_old;

    uint32_t timerDuration;
    uint32_t pktDuration;

    uint8_t *ptr_payload1;
    uint8_t *ptr_payload2;

    uint32_t sortedPktNum :3;
    uint32_t pktNValid    :1;
    uint32_t pktN_1Valid  :1;
    uint32_t pktN_2Valid  :1;
    uint32_t pktNBadCRC   :1;
    uint32_t pktN_1BadCRC :1;
    uint32_t pktN_2BadCRC :1;
    uint32_t consecutiveRxTimeout :2;
    uint32_t consecutiveRxTimeoutMax :2;
};

/* Unidirectional and bidirectional audio definitions */
enum rm_audio_direction
{
    RM_MONODIRECTION = 0,
    RM_BIDIRECTION,
};

/* Audio channel number */
enum rm_audio_channel_num
{
    RM_FIRST_AUDIO_CHANNEL = 0,
    RM_SECOND_AUDIO_CHANNEL,
};

/* Link status to be used by application */
enum rm_status_update
{
    LINK_DISCONNECTED = 0,
    LINK_ESTABLISHMENT_UNSUCCESS,
    LINK_ESTABLISHED,
};

/* Event types used in APIs to notify application */
enum rm_app_trx_event_type
{
    RM_TX_PAYLOAD_READY_LEFT = 0,
    RM_TX_PAYLOAD_READY_RIGHT,
    RM_TX_START,
    RM_TX_TRANSMIT_DONE,
    RM_TX_RETRANSMIT_DONE,
    RM_RX_MAIN_START,
    RM_RX_RETRANSMIT_START,
    RM_RX_TRANSFER_GOODPKT,
    RM_RX_TRANSFER_BADCRCPKT,
    RM_RX_TRANSFER_NOPKT,
    RM_RX_N_RECEIVED,
    RM_RX_N_RETRANSMIT_RECEIVED,
    RM_RX_N_1_RECEIVED,
    RM_RX_N_1_RETRANSMIT_RECEIVED,
    RM_RX_MAIN_TIMEOUT,
    RM_RX_RETRANSMIT_TIMEOUT,
    RM_SWPLL_SYNC,
};

/* Protocol environment */
struct rm_param_tag
{
    /* Configuration for role of protocol in device */
    uint8_t role;

    /* Times in us */
    uint16_t interval_time;
    uint16_t retrans_time;
    uint32_t scan_time;
    uint16_t ifs;

    uint16_t renderDelay;
    uint16_t preFetchDelay;

    /* Rate in 1000 bits per second */
    uint16_t audio_rate;
    uint16_t radio_rate;

    uint8_t audioChnl;

    /* Configuration of preamble and access word */
    uint8_t preamble;
    uint32_t accessword;

    uint8_t mod_idx;
    uint8_t payloadFlowRequest;
    uint16_t clkAccuracy;
    uint8_t dma_memcpy_num;

    /* Link budget thresholds */
    uint16_t pktLostLowThrshld;
    uint16_t pktLostHighThrshld;
    uint16_t pktLostLowThrshldSlow;

    /* Search algorithm parameters */
    uint16_t searchTryCntThrshld;
    uint16_t waitCntGranularity;


    /* Configuration parametrs for hopping */
    uint8_t hopList[16];
    uint8_t stepSize;
    uint8_t numChnlInHopList;

    uint8_t debug_dio_num[4];

};

extern struct rm_env_tag rm_env;

extern uint8_t rf_conf_2Mbps[];
extern uint8_t rf_conf_1Mbps[];
extern uint8_t rf_conf_250Kbps[];
extern uint8_t rf_conf_500Kbps[];
extern uint32_t rf_freq_table[];

/* ----------------------------------------------------------------------------
 * Function prototype definitions
 * --------------------------------------------------------------------------*/
extern void RF_Reg_WriteBurst (uint16_t addr, uint8_t size, uint8_t *data);
extern void RM_TransmitPacket(void);
extern void RM_ReceivePacket(void);
extern void RemoteMic_Protocol_Init(void);
extern uint8_t RM_Configure(struct rm_param_tag *, struct rm_callback);
extern uint8_t RM_Enable(uint16_t offset);
extern uint8_t RM_Disable(void);
extern uint8_t RM_EventHandler(uint8_t type, uint8_t *length, uint8_t *ptr);
extern void RM_StatusHandler(void);
extern void RF_TX_IRQHandler(void);
extern void RF_RXSTOP_IRQHandler(void);
extern void RF_InitRegistersCustomMode (void);
extern uint8_t RM_PrepareHeader(uint8_t cnt, uint8_t **ptr);
extern void RF_SwitchToBLEMode (void);
extern void RF_SwitchToCPMode (void);

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif

#endif /* RM_PKT_H */
