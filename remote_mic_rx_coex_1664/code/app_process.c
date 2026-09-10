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
 * $Date: 2018/02/27 15:42:17 $
 * ------------------------------------------------------------------------- */

#include "app.h"
#include "dsp_7100_init.h"
#include "dsp_7100_cmd.h"
#include "i2c_7100_hal.h"

/* ----------------------------------------------------------------------------
 * Function      : int APP_7100_HB_Handler(ke_msg_id_t const msg_id,
 *                                         void const *param,
 *                                         ke_task_id_t const dest_id,
 *                                         ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : 200ms 周期 tick。优先推进降噪/DFBC 写会话（一条命令一 tick），
 *                 否则推进读回会话；每 5s(25 tick) 向 7100 发心跳 {0x88, 0x01}。
 *                 写会话期间不读回/不发心跳，避免抢 I2C。
 * ------------------------------------------------------------------------- */
int APP_7100_HB_Handler(ke_msg_id_t const msg_id, void const *param,
                        ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    static uint16_t s_7100_cnt = 0;

    (void)msg_id;
    (void)param;
    (void)dest_id;
    (void)src_id;

    /* Re-arm: 200ms periodic tick */
    ke_timer_set(APP_7100_HB_TIMER, TASK_APP, TIMER_200MS_SETTING);

    s_7100_cnt++;

    /* 降噪/DFBC 写会话进行中：只推进它，不读回不发心跳 */
    if (dsp_7100_cmd_busy()) {
        dsp_7100_cmd_tick();
        return (KE_MSG_CONSUMED);
    }

    /* 每 tick(200ms)：推进 4 程序×(降噪/DFBC/WDRC) 读回（一轮完成即停止） */
    dsp_7100_rb_seq_tick();

    /* 每 25 tick(5s)：发心跳 {0x88,0x01} */
    if ((s_7100_cnt % 25) == 0) {
        uint8_t hb[2] = {0x88, 0x01};
        bool ok = i2c_7100_write(I2C_7100_ADDR, hb, sizeof(hb));
        (void)ok;
    }

    return (KE_MSG_CONSUMED);
}

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
    BASS_MESSAGE_HANDLER_LIST,
    CS_MESSAGE_HANDLER_LIST,
    APP_MESSAGE_HANDLER_LIST
};

/* Use the state and event handler definition for all states. */
const struct ke_state_handler appm_default_handler
    = KE_STATE_HANDLER(appm_default_state);

/* Defines a place holder for all task instance's state */
ke_state_t appm_state[APP_IDX_MAX];

/* ----------------------------------------------------------------------------
 * Function      : int APP_Timer(ke_msg_idd_t const msg_id,
 *                               void const *param,
 *                               ke_task_id_t const dest_id,
 *                               ke_task_id_t const src_id)
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
    /* 重启 200ms 定时器（供内核周期性唤醒） */
    ke_timer_set(APP_TEST_TIMER, TASK_APP, TIMER_200MS_SETTING);

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
