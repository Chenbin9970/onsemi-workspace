#if 0
/* ----------------------------------------------------------------------------
 * Copyright (c) 2015-2017 Semiconductor Components Industries, LLC (d/b/a
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
 * rm_app.c
 * - Remote mic application
 * ----------------------------------------------------------------------------
 * $Revision: 1.12 $
 * $Date: 2019/12/27 18:50:38 $
 * ------------------------------------------------------------------------- */

#include "app.h"

#ifdef APP_RM_ENABLE

#ifdef DEBUG_UART_ENABLE
#include <printf.h>
#define RM_PRINTF(...) PRINTF(__VA_ARGS__)
#else
#define RM_PRINTF(...)
#endif

uint32_t data_rd = 0;

/* For Test */
uint8_t tmp;
//uint32_t ascc_cnt, audio_sink_phase_cnt, erraaa = 0;

uint8_t inTempBuffLeft[100]  = {
    0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf,
    0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf
};
uint8_t inTempBuffRight[100] = {
    0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc,
    0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc
};
uint8_t outTempBuff[100];
uint16_t app_sendCntrRight = 0, app_sendCntrLeft = 0, app_receiveCntr = 0;
uint32_t app_err1 = 0, app_err2 = 0, app_err3 = 0, app_err4 = 0, app_err5 = 0,
         app_err6 = 0, app_err7 = 0;

extern struct app_env_tag app_env;

const uint8_t coded_sample[4 * 60] = {
    0xb1, 0x5b, 0x5d, 0xdf, 0xef, 0x7b, 0xb7, 0xff, 0x3c, 0xff, 0xbf, 0x3b,
    0xff, 0xcb, 0x5c, 0xb7,
    0xbb, 0x5d, 0xfa, 0xc7, 0x7d, 0xf3, 0xef, 0x7e, 0xbb, 0xd7, 0xbd, 0xff,
    0xe9, 0xff, 0xfb, 0x7f,
    0xe6, 0xdf, 0x59, 0xd5, 0xd6, 0xfd, 0x35, 0xd3, 0xf9, 0x14, 0x9f, 0x64,
    0xf7, 0x9b, 0x55, 0x3f,
    0x99, 0x49, 0xbf, 0xd5, 0x6d, 0xbf, 0xd7, 0x6b, 0x75, 0xbd, 0x7f, 0xb7,

    0xfa, 0xe7, 0xee, 0x32, 0x7a, 0x1a, 0x4e, 0xd7, 0x5e, 0xf7, 0xef, 0x7f,
    0xdf, 0xef, 0xbe, 0x5b,
    0xed, 0xf6, 0xdb, 0xfd, 0xb5, 0xdf, 0x5d, 0xa7, 0xd7, 0x79, 0xb6, 0xfe,
    0x79, 0xfe, 0xbf, 0xef,
    0x7e, 0xfa, 0xbf, 0x7b, 0xfb, 0xcb, 0x5b, 0xfa, 0xd7, 0x3f, 0x76, 0xf7,
    0x77, 0xde, 0xf5, 0xb5,
    0xfd, 0x5d, 0x36, 0x93, 0x6d, 0x76, 0xdb, 0x4f, 0xe5, 0xff, 0x79, 0xfe,

    0x9f, 0xd3, 0x6d, 0x36, 0xda, 0x18, 0x57, 0x47, 0xdd, 0xde, 0xff, 0xd6,
    0xd1, 0xdf, 0xfe, 0xbb,
    0xef, 0xb6, 0xf1, 0xef, 0x2f, 0xdb, 0xe5, 0x7e, 0x5b, 0xfd, 0x65, 0xfd,
    0xff, 0xff, 0xb3, 0xff,
    0xad, 0xed, 0xcd, 0xed, 0xd9, 0xfd, 0xb7, 0x5b, 0x59, 0xb4, 0xfd, 0x6f,
    0xbd, 0xba, 0xdf, 0x3e,
    0xef, 0xcf, 0x7f, 0xfe, 0x7d, 0x77, 0x57, 0x65, 0x24, 0xff, 0x6b, 0xbe,

    0xf5, 0xdb, 0x0c, 0xb4, 0xac, 0x63, 0xa5, 0xdd, 0xf7, 0x5d, 0xef, 0xad,
    0xfb, 0xff, 0xde, 0xfd,
    0xdb, 0x7f, 0xff, 0xd7, 0xfd, 0xff, 0xdf, 0xee, 0xf9, 0xef, 0xfe, 0xfb,
    0xff, 0xef, 0xff, 0xe5,
    0xae, 0xd6, 0xfd, 0xf5, 0xff, 0x6d, 0x76, 0xdb, 0x6d, 0x76, 0xd6, 0x6d,
    0xb4, 0xff, 0x5d, 0x75,
    0xff, 0x59, 0x77, 0xdf, 0x4f, 0xb6, 0xd7, 0xe9, 0x6e, 0x9e, 0x7d, 0xff
};
uint32_t coded_cntr = 0;

void APP_RM_Init(uint8_t side)
{
    struct rm_callback callback;

    uint8_t temp[16] = RM_HOPLIST;

    app_env.rm_link_status              = LINK_DISCONNECTED;
    app_env.rm_lostLink_counter         = 0;
    app_env.rm_unsuccessLink_counter     = 0;
    app_env.audio_streaming             = 0;

    app_env.rm_param.audioChnl          = side;
    app_env.rm_param.role = RM_SLAVE_ROLE;
    app_env.rm_param.interval_time      = 10000;
    app_env.rm_param.retrans_time       = 5000;
    app_env.rm_param.audio_rate         = 48;
    app_env.rm_param.radio_rate         = 2000;
    app_env.rm_param.scan_time          = 6500;
    app_env.rm_param.preamble           = 0x55;
    app_env.rm_param.accessword         = (0x00cde629 | (0xf2 << 24));

    app_env.rm_param.payloadFlowRequest = APP_RM_DATA_REQUEST_TYPE;
    app_env.rm_param.renderDelay        = 200;

    if (app_env.rm_param.payloadFlowRequest == RM_APP_REQUEST)
    {
        app_env.rm_param.preFetchDelay = 1300;
    }
    else
    {
        app_env.rm_param.preFetchDelay = 400;
    }

    app_env.rm_param.pktLostLowThrshld     = 10;
    app_env.rm_param.pktLostHighThrshld    = 200;
    app_env.rm_param.pktLostLowThrshldSlow = 1;

    app_env.rm_param.searchTryCntThrshld   = 20;
    app_env.rm_param.waitCntGranularity    = 200;

    app_env.rm_param.stepSize = 1;
    app_env.rm_param.numChnlInHopList = 7;

    app_env.rm_param.mod_idx  = BLE_MOD_IDX;
    app_env.rm_param.dma_memcpy_num   = MEMCPY_DMA_NUM;

    app_env.rm_param.debug_dio_num[0] = DEBUG_DIO_FIRST;
    app_env.rm_param.debug_dio_num[1] = DEBUG_DIO_SECOND;
    app_env.rm_param.debug_dio_num[2] = 0xff;
    app_env.rm_param.debug_dio_num[3] = 0xff;

    memcpy(app_env.rm_param.hopList, temp, 16);

    callback.trx_event     = RM_Callback_TRX;
    callback.status_update = RM_Callback_StatusUpdate;

    RM_Configure(&app_env.rm_param, callback);

    rm_env.intf.status_update(LINK_DISCONNECTED);
}

uint8_t cntr_enc_rm0 = 0, cntr_enc_rm1 = 0;
uint8_t RM_Callback_TRX(uint8_t type, uint8_t *length, uint8_t *ptr)
{
    switch (type)
    {
        case RM_TX_PAYLOAD_READY_LEFT:
        {
        }
        break;

        case RM_TX_PAYLOAD_READY_RIGHT:
        {
        }
        break;

        case RM_RX_TRANSFER_GOODPKT:
        case RM_RX_TRANSFER_BADCRCPKT:
        case RM_RX_TRANSFER_NOPKT:
        {
            static uint32_t trx_cnt = 0;
            trx_cnt++;
#if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT)
            if ((*length) == 0)
            {
                if ((trx_cnt & 0x7F) == 0) RM_PRINTF("TRX%d: NOPKT len=0\r\n", trx_cnt);
                memset(outTempBuff, 0xaa, ((app_env.rm_param.audio_rate *
                                            app_env.rm_param.interval_time) /
                                           8000));
                app_err1++;
            }
            else
            {
                if ((trx_cnt & 0x7F) == 0) RM_PRINTF("TRX%d: DATA len=%d\r\n", trx_cnt, *length);
                memcpy(outTempBuff, ptr, *length);
                RM_PRINTF("TRX: Renderer start\r\n");
                Rendering_func(outTempBuff);
                RM_PRINTF("TRX: Renderer done\r\n");
            }
#endif
        }
        break;

        case RM_SWPLL_SYNC:
        {
        }
        break;

        default:
        {
        }
        break;
    }

    return (0);
}

uint8_t RM_Callback_StatusUpdate(uint8_t status)
{
    switch (status)
    {
        case LINK_DISCONNECTED:
        {
            if (app_env.init_done)
            {
                RM_PRINTF("__RM_LINK_DISCONNECTED\r\n");
#if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT)
                NVIC_DisableIRQ(AUDIOSINK_PHASE_IRQn);
                NVIC_DisableIRQ(AUDIOSINK_PERIOD_IRQn);
                NVIC_DisableIRQ(DMA_IRQn(ASRC_IN_IDX));
                NVIC_DisableIRQ(DSP1_IRQn);
                NVIC_DisableIRQ(TIMER_IRQn(TIMER_REGUL));
                Sys_Timers_Stop(1 << TIMER_REGUL);
#endif
                RM_Disable();
                Sys_Timers_Stop(SELECT_TIMER0);
                Sys_Timers_Stop(SELECT_TIMER1);
                NVIC_ClearPendingIRQ(TIMER0_IRQn);
                NVIC_ClearPendingIRQ(TIMER1_IRQn);
                RF_SwitchToBLEMode();
                low_power_clk_param.low_power_enable = true;
                app_env.audio_streaming = 0;
            }
            app_env.rm_lostLink_counter++;
        }
        break;

        case LINK_ESTABLISHMENT_UNSUCCESS:
        {
            app_env.rm_unsuccessLink_counter++;
        }
        break;

        case LINK_ESTABLISHED:
        {
            RM_PRINTF("__RM_LINK_ESTABLISHED\n");
#if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT)
            RM_PRINTF("AUDIO: enabling IRQs\n");
            asrc_stable     = false;
            cntr_stability  = 0;
            audio_sink_cnt  = 0;
            flag_ascc_phase = false;

            Sys_ASRC_Reset();

            NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);
            NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);
            NVIC_EnableIRQ(DMA_IRQn(ASRC_IN_IDX));
            /* DSP1_IRQn already enabled from init */
            NVIC_EnableIRQ(TIMER_IRQn(TIMER_REGUL));
            RM_PRINTF("AUDIO: IRQs enabled\n");
#endif
        }
        break;

        default:
        {
        }
        break;
    }

    app_env.rm_link_status = status;
    return (0);
}

#endif /* APP_RM_ENABLE */
#else
/* ----------------------------------------------------------------------------
 * Copyright (c) 2015-2017 Semiconductor Components Industries, LLC (d/b/a
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
 * rm_app.c
 * - Remote mic application
 * ----------------------------------------------------------------------------
 * $Revision: 1.12 $
 * $Date: 2019/12/27 18:50:38 $
 * ------------------------------------------------------------------------- */

#include "app.h"
#include <printf.h>

uint32_t data_rd = 0;

/* For Test */
uint8_t tmp;
//uint32_t ascc_cnt, audio_sink_phase_cnt, erraaa = 0;

uint8_t inTempBuffLeft[100]  = {
    0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf,
    0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf, 0xaf
};
uint8_t inTempBuffRight[100] = {
    0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc,
    0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc, 0xbc
};
uint8_t outTempBuff[100];
uint16_t app_sendCntrRight = 0, app_sendCntrLeft = 0, app_receiveCntr = 0;
uint32_t app_err1 = 0, app_err2 = 0, app_err3 = 0, app_err4 = 0, app_err5 = 0,
         app_err6 = 0, app_err7 = 0;

extern struct app_env_tag app_env;

const uint8_t coded_sample[4 * 60] = {
    0xb1, 0x5b, 0x5d, 0xdf, 0xef, 0x7b, 0xb7, 0xff, 0x3c, 0xff, 0xbf, 0x3b,
    0xff, 0xcb, 0x5c, 0xb7,
    0xbb, 0x5d, 0xfa, 0xc7, 0x7d, 0xf3, 0xef, 0x7e, 0xbb, 0xd7, 0xbd, 0xff,
    0xe9, 0xff, 0xfb, 0x7f,
    0xe6, 0xdf, 0x59, 0xd5, 0xd6, 0xfd, 0x35, 0xd3, 0xf9, 0x14, 0x9f, 0x64,
    0xf7, 0x9b, 0x55, 0x3f,
    0x99, 0x49, 0xbf, 0xd5, 0x6d, 0xbf, 0xd7, 0x6b, 0x75, 0xbd, 0x7f, 0xb7,

    0xfa, 0xe7, 0xee, 0x32, 0x7a, 0x1a, 0x4e, 0xd7, 0x5e, 0xf7, 0xef, 0x7f,
    0xdf, 0xef, 0xbe, 0x5b,
    0xed, 0xf6, 0xdb, 0xfd, 0xb5, 0xdf, 0x5d, 0xa7, 0xd7, 0x79, 0xb6, 0xfe,
    0x79, 0xfe, 0xbf, 0xef,
    0x7e, 0xfa, 0xbf, 0x7b, 0xfb, 0xcb, 0x5b, 0xfa, 0xd7, 0x3f, 0x76, 0xf7,
    0x77, 0xde, 0xf5, 0xb5,
    0xfd, 0x5d, 0x36, 0x93, 0x6d, 0x76, 0xdb, 0x4f, 0xe5, 0xff, 0x79, 0xfe,

    0x9f, 0xd3, 0x6d, 0x36, 0xda, 0x18, 0x57, 0x47, 0xdd, 0xde, 0xff, 0xd6,
    0xd1, 0xdf, 0xfe, 0xbb,
    0xef, 0xb6, 0xf1, 0xef, 0x2f, 0xdb, 0xe5, 0x7e, 0x5b, 0xfd, 0x65, 0xfd,
    0xff, 0xff, 0xb3, 0xff,
    0xad, 0xed, 0xcd, 0xed, 0xd9, 0xfd, 0xb7, 0x5b, 0x59, 0xb4, 0xfd, 0x6f,
    0xbd, 0xba, 0xdf, 0x3e,
    0xef, 0xcf, 0x7f, 0xfe, 0x7d, 0x77, 0x57, 0x65, 0x24, 0xff, 0x6b, 0xbe,

    0xf5, 0xdb, 0x0c, 0xb4, 0xac, 0x63, 0xa5, 0xdd, 0xf7, 0x5d, 0xef, 0xad,
    0xfb, 0xff, 0xde, 0xfd,
    0xdb, 0x7f, 0xff, 0xd7, 0xfd, 0xff, 0xdf, 0xee, 0xf9, 0xef, 0xfe, 0xfb,
    0xff, 0xef, 0xff, 0xe5,
    0xae, 0xd6, 0xfd, 0xf5, 0xff, 0x6d, 0x76, 0xdb, 0x6d, 0x76, 0xd6, 0x6d,
    0xb4, 0xff, 0x5d, 0x75,
    0xff, 0x59, 0x77, 0xdf, 0x4f, 0xb6, 0xd7, 0xe9, 0x6e, 0x9e, 0x7d, 0xff
};
uint32_t coded_cntr = 0;

void APP_RM_Init(uint8_t side)
{
    struct rm_callback callback;

    uint8_t temp[16] = RM_HOPLIST;

    app_env.rm_link_status              = LINK_DISCONNECTED;
    app_env.rm_lostLink_counter         = 0;
    app_env.rm_unsuccessLink_counter     = 0;
    app_env.audio_streaming             = 0;

    app_env.rm_param.audioChnl          = side;
    app_env.rm_param.role = RM_SLAVE_ROLE;
    app_env.rm_param.interval_time      = 10000;
    app_env.rm_param.retrans_time       = 5000;
    app_env.rm_param.audio_rate         = 48;
    app_env.rm_param.radio_rate         = 2000;
    app_env.rm_param.scan_time          = 6500;
    app_env.rm_param.preamble           = 0x55;
    app_env.rm_param.accessword         = (0x00cde629 | (0xf2 << 24));

    app_env.rm_param.payloadFlowRequest = APP_RM_DATA_REQUEST_TYPE;
    app_env.rm_param.renderDelay        = 200;

    if (app_env.rm_param.payloadFlowRequest == RM_APP_REQUEST)
    {
        app_env.rm_param.preFetchDelay = 1300;
    }
    else
    {
        app_env.rm_param.preFetchDelay = 400;
    }

    app_env.rm_param.pktLostLowThrshld     = 10;
    app_env.rm_param.pktLostHighThrshld    = 200;
    app_env.rm_param.pktLostLowThrshldSlow = 1;

    app_env.rm_param.searchTryCntThrshld   = 20;
    app_env.rm_param.waitCntGranularity    = 200;

    app_env.rm_param.stepSize = 1;
    app_env.rm_param.numChnlInHopList = 7;

    app_env.rm_param.mod_idx  = BLE_MOD_IDX;
    app_env.rm_param.dma_memcpy_num   = MEMCPY_DMA_NUM;

    app_env.rm_param.debug_dio_num[0] = DEBUG_DIO_FIRST;
    app_env.rm_param.debug_dio_num[1] = DEBUG_DIO_SECOND;
    app_env.rm_param.debug_dio_num[2] = 0xff;
    app_env.rm_param.debug_dio_num[3] = 0xff;

    memcpy(app_env.rm_param.hopList, temp, 16);

    callback.trx_event     = RM_Callback_TRX;
    callback.status_update = RM_Callback_StatusUpdate;

    RM_Configure(&app_env.rm_param, callback);

    rm_env.intf.status_update(LINK_DISCONNECTED);
}

uint8_t ptr_right[ENCODED_FRAME_LENGTH];
uint8_t cntr_enc_rm0 = 0, cntr_enc_rm1 = 0;
uint8_t RM_Callback_TRX(uint8_t type, uint8_t *length, uint8_t *ptr)
{
    switch (type)
    {
        case RM_TX_PAYLOAD_READY_LEFT:
        {
        }
        break;

        case RM_TX_PAYLOAD_READY_RIGHT:
        {
        }
        break;

        case RM_RX_TRANSFER_GOODPKT:
        case RM_RX_TRANSFER_BADCRCPKT:
        case RM_RX_TRANSFER_NOPKT:
        {
            if ((*length) == 0)
            {
                /* PLC should be applied as tx hasn't sent data
                 * for example: repeat previous packet, */
                memset(outTempBuff, 0xaa, ((app_env.rm_param.audio_rate *
                                            app_env.rm_param.interval_time) /
                                           8000));

                app_err1++;
            }
            else
            {
#if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT)
                memcpy(outTempBuff, ptr, *length);
                Rendering_func(outTempBuff);
#endif    /* if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT) */


                if (type == RM_RX_TRANSFER_GOODPKT)
                {
                    if ((app_receiveCntr + 1) != (((outTempBuff[1] << 8) |
                                                   outTempBuff[0])))
                    {
                        app_err2++;
                    }

                    app_receiveCntr = ((outTempBuff[1] << 8) | outTempBuff[0]);

                    if (app_env.rm_param.audioChnl == RM_FIRST_AUDIO_CHANNEL)
                    {
                        if (outTempBuff[20] != 0xaf)
                        {
                            app_err3++;
                        }
                    }
                    else
                    {
                        if (outTempBuff[20] != 0xbc)
                        {
                            app_err3++;
                        }
                    }
                }
                else
                {
                    app_err1++;
                }
            }
        }
        break;

        case RM_SWPLL_SYNC:
        {
        }
        break;

        default:
        {
        }
        break;
    }

    return (0);
}

uint8_t RM_Callback_StatusUpdate(uint8_t status)
{
    switch (status)
    {
        case LINK_DISCONNECTED:
        {
            PRINTF("__RM_LINK_DISCONNECTED\n");
           // LONGMSNUM =LONG_MS ;
          //  DEBOUNCEMSNUM=DEBOUNCE_MS;
           // Sys_GPIO_Set_High(ATC_DIO_NUM);
           // programsetflag = 1;
           // firstprogramsetflag = 0;
            /* Stop audio transmission to avoid having annoying noise
             * decide if the number of lost links is large, do an action */
#if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT && SIMUL != 1)
            NVIC_DisableIRQ(AUDIOSINK_PHASE_IRQn);
            NVIC_DisableIRQ(AUDIOSINK_PERIOD_IRQn);

            /* DMA interrupts */
            NVIC_DisableIRQ(DMA_IRQn(ASRC_IN_IDX));

            /* LPDSP32 interrupt */
            NVIC_DisableIRQ(DSP1_IRQn);

            /* Timer interrupts */
            NVIC_DisableIRQ(TIMER_IRQn(TIMER_REGUL));
            Sys_Timers_Stop(1 << TIMER_REGUL);
#endif    /* if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT && SIMUL != 1) */
            app_env.rm_lostLink_counter++;
        }
        break;

        case LINK_ESTABLISHMENT_UNSUCCESS:
        {
            app_env.rm_unsuccessLink_counter++;
        }
        break;

        case LINK_ESTABLISHED:
        {
            PRINTF("__RM_LINK_ESTABLISHED\n");
           // LONGMSNUM =RMLONG_MS ;
           // DEBOUNCEMSNUM=RMDEBOUNCE_MS;
           // programsetflag = 0;
           // Sys_GPIO_Set_Low(ATC_DIO_NUM);
            /* start audio transmission */
#if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT && SIMUL != 1)
            asrc_stable     = false;
            cntr_stability  = 0;
            audio_sink_cnt  = 0;
            flag_ascc_phase = false;

            Sys_ASRC_Reset();

            /* ASCC interrupts */
            NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);
            NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

            /* DMA interrupts */
            NVIC_EnableIRQ(DMA_IRQn(ASRC_IN_IDX));

            /* LPDSP32 interrupt */
            NVIC_EnableIRQ(DSP1_IRQn);

            /* Timer interrupts */
            NVIC_EnableIRQ(TIMER_IRQn(TIMER_REGUL));
#endif    /* if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT && SIMUL != 1) */
        }
        break;

        default:
        {
        }
        break;
    }

    app_env.rm_link_status = status;
    return (0);
}
#endif
