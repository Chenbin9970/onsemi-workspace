/* ----------------------------------------------------------------------------
 * Copyright (c) 2015-2017 Semiconductor Components Industries, LLC (d/b/a
 * ON Semiconductor), All Rights Reserved
 *
 * Copyright (C) RivieraWaves 2009-2016
 *
 * This module is derived in part from example code provided by RivieraWaves
 * and as such the underlying code is the property of RivieraWaves [a member
 * of the CEVA, Inc. group of companies], together with additional code which
 * is the property of ON Semiconductor. The code (in whole or any part) may not
 * be redistributed in any form without prior written permission from
 * ON Semiconductor.
 *
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 *
 * This is Reusable Code.
 *
 * ----------------------------------------------------------------------------
 * app_process.c
 * - Application task handler definition and support processes
 * ----------------------------------------------------------------------------
 * $Revision: 1.8 $
 * $Date: 2018/03/16 14:22:51 $
 * ------------------------------------------------------------------------- */

#include "app.h"
#include <printf.h>

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

const struct ke_task_desc TASK_DESC_APP = {
    NULL,       &appm_default_handler,
    appm_state, APPM_STATE_MAX,
    APP_IDX_MAX
};

/* State and event handler definition */
const struct ke_msg_handler appm_default_state[] =
{
    /* Note: Put the default handler on top as this is used for handling any
     *       messages without a defined handler */
    { KE_MSG_DEFAULT_HANDLER, (ke_msg_func_t)Msg_Handler },
    BLE_MESSAGE_HANDLER_LIST,
    BASC_MESSAGE_HANDLER_LIST,
    CS_MESSAGE_HANDLER_LIST,
    APP_MESSAGE_HANDLER_LIST
};

/* Use the state and event handler definition for all states. */
const struct ke_state_handler appm_default_handler
    = KE_STATE_HANDLER(appm_default_state);

/* Defines a place holder for all task instance's state */
ke_state_t appm_state[APP_IDX_MAX];

/* ----------------------------------------------------------------------------
 * Function      : unsigned int APP_Timer(ke_msg_idd_t const msg_id,
 *                                 void const *param,
 *                                ke_task_id_t const dest_id,
 *                                ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle timer event message
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameter (unused)
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int APP_Timer(ke_msg_id_t const msg_id,
              void const *param,
              ke_task_id_t const dest_id,
              ke_task_id_t const src_id)
{
    uint8_t onoff_val = 1;
    static uint8_t delay_cnt[PEER_COUNT];
    static uint8_t configured_count;
    static uint8_t rm_delay;
    static bool  peer1_connect_started;

    /* Audio detection state machine */
    static bool  ad_detected;
    static uint8_t ad_cnt;
    static uint8_t ad_dbg;

    /* Restart timer */
    ke_timer_set(APP_TEST_TIMER, TASK_APP, TIMER_200MS_SETTING);

    /* LED: on if any peer connected */
    {
        bool any_connected = false;
        uint8_t p;
        for (p = 0; p < PEER_COUNT; p++)
            if (peer_ble_connected[p])
                any_connected = true;
        if (any_connected)
            Sys_GPIO_Set_High(LED_DIO_NUM);
        else if (ble_env.state == APPM_CONNECTING)
            Sys_GPIO_Toggle(LED_DIO_NUM);
        else
            Sys_GPIO_Set_Low(LED_DIO_NUM);
    }

    /* Step 0: After peer 0 discovery, start connecting peer 1 (keep both) */
    if (cs_env[0].state >= CS_ALL_ATTS_DISCOVERED
        && !peer_ble_connected[1] && !peer1_connect_started)
    {
        peer1_connect_started = true;
        DirectConnect(1);
    }

    /* Audio detection: EMA-filtered energy from DMIC ISR.
     * Keep running during RM streaming so we can detect silence later. */
    if (peer_ble_connected[0] || peer_ble_connected[1]
        || app_env.audio_streaming)
    {
        static uint32_t ema_e;

        uint32_t raw = (audio_energy_left > audio_energy_right)
                       ? audio_energy_left : audio_energy_right;

        if (ema_e == 0)
            ema_e = raw;
        else if (raw > ema_e)
            ema_e += (raw - ema_e) >> 4;
        else
            ema_e -= (ema_e - raw) >> 4;

        if (ema_e > AUDIO_ENERGY_THRESHOLD)
        {
            if (ad_cnt < AUDIO_DETECT_CONSEC_CNT)
                ad_cnt++;
            if (ad_cnt >= AUDIO_DETECT_CONSEC_CNT && !ad_detected)
            {
                ad_detected = true;
                PRINTF("__AUD DETECTED raw=%lu ema=%lu\n", raw, ema_e);
            }
        }
        else
        {
            ad_cnt = 0;
        }

        if (++ad_dbg >= 10)
        {
            ad_dbg = 0;
            PRINTF("__AUD raw=%lu ema=%lu cnt=%d det=%d\n",
                   raw, ema_e, ad_cnt, ad_detected);
        }
    }
    else
    {
        ad_cnt = 0;
        ad_detected = false;
    }

    /* Step 1: Write RM_ONOFF to each peer after discovery + audio detected */
    {
        uint8_t i;
        for (i = 0; i < PEER_COUNT; i++)
        {
            if (cs_env[i].state == CS_ALL_ATTS_DISCOVERED
                && peer_ble_connected[i] && ad_detected)
            {
                if (++delay_cnt[i] >= 5)
                {
                    delay_cnt[i] = 0;
                    cs_env[i].state = CS_CONFIGURING;
                    cs_env[i].config_num = 0;
                    PRINTF("__RM_ONOFF write p=%d conidx=%d\n",
                           i, peer_conidx[i]);
                    CustomSrvice_SendWrite(peer_conidx[i], &onoff_val,
                                           cs_env[i].disc_att[CS_REMPRO_IDX_RM_ONOFF].pointer_hdl,
                                           0, 1, GATTC_WRITE);
                }
            }
        }
    }

    /* Step 2: Count configured peers */
    {
        uint8_t i;
        configured_count = 0;
        for (i = 0; i < PEER_COUNT; i++)
        {
            if (cs_env[i].state == CS_PEER_CONFIGURED)
                configured_count++;
        }
    }

    /* Step 3: Start RM when both configured + audio detected */
    if (configured_count >= PEER_COUNT && !app_env.audio_streaming && ad_detected)
    {
        if (++rm_delay >= 5)
        {
            rm_delay = 0;
            PRINTF("__RM START streaming\n");
            APP_RM_Init(ear_side);
            RF_SwitchToCPMode();
            NVIC_DisableIRQ(BLE_FINETGTIM_IRQn);
            RM_Enable(1000);
            app_env.audio_streaming = 1;
        }
    }

    /* Reconnect disconnected peers */
    if (!app_env.audio_streaming)
    {
        uint8_t i;
        for (i = 0; i < PEER_COUNT; i++)
        {
            if (!peer_ble_connected[i]
                && cs_env[i].state < CS_PEER_CONFIGURED
                && ble_env.state != APPM_CONNECTING)
            {
                static uint8_t reconnect_cnt = 0;
                if (++reconnect_cnt >= 5)
                {
                    reconnect_cnt = 0;
                    DirectConnect(i);
                }
                break;
            }
        }
    }

    /* Battery: only on peer 0 before any writes */
    if (configured_count == 0 && cs_env[0].state < CS_CONFIGURING)
        app_env.send_batt_req++;

    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int Msg_Handler(ke_msg_id_t const msg_id,
 *                                 void const *param,
 *                                 ke_task_id_t const dest_id,
 *                                 ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle any message received from kernel that doesn't have
 *                 a dedicated handler
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameter (unused)
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int Msg_Handler(ke_msg_id_t const msg_id, void *param,
                ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    return (KE_MSG_CONSUMED);
}
