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
 * rm_pkt_hdl.c
 * - Remote microphone protocol packet and state machine handler
 * ----------------------------------------------------------------------------
 * $Revision: 1.46 $
 * $Date: 2019/08/15 13:58:18 $
 * ------------------------------------------------------------------------- */

#include "rm_pkt.h"

/* Global variables */

struct rm_env_tag rm_env;

/* ----------------------------------------------------------------------------
 * Function      : void RF_RXSTOP_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : Interrupt routine when receiver stops
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void RF_RXSTOP_IRQHandler(void)
{
    __disable_irq();

    rm_env.rxStatus = RF_REG_READ(DESER_STATUS);
    rm_env.errorHndl = 1;

    Sys_GPIO_Set_Low(rm_env.debug_dio_num[2]);

    if((rm_env.rxStatus & 0xf) == 0x01)
    {
        rm_env.rxLen = RF_REG_READ(RXFIFO_COUNT);

        *((uint8_t *) &rm_env.rxBuff) = RF_REG_READ(RXFIFO);

        /* TODO: Handling packet lengths more than expectation */
        if(rm_env.rxLen > (rm_env.packet_length + 1))
        {
            rm_env.gi = (rm_env.rxLen - (rm_env.packet_length + 1));
            // start reading from next address
            while(rm_env.gi--)
            {
                *((uint8_t *)&rm_env.rxBuff) = RF_REG_READ(RXFIFO);
            }
        }

#if RM_DEBUG
        if(rm_env.state == RM_RX_FIRST)
            Sys_GPIO_Set_Low(rm_env.debug_dio_num[0]);
        else if(rm_env.state == RM_RX_SECOND)
            Sys_GPIO_Set_Low(rm_env.debug_dio_num[1]);
#endif

        /* Expected audio channel */
        if((rm_env.audioChnl << 1) == (rm_env.rxBuff.audio_ch & RM_AUDIO_RIGHT))
        {
            if(rm_env.rxBuff.trans_id & RM_TRANS_ID_N_1)
            {
                rm_env.ptr = rm_env.ptr_payload2;
            }
            else
            {
                rm_env.ptr = rm_env.ptr_payload1;
            }

            rm_env.consecutiveRxTimeoutMax = 1;
            RF_REG_WRITE(MAC_CONF, 0x0);

            Sys_DMA_ChannelConfig(rm_env.dma_memcpy_num,
            RM_DMA_MEMCPY_CONFIG_RX, rm_env.packet_length, 0,
            RF_RX_FIFO, (uint32_t) rm_env.ptr);

            rm_env.sortedPktNum = (rm_env.rxBuff.audio_ch & RM_AUDIO_RIGHT)
                    + ((rm_env.rxBuff.trans_id & RM_TRANS_ID_N_RETRY) << 1)
                    + (rm_env.rxBuff.trans_id & RM_TRANS_ID_N_1);

        	if(rm_env.rxBuff.trans_id & RM_TRANS_ID_N_1)
            {
                rm_env.pktN_1Valid = 1;

                if(rm_env.pktLostCnt < rm_env.pktLostHighThrshld)
                    rm_env.pktLostCnt += 1;
            }
            else
            {
                rm_env.pktNValid = 1;

                if(rm_env.state == RM_RX_FIRST)
                {
                    DIO->BB_RX_SRC = (BB_RF_SYNC_P_SRC_CONST_HIGH
                            | (DIO->BB_RX_SRC & ~DIO_BB_RX_SRC_RF_SYNC_P_Mask));
                    DIO->BB_RX_SRC = (BB_RF_SYNC_P_SRC_CONST_LOW
                            | (DIO->BB_RX_SRC & ~DIO_BB_RX_SRC_RF_SYNC_P_Mask));
                    BBIF->SYNC_CFG = (IDLE | RX_IDLE | SYNC_ENABLE
                            | SYNC_SOURCE_RF_RX);
                }

                if(rm_env.pktLostCnt < rm_env.pktLostHighThrshld)
                    rm_env.pktLostCnt += 2;
            }

        rm_env.errorHndl = 0;
        rm_env.rxPktCnt++;

        rm_env.timerDuration = (rm_env.retrans_time
                + (((rm_env.audioChnl << 1) - (rm_env.sortedPktNum & 0x3))
                        * (rm_env.pktDuration + rm_env.ifs))
                - rm_env.pktDuration - 160);

        while (!(Sys_DMA_Get_ChannelStatus(rm_env.dma_memcpy_num) & DMA_COMPLETE_INT_STATUS));

        if(rm_env.state == RM_SEARCH)
        {
            RF_REG_WRITE(FSM_MODE, 0x8);

            BBIF_COEX_CTRL->RX_ALIAS = 0;
            BBIF_COEX_CTRL->TX_ALIAS = 0;
            RF_SwitchToBLEMode();

            if(rm_env.rxBuff.trans_id & RM_TRANS_ID_N_RETRY)
            {
                rm_env.state = RM_RX_SECOND;
            }
            else
            {
                rm_env.state = RM_RX_FIRST;
            }

            rm_env.event = 1;
            rm_env.eventType = RM_RX_N_RECEIVED;

            rm_env.linkStatus = LINK_ESTABLISHED;
            rm_env.statusChange = 1;
        }
        else if((rm_env.state == RM_RX_FIRST) || (rm_env.state == RM_RX_SECOND))
        {
            if (((rm_env.state == RM_RX_FIRST)
                    && (rm_env.rxBuff.trans_id & RM_TRANS_ID_N_RETRY))
                    || ((rm_env.state == RM_RX_SECOND)
                            && !(rm_env.rxBuff.trans_id & RM_TRANS_ID_N_RETRY))
                    || ((rm_env.audioChnl << 1)
                            != (rm_env.rxBuff.audio_ch & RM_AUDIO_RIGHT))
                    || (rm_env.rxBuff.trans_id & RM_TRANS_ID_N_1) )
            {
                RF_REG_WRITE(FSM_MODE, 0x8);

                BBIF_COEX_CTRL->RX_ALIAS = 0;
                BBIF_COEX_CTRL->TX_ALIAS = 0;
                RF_SwitchToBLEMode();

                rm_env.proErrCnt++;
            }

            if((rm_env.rxBuff.trans_id & RM_TRANS_ID_N_1)
                    || (rm_env.pktN_1Valid
                            && !(rm_env.rxBuff.trans_id & RM_TRANS_ID_N_1)))
            {
                RF_REG_WRITE(FSM_MODE, 0x8);

                BBIF_COEX_CTRL->RX_ALIAS = 0;
                BBIF_COEX_CTRL->TX_ALIAS = 0;
                RF_SwitchToBLEMode();
            }

            switch(rm_env.rxBuff.trans_id & 0x3)
            {
                case RM_TRANS_ID_N:
                    rm_env.rxPktNCnt++;
                    break;
                case RM_TRANS_ID_N_1:
                    rm_env.rxPktN_1Cnt++;
                    break;
                case RM_TRANS_ID_N_RETRY:
                    rm_env.rxPktNReCnt++;
                    break;
                case RM_TRANS_ID_N_1_RETRY:
                    rm_env.rxPktN_1ReCnt++;
                    break;
                default:
                    break;
            }
        }
        else
        {
            rm_env.proErrCnt++;
            rm_env.errorHndl = 1;
        }

        Sys_Timers_Stop(SELECT_TIMER0);
        Sys_Timer_Set_Control(0, TIMER_FREE_RUN | TIMER_PRESCALE_1 |
        TIMER_SLOWCLK_DIV2 | (rm_env.timerDuration));
        Sys_Timers_Start(SELECT_TIMER0);
    }
    else
    {
        rm_env.proErrCnt++;
        rm_env.errorHndl=1;
    }
}

    if(rm_env.errorHndl)
    {
        BBIF->SYNC_CFG = (IDLE | RX_IDLE | SYNC_ENABLE | SYNC_SOURCE_RF_RX);

        if((rm_env.rxStatus & 0xf) == 0x0)
        {
            /* Could be timeout */
            rm_env.rxErrCnt++;

            RF_REG_WRITE(TIMEOUT, 0x94);
            RF_REG_WRITE(MAC_CONF, 0x0);
        }
        else if((rm_env.rxStatus & 0xf) == 0x02)
        {
            rm_env.crcErrCnt++;

            rm_env.rxLen = RF_REG_READ(RXFIFO_COUNT);

            if((rm_env.rxBuff.trans_id & RM_TRANS_ID_N_1) && !rm_env.pktN_1Valid)
            {
                rm_env.pktN_1BadCRC = 1;

            }
            else if(!rm_env.pktN_1Valid)
            {
                rm_env.pktNBadCRC = 1;

            }
        }
        else if((rm_env.rxStatus & 0xf) == 0x08)
        {
            rm_env.pktLenErrCnt++;
        }
        else if((rm_env.rxStatus & 0xf) == 0x04)
        {
            /* address_err, should be zero */
            rm_env.adrsErrCnt++;
        }

        if(rm_env.state == RM_SEARCH)
        {
            rm_env.linkStatus = LINK_ESTABLISHMENT_UNSUCCESS;
            rm_env.statusChange = 1;
        }
        else if(rm_env.state == RM_RX_FIRST || rm_env.state == RM_RX_SECOND)
        {
            if(rm_env.state == RM_RX_FIRST)
            {
                rm_env.event = 1;
                rm_env.eventType = RM_RX_MAIN_TIMEOUT;
            }
            else
            {
                rm_env.event = 1;
                rm_env.eventType = RM_RX_RETRANSMIT_TIMEOUT;
            }

            if(rm_env.pktLostCnt)
                rm_env.pktLostCnt -= 1;

            if(rm_env.pktLostCnt < rm_env.pktLostLowThrshld)
            {
                rm_env.linkStatus = LINK_DISCONNECTED;
                if(rm_env.linkStatus != rm_env.oldLinkStatus)
                {
                    rm_env.statusChange = 1;
                }

                rm_env.linkLost_count++;

                RF_REG_WRITE(FSM_MODE, 0x8);
                RemoteMic_Protocol_Init();
                RM_Enable(2000);
                BBIF_COEX_CTRL->RX_ALIAS = 0;
                BBIF_COEX_CTRL->TX_ALIAS = 0;
                RF_SwitchToBLEMode();
                __enable_irq();

                return;
            }
        }

        if(rm_env.state == RM_SEARCH)
        {
            RF_REG_WRITE(FSM_MODE, 0x8);

            BBIF_COEX_CTRL->RX_ALIAS = 0;
            BBIF_COEX_CTRL->TX_ALIAS = 0;
            RF_SwitchToBLEMode();
        }

        rm_env.consecutiveRxTimeout++;
        if(rm_env.pktN_1Valid)
        {
            RF_REG_WRITE(FSM_MODE, 0x8);

            BBIF_COEX_CTRL->RX_ALIAS = 0;
            BBIF_COEX_CTRL->TX_ALIAS = 0;
            RF_SwitchToBLEMode();
        }
        else if(rm_env.pktNValid && rm_env.state == RM_RX_FIRST)
        {
            RF_REG_WRITE(FSM_MODE, 0x8);

            BBIF_COEX_CTRL->RX_ALIAS = 0;
            BBIF_COEX_CTRL->TX_ALIAS = 0;
            RF_SwitchToBLEMode();
        }
        else if((rm_env.consecutiveRxTimeout >= rm_env.consecutiveRxTimeoutMax))
        {
            RF_REG_WRITE(FSM_MODE, 0x8);

            BBIF_COEX_CTRL->RX_ALIAS = 0;
            BBIF_COEX_CTRL->TX_ALIAS = 0;
            RF_SwitchToBLEMode();
        }
    }

    __enable_irq();
}

/* ----------------------------------------------------------------------------
 * Function      : void RF_TX_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : Interrupt routine for the end of transmission
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void RF_TX_IRQHandler(void)
{
    __disable_irq();

    rm_env.gtmp = RF_REG_READ(IRQ_STATUS);

#if RM_DEBUG
    Sys_GPIO_Set_Low(rm_env.debug_dio_num[0]);
#endif

    rm_env.txPktCnt++;

    if((rm_env.trackCnt + 1) == RM_PACKETS_PER_INTERVAL)
    {
        RF_REG_WRITE(FSM_MODE, 0x8);

        BBIF_COEX_CTRL->RX_ALIAS = 0;
        BBIF_COEX_CTRL->TX_ALIAS = 0;
        RF_SwitchToBLEMode();
    }
    else
    {
    	rm_env.trackCnt += 1;

        RF_REG_WRITE(TXFIFO,
                RM_PrepareHeader(rm_env.trackCnt,
                        (uint8_t ** )&rm_env.ptr_ptr));

        Sys_DMA_ChannelConfig(rm_env.dma_memcpy_num,
        RM_DMA_MEMCPY_CONFIG_TX, rm_env.packet_length, 0,
                (uint32_t) &rm_env.ptr_ptr[0],
                RF_TX_FIFO);

        while (!(Sys_DMA_Get_ChannelStatus(rm_env.dma_memcpy_num) & DMA_COMPLETE_INT_STATUS));

#if RM_DEBUG
        Sys_GPIO_Set_High(rm_env.debug_dio_num[0]);
#endif
    }

    __enable_irq();
}

/* ----------------------------------------------------------------------------
 * Function      : void RM_TransmitPacket(void)
 * ----------------------------------------------------------------------------
 * Description   : Pre-transmission processing and enable Transmitter
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void RM_TransmitPacket(void)
{
    /* Flush FIFO */
    RF_REG_WRITE(TXFIFO_STATUS, 0x1);

    RF_REG_WRITE(IRQ_CONF, 0x1);

    rm_env.trackCnt = 0;

    rm_env.gchnl = rm_env.hoplist[((rm_env.interval_counter + rm_env.stepSize)
            % rm_env.numChnlInHopList)];

    RF_Reg_WriteBurst(CENTER_FREQ, 4, (uint8_t *) &rf_freq_table[rm_env.gchnl]);

    rm_env.interval_counter++;

    if(rm_env.state == RM_TX_FIRST)
    {
        if(rm_env.state == RM_TX_FIRST)
        {
            BBIF->SYNC_CFG = (ACTIVE | RX_ACTIVE | SYNC_ENABLE
                    | SYNC_SOURCE_RF_RX);
            DIO->BB_RX_SRC = (BB_RF_SYNC_P_SRC_CONST_HIGH
                    | (DIO->BB_RX_SRC & ~DIO_BB_RX_SRC_RF_SYNC_P_Mask));
            DIO->BB_RX_SRC = (BB_RF_SYNC_P_SRC_CONST_LOW
                    | (DIO->BB_RX_SRC & ~DIO_BB_RX_SRC_RF_SYNC_P_Mask));
            BBIF->SYNC_CFG = (IDLE | RX_IDLE | SYNC_ENABLE | SYNC_SOURCE_RF_RX);
        }
    }

    RF_REG_WRITE(TXFIFO,
            RM_PrepareHeader(rm_env.trackCnt, (uint8_t ** )&rm_env.ptr_ptr));

    Sys_DMA_ChannelConfig(rm_env.dma_memcpy_num,
    RM_DMA_MEMCPY_CONFIG_TX, rm_env.packet_length, 0,
            (uint32_t) &rm_env.ptr_ptr[0],
            RF_TX_FIFO);

    while (!(Sys_DMA_Get_ChannelStatus(rm_env.dma_memcpy_num) & DMA_COMPLETE_INT_STATUS));

#if RM_DEBUG
    Sys_GPIO_Set_High(rm_env.debug_dio_num[0]);
#endif

    RF_REG_WRITE(FSM_MODE, 0x7);
}

/* ----------------------------------------------------------------------------
 * Function      : void RM_ReceivePacket(void)
 * ----------------------------------------------------------------------------
 * Description   : Pre-receiving processing and enable Receiver
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void RM_ReceivePacket(void)
{
    uint8_t mac_conf = 0x20;
    uint8_t timeout = 0x94;

	rm_env.consecutiveRxTimeout = 0;
    rm_env.consecutiveRxTimeoutMax = 2;


    switch(rm_env.state)
    {
        case RM_SEARCH:

            Sys_Timers_Stop(SELECT_TIMER0);
            Sys_Timer_Set_Control(0,
                    TIMER_FREE_RUN | TIMER_PRESCALE_1 |
                    TIMER_SLOWCLK_DIV2
                            | (rm_env.retrans_time - rm_env.pktDuration / 2));
            Sys_Timers_Start(SELECT_TIMER0);

            if(!rm_env.searchTryCnt && !rm_env.waitCnt)
            {
                /* 1ms * (timeout value+1) */
            	timeout = 0xb1;
            }

            rm_env.searchTryCnt++;
            if(rm_env.searchTryCnt >= rm_env.searchTryCntThrshld)
            {
                rm_env.state = RM_READY;
                return;
            }

            rm_env.gchnl = rm_env.hoplist[0];

            break;

        case RM_READY:

            rm_env.waitCnt++;
            rm_env.searchTryCnt = 0;

            Sys_Timers_Stop(SELECT_TIMER0);
            Sys_Timer_Set_Control(0,
                    TIMER_FREE_RUN | TIMER_PRESCALE_1 |
                    TIMER_SLOWCLK_DIV2
                            | (rm_env.retrans_time * rm_env.waitCntGranularity));
            Sys_Timers_Start(SELECT_TIMER0);

            rm_env.state = RM_SEARCH;
            if(rm_env.waitCnt < 3)
            {
                /* (timeout value + 1) * 64us */
                timeout = 0x9f;
            }
            else
            {
                /* (timeout value + 1) * 64us */
                timeout = 0x9c;
            }

            rm_env.gchnl = rm_env.hoplist[0];

            break;

        case RM_RX_SECOND:

            rm_env.interval_counter++;

            Sys_Timers_Stop(SELECT_TIMER0);
            Sys_Timer_Set_Control(0, TIMER_FREE_RUN | TIMER_PRESCALE_1 |
            TIMER_SLOWCLK_DIV2 | (rm_env.retrans_time - 70));
            Sys_Timers_Start(SELECT_TIMER0);

            /* (timeout value + 1) * 64us */
            timeout = 0x95;

            rm_env.state = RM_RX_FIRST;
            rm_env.gchnl = rm_env.hoplist[((rm_env.interval_counter
                    + rm_env.stepSize) % rm_env.numChnlInHopList)];

            rm_env.event = 1;
            rm_env.eventType = RM_RX_MAIN_START;

            BBIF->SYNC_CFG = (ACTIVE | RX_ACTIVE | SYNC_ENABLE
                    | SYNC_SOURCE_RF_RX);

            if(rm_env.pktN_1Valid)
            {
                rm_env.consecutiveRxTimeoutMax = 1;

                /* (timeout value + 1) * 64us */
                timeout = 0x92;

                mac_conf = 0;
            }

            break;

        case RM_RX_FIRST:

            rm_env.interval_counter++;

            Sys_Timers_Stop(SELECT_TIMER0);
            Sys_Timer_Set_Control(0, TIMER_FREE_RUN | TIMER_PRESCALE_1 |
            TIMER_SLOWCLK_DIV2 | (rm_env.retrans_time - 1)); //
            Sys_Timers_Start(SELECT_TIMER0);

            /* (timeout value + 1) * 64us */
            timeout = 0x95;

            rm_env.state = RM_RX_SECOND;

            Sys_Timers_Stop(SELECT_TIMER1);
            Sys_Timer_Set_Control(1, TIMER_SHOT_MODE | TIMER_PRESCALE_1 |
            TIMER_SLOWCLK_DIV2 | rm_env.renderDelay);
            Sys_Timers_Start(SELECT_TIMER1);

            if(rm_env.pktN_1Valid)
            {
                rm_env.consecutiveRxTimeoutMax = 1;

                /* (timeout value + 1) * 64us */
                timeout = 0x92;

                mac_conf = 0;
            }

            if(rm_env.pktNValid && rm_env.pktN_1Valid)
            {
                return;
            }

            if(rm_env.pktNValid && !rm_env.pktN_1Valid)
            {
                rm_env.state = RM_WAIT_FOR_RX_START_SECOND_DELAYED;

                Sys_Timers_Stop(SELECT_TIMER1);
                Sys_Timer_Set_Control(1, TIMER_SHOT_MODE | TIMER_PRESCALE_1 |
                TIMER_SLOWCLK_DIV2 | (rm_env.pktDuration + rm_env.ifs));
                Sys_Timers_Start(SELECT_TIMER1);

                return;
            }

            rm_env.gchnl = rm_env.hoplist[((rm_env.interval_counter
                    + rm_env.stepSize) % rm_env.numChnlInHopList)];
            break;

        case RM_WAIT_FOR_RX_START_SECOND_DELAYED:

            /* (timeout value + 1) * 64us */
            timeout = 0x92;

            mac_conf = 0;

            rm_env.state = RM_RX_SECOND;

            Sys_Timers_Stop(SELECT_TIMER1);
            Sys_Timer_Set_Control(1, TIMER_SHOT_MODE | TIMER_PRESCALE_1 |
            TIMER_SLOWCLK_DIV2 | (rm_env.pktDuration + 140));
            Sys_Timers_Start(SELECT_TIMER1);

            rm_env.gchnl = rm_env.hoplist[((rm_env.interval_counter
                    + rm_env.stepSize) % rm_env.numChnlInHopList)];
            break;

        default:
            break;
    }

    if(BBIF_COEX_STATUS->BLE_IN_PROCESS_ALIAS)
        return;

    __disable_irq();

    BBIF_COEX_CTRL->RX_ALIAS = 1;
    BBIF_COEX_CTRL->TX_ALIAS = 1;

    Sys_GPIO_Set_High(rm_env.debug_dio_num[2]);
    rm_env.rxStatus = RF_REG_READ(DESER_STATUS);

    while(BBIF_COEX_STATUS->BLE_IN_PROCESS_ALIAS);
    Sys_Delay_ProgramROM(512);

    RF_REG_WRITE(TIMEOUT, timeout);
    RF_REG_WRITE(MAC_CONF, mac_conf);

    RF_REG_WRITE(FSM_MODE, 0x8);
    RF_SwitchToCPMode();
    RF_REG_WRITE(RXFIFO_STATUS, 0x1);
    RF_Reg_WriteBurst(CENTER_FREQ, 4, (uint8_t *) &rf_freq_table[rm_env.gchnl]);
    RF_REG_WRITE(FSM_MODE, 0x3);

    __enable_irq();

#if RM_DEBUG
    if(rm_env.state == RM_RX_FIRST)
    {
        Sys_GPIO_Set_Low(rm_env.debug_dio_num[0]);
        Sys_GPIO_Set_Low(rm_env.debug_dio_num[0]);
        Sys_GPIO_Set_High(rm_env.debug_dio_num[0]);
    }
    if(rm_env.state == RM_RX_SECOND)
    {
        Sys_GPIO_Set_Low(rm_env.debug_dio_num[1]);
        Sys_GPIO_Set_Low(rm_env.debug_dio_num[1]);
        Sys_GPIO_Set_High(rm_env.debug_dio_num[1]);
    }
#endif
}

/* ----------------------------------------------------------------------------
 * Function      : void RemoteMic_Protocol_Init(void)
 * ----------------------------------------------------------------------------
 * Description   : Reset state machine and the protocol global variables and
 *                 start the protocol
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : Parameters for the protocol have been set
 * ------------------------------------------------------------------------- */
void RemoteMic_Protocol_Init(void)
{
    RF_REG_WRITE(FSM_MODE, 0x8);

    rm_env.interval_counter = 0xffffffff;
    rm_env.ble_offset = 500;

    rm_env.oldChnl = 0;

    rm_env.rxErrCnt = 0;
    rm_env.crcErrCnt = 0;
    rm_env.pktLenErrCnt = 0;
    rm_env.proErrCnt = 0;
    rm_env.rxPktCnt = 0;
    rm_env.txPktCnt = 0;
    rm_env.adrsErrCnt = 0;
    rm_env.inputCnt = 0;
    rm_env.rxPktNCnt = 0;
    rm_env.rxPktN_1Cnt = 0;
    rm_env.rxPktNReCnt = 0;
    rm_env.rxPktN_1ReCnt = 0;
    rm_env.searchTryCnt = 0;
    rm_env.waitCnt = 0;

    rm_env.pktLostCnt = rm_env.pktLostHighThrshld;

    rm_env.event = 0;

    rm_env.state = RM_IDLE;
}

/* ----------------------------------------------------------------------------
 * Function      : void TIMER0_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : First timer interrupt routine for the first transmit
 *                 or receive
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void TIMER0_IRQHandler(void)
{
    __disable_irq();

    if(rm_env.role == RM_MASTER_ROLE)
    {
        BBIF_COEX_CTRL->RX_ALIAS = 1;
        BBIF_COEX_CTRL->TX_ALIAS = 1;
        while(BBIF_COEX_STATUS->BLE_IN_PROCESS_ALIAS);
        RF_SwitchToCPMode();

        if(rm_env.state == RM_READY)
        {
            Sys_Timers_Stop(SELECT_TIMER0);
            Sys_Timer_Set_Control(0, TIMER_FREE_RUN | TIMER_PRESCALE_1 |
            TIMER_SLOWCLK_DIV2 | (rm_env.retrans_time - 1));
            Sys_Timers_Start(SELECT_TIMER0);

            rm_env.state = RM_TX_FIRST;
            RM_TransmitPacket();

            Sys_Timers_Stop(SELECT_TIMER1);

            Sys_Timer_Set_Control(1, TIMER_SHOT_MODE | TIMER_PRESCALE_1 |
            TIMER_SLOWCLK_DIV2 | ((rm_env.interval_time - 1300)));

            Sys_Timers_Start(SELECT_TIMER1);
        }
        else
        {
            rm_env.state =
                    (rm_env.state == RM_TX_FIRST) ? RM_TX_SECOND : RM_TX_FIRST;
            RM_TransmitPacket();

            if(rm_env.state == RM_TX_FIRST)
            {
                Sys_Timers_Stop(SELECT_TIMER1);

                Sys_Timer_Set_Control(1,
                        TIMER_SHOT_MODE | TIMER_PRESCALE_1 |
                        TIMER_SLOWCLK_DIV2
                                | ((rm_env.interval_time - rm_env.preFetchDelay)));

                Sys_Timers_Start(SELECT_TIMER1);
            }
        }
    }
    else
    {
        RM_ReceivePacket();
    }

    __enable_irq();
}
extern uint8_t RetoneMODE[4];
/* ----------------------------------------------------------------------------
 * Function      : void TIMER1_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : Second timer interrupt routine for retransmission handling
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void TIMER1_IRQHandler(void)
{
	if(RetoneMODE[0]==0)
	{
		if(rm_env.role == RM_MASTER_ROLE)
		{
			Sys_Timers_Stop(SELECT_TIMER1);

			rm_env.intf.trx_event(RM_SWPLL_SYNC, &rm_env.gi, rm_env.ptr);

			memcpy(&rm_env.payload.tx.rm_payload_left_2[0],
					&rm_env.payload.tx.rm_payload_left_1[0], rm_env.packet_length);
			memcpy(&rm_env.payload.tx.rm_payload_right_2[0],
					&rm_env.payload.tx.rm_payload_right_1[0], rm_env.packet_length);

			if(rm_env.payloadFlowRequest == RM_PRO_REQUEST)
			{
				rm_env.intf.trx_event(RM_TX_PAYLOAD_READY_LEFT,
						&rm_env.packet_length,
						&rm_env.payload.tx.rm_payload_left_1[0]);
				rm_env.intf.trx_event(RM_TX_PAYLOAD_READY_RIGHT,
						&rm_env.packet_length,
						&rm_env.payload.tx.rm_payload_right_1[0]);
			}
		}
		else
		{
			if(rm_env.state == RM_RX_SECOND)
			{
				rm_env.ptr = rm_env.ptr_payload2;
				rm_env.ptr_payload2 = rm_env.ptr_payload1;
				rm_env.ptr_payload1 = rm_env.ptr;

				rm_env.pktN_2Valid = rm_env.pktN_1Valid;
				rm_env.pktN_1Valid = rm_env.pktNValid;
				rm_env.pktNValid = 0;

				rm_env.pktN_2BadCRC = rm_env.pktN_1BadCRC;
				rm_env.pktN_1BadCRC = rm_env.pktNBadCRC;
				rm_env.pktNBadCRC = 0;

				if(rm_env.pktN_2Valid)
				{
					rm_env.intf.trx_event(RM_RX_TRANSFER_GOODPKT,
							&rm_env.packet_length, rm_env.ptr);
				}
				else if(rm_env.pktN_2BadCRC)
				{
					rm_env.intf.trx_event(RM_RX_TRANSFER_BADCRCPKT,
							&rm_env.packet_length, rm_env.ptr);
				}
				else
				{
					rm_env.intf.trx_event(RM_RX_TRANSFER_NOPKT,
							&rm_env.packet_length, rm_env.ptr);
				}
			}
			else
			{
				__disable_irq();

				RM_ReceivePacket();

				__enable_irq();
			}
		}
	}
}

/* ----------------------------------------------------------------------------
 * Function      : void RF_InitRegistersCustomMode(void)
 * ----------------------------------------------------------------------------
 * Description   : Set RF registers for packet handling mode of RF block
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : All of registers have been set
 * ------------------------------------------------------------------------- */
inline void RF_InitRegistersCustomMode(void)
{
    RF_REG_WRITE(FSM_MODE, 0x8);
    RF_REG_WRITE(MODE_2, 0x0);
    RF_REG_WRITE(TIMEOUT, 0x0);
    RF_REG_WRITE(PACKET_HANDLING, 0xf3);
    RF_REG_WRITE(PREAMBLE, rm_env.preamble);
    RF_REG_WRITE(PREAMBLE_LENGTH, 0);
    RF_Reg_WriteBurst(PATTERN, 4, (uint8_t *) &rm_env.accessword);
    RF_REG_WRITE(PACKET_EXTRA, 0x3);
    RF_REG_WRITE(CODING, 0x0);
    RF_Reg_WriteBurst(CRC_POLYNOMIAL, 4, (uint8_t *) &rm_env.crc_poly);
    rm_env.gtmp = 0x00ffff;
    RF_Reg_WriteBurst(CRC_RST, 4, (uint8_t *) &rm_env.gtmp);

    RF_REG_WRITE(PACKET_LENGTH, (rm_env.packet_length + 1));
    RF_REG_WRITE(PACKET_LENGTH_OPTS, 0x40);
    RF_REG_WRITE(IRQ_CONF, 0x0);

    if(rm_env.mod_idx == BT_MOD_IDX)
    {
        RF_REG_WRITE(TX_MULT, 0x31);
        RF_REG_WRITE(FILTER_GAIN, 0x28);
    }
    else
    {
        RF_REG_WRITE(TX_MULT, 0x3b);
        RF_REG_WRITE(FILTER_GAIN, 0x3f);
    }

    if(rm_env.role == RM_MASTER_ROLE)
    {
        RF_REG_WRITE(MAC_CONF, 0x6);
        RF_REG_WRITE(TX_MAC_TIMER, 26);
    }
    else
    {
        RF_REG_WRITE(IRQ_CONF, 0x2);
        /* RX rampup should be considered */
        RF_REG_WRITE(RX_MAC_TIMER, 6);
        RF_REG_WRITE(MAC_CONF, 0x20);
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void RF_Reg_WriteBurst(uint16_t addr, uint8_t size, 
 *                                        uint8_t *data)
 * ----------------------------------------------------------------------------
 * Description   : Write a burst of data into RF registers
 * Inputs        : - addr       - Address of the first register
 *                 - size       - The number of registers to be written
 *                 - data       - Pointer to data that should be written
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
inline void RF_Reg_WriteBurst(uint16_t addr, uint8_t size, uint8_t *data)
{
    uint8_t i;

    for(i = 0; i < size; i++)
    {
        RF_REG_WRITE((addr + i), data[i]);
    }
}

/* ----------------------------------------------------------------------------
 * Function      : uint8_t RM_PrepareHeader(uint8_t cnt, uint8_t **ptr)
 * ----------------------------------------------------------------------------
 * Description   : Prepare the header for packets
 * Inputs        : - cnt       - The packet counter
 *                 - ptr       - Pointer to the data that should be sent
 * Outputs       : return value - The prepared header
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
uint8_t RM_PrepareHeader(uint8_t cnt, uint8_t **ptr)
{
    volatile uint8_t header;

    if(rm_env.state == RM_TX_SECOND)
    {
        cnt = (cnt + RM_PACKETS_PER_INTERVAL);

    }
    else
    {
        header = (cnt + RM_PACKETS_PER_INTERVAL);
    }

    switch(cnt)
    {
        case 0:
            header = N_LEFT;
            *ptr = rm_env.payload.tx.rm_payload_left_1;
            break;
        case 1:
            header = N_1_LEFT;
            *ptr = rm_env.payload.tx.rm_payload_left_2;
            break;
        case 2:
            header = N_RIGHT;
            *ptr = rm_env.payload.tx.rm_payload_right_1;
            break;
        case 3:
            header = N_1_RIGHT;
            *ptr = rm_env.payload.tx.rm_payload_right_2;
            break;
        case 4:
            header = N_LEFT_RETRY;
            *ptr = rm_env.payload.tx.rm_payload_left_1;
            break;
        case 5:
            header = N_1_LEFT_RETRY;
            *ptr = rm_env.payload.tx.rm_payload_left_2;
            break;
        case 6:
            header = N_RIGHT_RETRY;
            *ptr = rm_env.payload.tx.rm_payload_right_1;
            break;
        case 7:
            header = N_1_RIGHT_RETRY;
            *ptr = rm_env.payload.tx.rm_payload_right_2;
            break;
        default:
            break;
    }

    return header;
}

/* ----------------------------------------------------------------------------
 * Function      : void RF_SwitchToBLEMode(void)
 * ----------------------------------------------------------------------------
 * Description   : Prepare RF for BLE when the protocol event ends
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
inline void RF_SwitchToBLEMode (void)
{
    RF_REG_WRITE(IRQ_CONF, 0x0);
	RF_REG_WRITE(BANK, 0x1);
	RF_REG_WRITE((CARRIER_RECOVERY_EXTRA+1),0x3f);
	RF_REG_WRITE(MODE_2, 0x08);
	RF_REG_WRITE(MODE, 0x22);
	RF_REG_WRITE(MAC_CONF, 0x80);
	RF_REG_WRITE(TIMINGS_4,0x61);
	RF_REG_WRITE(TIMEOUT, 0);
	RF_REG_WRITE(PACKET_HANDLING, 0x82);
    RF_REG_WRITE(TX_MULT, 0x3b);
    RF_REG_WRITE(FILTER_GAIN, 0x3f);
    RF_REG_WRITE(TX_MULT, 0x3b);
    RF_REG_WRITE(FILTER_GAIN, 0x3f);
}

/* ----------------------------------------------------------------------------
 * Function      : void RF_SwitchToCPMode(void)
 * ----------------------------------------------------------------------------
 * Description   : Prepare RF for CP when the protocol event ends
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
inline void RF_SwitchToCPMode (void)
{
    RF_REG_WRITE(BANK, 0x1);

    if(rm_env.role == RM_MASTER_ROLE)
    {
        RF_REG_WRITE(MAC_CONF, 0x6);
    }
    else
    {
    	RF_Reg_WriteBurst(PATTERN, 4 , (uint8_t *)&rm_env.accessword);
        RF_REG_WRITE(IRQ_CONF, 0x2);
    }

    RF_REG_WRITE(MODE, 0);
    RF_REG_WRITE(MODE, 0x22);
    RF_REG_WRITE(MODE_2, 0x0);
    RF_REG_WRITE(TIMINGS_4,0x51);
    RF_REG_WRITE((CARRIER_RECOVERY_EXTRA+1),0x70);
    RF_REG_WRITE(PACKET_HANDLING, 0xf3);

    if(rm_env.mod_idx == BT_MOD_IDX)
    {
        RF_REG_WRITE(TX_MULT, 0x31);
        RF_REG_WRITE(FILTER_GAIN, 0x28);
    }
    else
    {
        RF_REG_WRITE(TX_MULT, 0x3b);
        RF_REG_WRITE(FILTER_GAIN, 0x3f);
    }
}
