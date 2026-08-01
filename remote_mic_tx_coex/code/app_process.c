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
    /* TX state machine */
    enum { TX_BLE_IDLE, TX_CONNECTING, TX_RM_ACTIVE } static tx_state;

    /* Audio detection state */
    static bool     ad_detected;
    static uint8_t  ad_cnt;
    static uint8_t  ad_lost_cnt;
    static bool     ad_lost;
    static uint8_t  ad_dbg;

    /* CONNECT_IND phase state */
    static uint8_t  phase;
    static uint8_t  tick;
    static uint8_t  cancelling;

    /* Restart timer */
    ke_timer_set(APP_TEST_TIMER, TASK_APP, TIMER_200MS_SETTING);

    /* LED: on=streaming, toggle=connecting, off=idle */
    {
        if (tx_state == TX_RM_ACTIVE)
            Sys_GPIO_Set_High(LED_DIO_NUM);
        else if (tx_state == TX_CONNECTING)
            Sys_GPIO_Toggle(LED_DIO_NUM);
        else
            Sys_GPIO_Set_Low(LED_DIO_NUM);
    }

    /* Audio detection: EMA-filtered energy from DMIC ISR.
     * Always run — needed in BLE_IDLE (detect start) and RM_ACTIVE (detect stop). */
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
            ad_lost_cnt = 0;
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
            if (ad_detected)
            {
                if (ad_lost_cnt < AUDIO_LOST_CONSEC_CNT)
                    ad_lost_cnt++;
                if (ad_lost_cnt >= AUDIO_LOST_CONSEC_CNT && !ad_lost)
                {
                    ad_lost = true;
                    PRINTF("__AUD LOST raw=%lu ema=%lu\n", raw, ema_e);
                }
            }
        }

        if (++ad_dbg >= 10)
        {
            ad_dbg = 0;
            PRINTF("__AUD raw=%lu ema=%lu cnt=%d det=%d lost=%d\n",
                   raw, ema_e, ad_cnt, ad_detected, ad_lost);
        }
    }

    /* TX state machine */
    switch (tx_state)
    {
    case TX_BLE_IDLE:
        if (ad_detected)
        {
            PRINTF("__TX BLE_IDLE -> CONNECTING\n");
            phase = 0;
            tick = 0;
            cancelling = 0;
            tx_state = TX_CONNECTING;
        }
        break;

    case TX_CONNECTING:
    {
        if (phase < PEER_COUNT)
        {
            if (cancelling)
            {
                if (ble_env.state != APPM_CONNECTING)
                    phase++, cancelling = 0, tick = 0;
            }
            else if (tick == 0)
            {
                DirectConnect(phase);
                tick = 1;
            }
            else if (++tick >= 3)
            {
                struct gapm_cancel_cmd *c;
                c = KE_MSG_ALLOC(GAPM_CANCEL_CMD, TASK_GAPM,
                                 TASK_APP, gapm_cancel_cmd);
                c->operation = GAPM_CANCEL;
                ke_msg_send(c);
                cancelling = 1;
                tick = 0;
            }
        }
        else if (ble_env.state != APPM_CONNECTING)
        {
            if (ad_detected && !ad_lost)
            {
                PRINTF("__RM START\n");
                APP_RM_Init(ear_side);
                RF_SwitchToCPMode();
                NVIC_DisableIRQ(BLE_FINETGTIM_IRQn);
#if OUTPUT_POWER_6DBM
                Sys_RFFE_SetTXPower(6);
#endif
                RM_Enable(1000);
                app_env.audio_streaming = 1;
                tx_state = TX_RM_ACTIVE;
            }
            else
            {
                PRINTF("__TX CONNECTING -> BLE_IDLE (audio gone)\n");
                ad_detected = false;
                ad_lost = false;
                ad_lost_cnt = 0;
                tx_state = TX_BLE_IDLE;
            }
        }
        break;
    }

    case TX_RM_ACTIVE:
        if (ad_lost)
        {
            PRINTF("__TX RM_ACTIVE -> BLE_IDLE (audio lost)\n");
            RM_Disable();
            RF_SwitchToBLEMode();
            NVIC_EnableIRQ(BLE_FINETGTIM_IRQn);
            app_env.audio_streaming = 0;
            ad_detected = false;
            ad_lost = false;
            ad_lost_cnt = 0;
            tx_state = TX_BLE_IDLE;
        }
        break;
    }

    /* Battery: only when idle */
    if (tx_state == TX_BLE_IDLE)
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
