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

    /* Restart timer */
    ke_timer_set(APP_TEST_TIMER, TASK_APP, TIMER_200MS_SETTING);

    /* LED */
    if (ble_env.state >= APPM_CONNECTED)
        Sys_GPIO_Set_High(LED_DIO_NUM);
    else if (ble_env.state == APPM_CONNECTING)
        Sys_GPIO_Toggle(LED_DIO_NUM);
    else
        Sys_GPIO_Set_Low(LED_DIO_NUM);

    /* Step 1: For each peer, write RM_ONOFF 1s after discovery */
    if (ble_env.state == APPM_CONNECTED)
    {
        uint8_t i;
        for (i = 0; i < PEER_COUNT; i++)
        {
            if (cs_env[i].state == CS_ALL_ATTS_DISCOVERED)
            {
                if (++delay_cnt[i] >= 5)    /* 1s */
                {
                    delay_cnt[i] = 0;
                    cs_env[i].state = CS_CONFIGURING;
                    cs_env[i].config_num = 0;
                    CustomSrvice_SendWrite(ble_env.conidx, &onoff_val,
                                           cs_env[i].disc_att[CS_REMPRO_IDX_RM_ONOFF].pointer_hdl,
                                           0, 1, GATTC_WRITE);
                }
            }
        }
    }

    /* Step 2: Count configured peers, start next connection or RM switch */
    {
        uint8_t i;
        configured_count = 0;
        for (i = 0; i < PEER_COUNT; i++)
        {
            if (cs_env[i].state == CS_PEER_CONFIGURED)
                configured_count++;
        }
    }

    if (configured_count >= PEER_COUNT && !app_env.audio_streaming)
    {
        /* All peers configured — switch to RM after 1s */
        if (++rm_delay >= 5)
        {
            rm_delay = 0;
            APP_RM_Init(ear_side);
            RF_SwitchToCPMode();
            NVIC_DisableIRQ(BLE_FINETGTIM_IRQn);
            RM_Enable(1000);
            app_env.audio_streaming = 1;
        }
    }
    else if (configured_count == 1 && current_peer == 0
             && ble_env.state == APPM_CONNECTED)
    {
        /* Peer 0 configured — disconnect and start peer 1 */
        struct gapc_disconnect_cmd *disc_cmd;
        disc_cmd = KE_MSG_ALLOC(GAPC_DISCONNECT_CMD,
                                KE_BUILD_ID(TASK_GAPC, ble_env.conidx),
                                TASK_APP, gapc_disconnect_cmd);
        disc_cmd->operation = GAPC_DISCONNECT;
        disc_cmd->reason = CO_ERROR_REMOTE_USER_TERM_CON;
        ke_msg_send(disc_cmd);
        current_peer = 1;
    }

    /* Reconnect: after disconnect, connect next peer after 1s */
    if (ble_env.state == APPM_READY && !app_env.audio_streaming
        && configured_count < PEER_COUNT)
    {
        static uint8_t reconnect_cnt = 0;
        if (++reconnect_cnt >= 5)
        {
            reconnect_cnt = 0;
            DirectConnect(current_peer);
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
