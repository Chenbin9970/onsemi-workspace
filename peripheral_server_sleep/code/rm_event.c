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
 * rm_event.c
 * - Remote microphone protocol event handler
 * ----------------------------------------------------------------------------
 * $Revision: 1.14 $
 * $Date: 2019/01/09 22:13:40 $
 * ------------------------------------------------------------------------- */

#include "rm_pkt.h"

/* ----------------------------------------------------------------------------
 * Function      : uint8_t RM_Configure(struct rm_param_tag param,
 *                                      struct rm_callback callback)
 * ----------------------------------------------------------------------------
 * Description   : Configure protocol environment based on input from application
 * Inputs        : - param          - Application input parameters
 *                 - callback       - Application call back functions
 * Outputs       : return value     - 0 if it configures successfully,
 *                                    error value otherwise
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
uint8_t RM_Configure(struct rm_param_tag *param, struct rm_callback callback)
{
    uint8_t pa_pwr_temp;

	rm_env.role = param->role;
    rm_env.interval_time = param->interval_time; //in us
    rm_env.retrans_time = param->retrans_time;
    rm_env.audio_rate = param->audio_rate; //in Kbps
    rm_env.radio_rate = param->radio_rate; //in Kbps
    rm_env.ifs = 45; //in us

    rm_env.scan_time = param->scan_time;

    rm_env.audioChnl = param->audioChnl;

    rm_env.preFetchDelay = param->preFetchDelay;

    rm_env.preamble = param->preamble;
    rm_env.accessword = param->accessword;

    rm_env.pktLostLowThrshld = param->pktLostLowThrshld;
    rm_env.pktLostHighThrshld = param->pktLostHighThrshld;
    rm_env.pktLostLowThrshldSlow = param->pktLostLowThrshldSlow;

    rm_env.searchTryCntThrshld = param->searchTryCntThrshld;
    rm_env.waitCntGranularity = param->waitCntGranularity;

    rm_env.numChnlInHopList = param->numChnlInHopList;

    rm_env.clkAccuracy = param->clkAccuracy;

    rm_env.payloadFlowRequest = param->payloadFlowRequest;

    rm_env.stepSize = param->stepSize;
    rm_env.numChnlInHopList = param->numChnlInHopList;
    memcpy(&rm_env.hoplist, &param->hopList, 16);

    rm_env.mod_idx = param->mod_idx;
    rm_env.dma_memcpy_num = param->dma_memcpy_num;

    memcpy(&rm_env.debug_dio_num, &param->debug_dio_num, 4);

    rm_env.packet_length = ((rm_env.audio_rate * rm_env.interval_time) / 8000);
    rm_env.pktDuration = (((rm_env.packet_length + 8) * 8000)
            / rm_env.radio_rate);

    if(rm_env.audioChnl == RM_SECOND_AUDIO_CHANNEL)
    {
        rm_env.renderDelay = param->renderDelay + (rm_env.pktDuration * 2)
                + rm_env.ifs;
    }
    else
    {
        /* Real IFS is 9 us */
        rm_env.renderDelay = param->renderDelay + (rm_env.pktDuration * 4)
                + (rm_env.ifs * 3) + 18;
    }

    rm_env.crc_poly = 0x8810;

    rm_env.intf.trx_event = callback.trx_event;
    rm_env.intf.status_update = callback.status_update;

    while(BBIF_COEX_STATUS->BLE_IN_PROCESS_ALIAS || BBIF_COEX_STATUS->BLE_RX_ALIAS || BBIF_COEX_STATUS->BLE_TX_ALIAS);
    BBIF_COEX_CTRL->RX_ALIAS = 1;
    BBIF_COEX_CTRL->TX_ALIAS = 1;
    Sys_Delay_ProgramROM(1024);

    if(rm_env.radio_rate == 2000)
    {
        RF_REG_WRITE(BANK, 0x1);
        pa_pwr_temp = RF_REG19->PA_PWR_BYTE;
        memcpy((uint8_t *) RFREG_BASE, (uint8_t *) &rf_conf_2Mbps[0], 0x16);
        memcpy((uint8_t *) (RFREG_BASE + 0x17),
                (uint8_t *) &rf_conf_2Mbps[0x17], 0xA8);
        RF_REG19->PA_PWR_BYTE = pa_pwr_temp;
    }

    RF_REG_WRITE(BANK, 0x1);

    RF_InitRegistersCustomMode();

    rm_env.event = 0;
    rm_env.statusChange = 0;
    rm_env.oldLinkStatus = LINK_DISCONNECTED;
    rm_env.linkStatus = LINK_DISCONNECTED;

    rm_env.ptr_payload1 = rm_env.payload.rx.rm_payload_X;
    rm_env.ptr_payload2 = rm_env.payload.rx.rm_payload_Y;

    memset(&rm_env.payload.tx.rm_payload_left_1[0], 0xff, rm_env.packet_length);
    memset(&rm_env.payload.tx.rm_payload_right_1[0], 0xff,
            rm_env.packet_length);
    memset(&rm_env.payload.tx.rm_payload_left_2[0], 0xff, rm_env.packet_length);
    memset(&rm_env.payload.tx.rm_payload_right_2[0], 0xff,
            rm_env.packet_length);

    RemoteMic_Protocol_Init();

    *((uint32_t *)BB_COEXIFCNTL0_BASE) = 0x110c3;
    BB_COEXIFCNTL2->RX_ANT_DELAY_BYTE = 0xf;
    BB_COEXIFCNTL2->TX_ANT_DELAY_BYTE = 0xf;

    BBIF_COEX_CTRL->RX_ALIAS = 0;
    BBIF_COEX_CTRL->TX_ALIAS = 0;

    return 0;
}

/* ----------------------------------------------------------------------------
 * Function      : uint8_t RM_Enable(uint16_t offset)
 * ----------------------------------------------------------------------------
 * Description   : Enable the protocol
 * Inputs        : - offset     - Offset instant in micro second
 * Outputs       : return value - 0 if it enables successfully,
 *                                error value otherwise
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
uint8_t RM_Enable(uint16_t offset)
{
    Sys_Timers_Stop(SELECT_TIMER0);
    Sys_Timers_Stop(SELECT_TIMER1);
    NVIC_ClearPendingIRQ(TIMER0_IRQn);
    NVIC_ClearPendingIRQ(TIMER1_IRQn);


    if(rm_env.role == RM_SLAVE_ROLE)
    {
        NVIC_ClearPendingIRQ(RF_RXSTOP_IRQn);
        NVIC_EnableIRQ(RF_RXSTOP_IRQn);

        rm_env.state = RM_SEARCH;
    }
    else
    {
        NVIC_ClearPendingIRQ(RF_TX_IRQn);
        NVIC_EnableIRQ(RF_TX_IRQn);

        rm_env.state = RM_READY;
    }

    NVIC_EnableIRQ(TIMER0_IRQn);
    NVIC_EnableIRQ(TIMER1_IRQn);

    Sys_Timer_Set_Control(0, TIMER_SHOT_MODE | TIMER_PRESCALE_1 |
                          TIMER_SLOWCLK_DIV2 |
                          (offset - 1));
    Sys_Timers_Start(SELECT_TIMER0);

    SYSCTRL_RF_ACCESS_CFG->RF_IRQ_ACCESS_ALIAS = RF_IRQ_ACCESS_ENABLE_BITBAND;

    return RF_REG_READ(IRQ_STATUS);
}

/* ----------------------------------------------------------------------------
 * Function      : uint8_t RM_Disable(void)
 * ----------------------------------------------------------------------------
 * Description   : Disable the protocol
 * Inputs        : None
 * Outputs       : return value - 0 if it disables successfully,
 *                                error value otherwise
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
uint8_t RM_Disable(void)
{

    NVIC_DisableIRQ(TIMER0_IRQn);
    NVIC_DisableIRQ(TIMER1_IRQn);
    NVIC_DisableIRQ(RF_TX_IRQn);
    NVIC_DisableIRQ(RF_SYNC_IRQn);
    NVIC_DisableIRQ(RF_RXSTOP_IRQn);

    SYSCTRL_RF_ACCESS_CFG->RF_IRQ_ACCESS_ALIAS = RF_IRQ_ACCESS_DISABLE_BITBAND;

    return 0;
}

/* ----------------------------------------------------------------------------
 * Function      : uint8_t RM_EventHandler(uint8_t type,
 *                                         uint8_t *length, uint8_t *ptr)
 * ----------------------------------------------------------------------------
 * Description   : Protocol even handler
 * Inputs        : None
 * Outputs       : return value - 0 if it handles successfully,
 *                                error value otherwise
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
uint8_t RM_EventHandler(uint8_t type, uint8_t *length, uint8_t *ptr)
{
    switch(type)
    {
        case RM_TX_PAYLOAD_READY_LEFT:
            memcpy(&rm_env.payload.tx.rm_payload_left_2[0],
                    &rm_env.payload.tx.rm_payload_left_1[0],
                    rm_env.packet_length);
            memcpy(&rm_env.payload.tx.rm_payload_left_1[0], &ptr[0],
                    rm_env.packet_length);
            break;

        case RM_TX_PAYLOAD_READY_RIGHT:
            memcpy(&rm_env.payload.tx.rm_payload_right_2[0],
                    &rm_env.payload.tx.rm_payload_right_1[0],
                    rm_env.packet_length);
            memcpy(&rm_env.payload.tx.rm_payload_right_1[0], &ptr[0],
                    rm_env.packet_length);
            break;

        case RM_RX_TRANSFER_GOODPKT:
        case RM_RX_TRANSFER_BADCRCPKT:
        case RM_RX_TRANSFER_NOPKT:
            break;

        case RM_TX_START:
            break;

        case RM_TX_TRANSMIT_DONE:
            break;

        case RM_TX_RETRANSMIT_DONE:
            break;

        case RM_RX_MAIN_START:
        case RM_RX_RETRANSMIT_START:
            break;

        case RM_RX_N_RECEIVED:
            break;

        case RM_RX_N_RETRANSMIT_RECEIVED:
            break;

        case RM_RX_N_1_RECEIVED:
            break;

        case RM_RX_N_1_RETRANSMIT_RECEIVED:
            break;

        case RM_RX_MAIN_TIMEOUT:
            break;

        case RM_RX_RETRANSMIT_TIMEOUT:
            break;

        case RM_SWPLL_SYNC:
            break;

        default:
            break;
    }

    if(rm_env.statusChange)
    {
        rm_env.statusChange = 0;
        rm_env.intf.status_update(rm_env.linkStatus);
        rm_env.oldLinkStatus = rm_env.linkStatus;
    }

    return (rm_env.eventType);
}

/* ----------------------------------------------------------------------------
 * Function      : void RM_StatusHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : Protocol status update handler
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void RM_StatusHandler(void)
{
    if(rm_env.statusChange)
    {
        rm_env.statusChange = 0;
        rm_env.intf.status_update(rm_env.linkStatus);
        rm_env.oldLinkStatus = rm_env.linkStatus;
    }

    if(rm_env.event)
    {
        rm_env.event = 0;
        rm_env.intf.trx_event(rm_env.eventType, &rm_env.packet_length,
                rm_env.ptr);
    }
}
