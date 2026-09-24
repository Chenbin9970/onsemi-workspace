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
    uint8_t length;
    uint8_t * *value;

    /* Restart timer */
    ke_timer_set(APP_TEST_TIMER, TASK_APP, TIMER_200MS_SETTING);

#if (TX_DBG_PRINT)
    /* Bring-up report: RM payload requests and link status, then the pipeline
     * liveness counters. pcm should climb ~2000/s (two interrupts per ms, one
     * per half block) and enc ~1000/s; asrc must stay 0 because this input path
     * does not use the ASRC. rst counts TX FIFO read pointer repositions — each
     * one is an audible discontinuity.
     * cfg reports the four PCM pads' mode/pull bits (mode is bits 5:0, pull is
     * bits 9:8): 0x13E = input + weak pull-up, 0x03E = input, 0x002/0x003 =
     * output low/high. dat is the raw level of all 16 pads. */
    PRINTF("[TX] req L=%u R=%u len=%u st=%u/%u | pcm=%u pk=%d enc=%u asrc=%u"
           " rst=%u | cfg=%03X/%03X/%03X/%03X dat=%04X\r\n",
           (unsigned)dbg_cnt_tx_req[0], (unsigned)dbg_cnt_tx_req[1],
           (unsigned)dbg_len_last, (unsigned)dbg_cnt_status,
           (unsigned)dbg_last_status, (unsigned)dbg_cnt_pcm_isr,
           (int)dbg_pcm_peak, (unsigned)dbg_cnt_enc,
           (unsigned)dbg_cnt_asrc_out, (unsigned)ptr_rst_cnt,
           (unsigned)(DIO->CFG[PCM_FRAME_SYNC] & 0x3FF),
           (unsigned)(DIO->CFG[PCM_SER_DI] & 0x3FF),
           (unsigned)(DIO->CFG[PCM_CLK_DO] & 0x3FF),
           (unsigned)(DIO->CFG[PCM_SER_DO] & 0x3FF),
           (unsigned)(DIO->DATA & 0xFFFF));
#endif    /* if (TX_DBG_PRINT) */

#if (!TX_DBG_PRINT)
    /* Stay silent while everything is healthy — printing costs milliseconds of
     * CPU and corrupts the PCM samples. Arm a baseline a few seconds in (the
     * FIFO legitimately repositions once while it first fills), then speak up
     * only when either counter moves past that baseline. */
    {
        static uint8_t  settle;
        static uint8_t  armed;
        static uint8_t  rst_base;
        static uint32_t qfail_base;

        if (settle < 25)
        {
            settle++;
        }
        else if (!armed)
        {
            armed      = 1;
            rst_base   = ptr_rst_cnt;
            qfail_base = dbg_q_alloc_fail;
        }
        else if ((ptr_rst_cnt != rst_base) ||
                 (dbg_q_alloc_fail != qfail_base))
        {
            PRINTF("[TX] ALARM rst=%u(+%u) qfail=%u(+%u) | pcm=%u pk=%d enc=%u"
                   "\r\n",
                   (unsigned)ptr_rst_cnt, (unsigned)(ptr_rst_cnt - rst_base),
                   (unsigned)dbg_q_alloc_fail,
                   (unsigned)(dbg_q_alloc_fail - qfail_base),
                   (unsigned)dbg_cnt_pcm_isr, (int)dbg_pcm_peak,
                   (unsigned)dbg_cnt_enc);
        }
    }
#endif    /* if (!TX_DBG_PRINT) */

    /* Turn on LED of EVB if the link is established */
    if (ble_env.state >= APPM_CONNECTED)
    {
        Sys_GPIO_Set_High(LED_DIO_NUM);
    }
    else if (ble_env.state == APPM_CONNECTING)
    {
        Sys_GPIO_Toggle(LED_DIO_NUM);
    }
    else
    {
        Sys_GPIO_Set_Low(LED_DIO_NUM);
    }

    if ((ble_env.state == APPM_CONNECTED) && (cs_env.state ==
                                              CS_ALL_ATTS_DISCOVERED))
    {
        cs_env.state = CS_CONFIGURING;

        cs_env.config_num = 0;

        /* The first parameter */
        length = CustomProtocol_SelectAttributeValue(cs_env.config_num,
                                                     (uint8_t * *)&value);

        CustomSrvice_SendWrite(ble_env.conidx, (uint8_t *)value,
                               cs_env.disc_att[cs_env.config_num].pointer_hdl,
                               0,
                               length,
                               GATTC_WRITE);
    }

    if ((ble_env.state == APPM_CONNECTED) && (cs_env.state ==
                                              CS_PEER_CONFIGURED))
    {
        app_env.RM_on_off = 1;
        CustomSrvice_SendWrite(ble_env.conidx, &app_env.RM_on_off,
                               cs_env.disc_att[CS_REMPRO_IDX_ONOFF].pointer_hdl,
                               0,
                               1, GATTC_WRITE);
    }

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
