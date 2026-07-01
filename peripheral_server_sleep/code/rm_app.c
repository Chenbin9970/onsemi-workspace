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
 * rm_app.c
 * - Remote microphone BLE application
 * ----------------------------------------------------------------------------
 * $Revision: 1.9 $
 * $Date: 2019/12/30 20:50:47 $
 * ------------------------------------------------------------------------- */

#include "app.h"
#include <printf.h>

#ifdef APP_RM_ENABLE

uint8_t RetoneMODE[4];

uint8_t ear_side = APP_RM_AUDIO_CHANNEL;

void APP_RM_Init(uint8_t side)
{
    struct rm_callback callback;
    uint8_t temp[16] = RM_HOPLIST;

    app_env.rm_link_status           = LINK_DISCONNECTED;
    app_env.rm_lostLink_counter      = 0;
    app_env.rm_unsuccessLink_counter = 0;
    app_env.audio_streaming          = 0;

    app_env.rm_param.audioChnl      = side;
    app_env.rm_param.role           = APP_RM_ROLE;
    app_env.rm_param.interval_time  = 10000;
    app_env.rm_param.retrans_time   = 5000;

    app_env.rm_param.audio_rate     = 48;

    app_env.rm_param.radio_rate     = 2000;
    app_env.rm_param.scan_time      = 6500;
    app_env.rm_param.preamble       = 0x55;
    app_env.rm_param.accessword     = (0x00cde629 | (0xf2 << 24));

    app_env.rm_param.payloadFlowRequest = APP_RM_DATA_REQUEST_TYPE;
    app_env.rm_param.renderDelay    = 200;

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

    app_env.rm_param.searchTryCntThrshld = 10;
    app_env.rm_param.waitCntGranularity  = 200;

    app_env.rm_param.stepSize         = 1;
    app_env.rm_param.numChnlInHopList = 7;

    app_env.rm_param.mod_idx        = BLE_MOD_IDX;
    app_env.rm_param.dma_memcpy_num = MEMCPY_DMA_NUM;

    app_env.rm_param.debug_dio_num[0] = DEBUG_DIO_FIRST;
    app_env.rm_param.debug_dio_num[1] = DEBUG_DIO_SECOND;
    app_env.rm_param.debug_dio_num[2] = DEBUG_DIO_THIRD;
    app_env.rm_param.debug_dio_num[3] = 0xff;

    memcpy(app_env.rm_param.hopList, temp, 16);

    callback.trx_event     = RM_Callback_TRX;
    callback.status_update = RM_Callback_StatusUpdate;

    RM_Configure(&app_env.rm_param, callback);

    rm_env.intf.status_update(LINK_DISCONNECTED);
}

uint8_t RM_Callback_TRX(uint8_t type, uint8_t *length, uint8_t *ptr)
{
    switch (type)
    {
        case RM_RX_TRANSFER_GOODPKT:
        case RM_RX_TRANSFER_BADCRCPKT:
        case RM_RX_TRANSFER_NOPKT:
        {
            /* App_Process_Incoming_Data(ptr, *length); */
        }
        break;

        default:
        {
        }
        break;
    }

    return 0;
}

uint8_t RM_Callback_StatusUpdate(uint8_t status)
{
    switch (status)
    {
        case LINK_DISCONNECTED:
        {
            PRINTF("__RM_LINK_DISCONNECTED\n");
            /* App_Process_Link_Disconnected(); */
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
            /* App_Process_Connected(); */
        }
        break;

        default:
        {
        }
        break;
    }

    app_env.rm_link_status = status;
    return 0;
}

#endif /* APP_RM_ENABLE */
