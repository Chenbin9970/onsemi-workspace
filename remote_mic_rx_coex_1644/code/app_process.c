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
#include "bs300_ram_sync.h"
#include "ble_rempro_cmd.h"
#include <printf.h>

/* BS300 内核同步定时消息处理（见 bs300_ram_sync.h） */
int BS300_SyncTimer(ke_msg_id_t const msg_id, void const *param,
                    ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    (void)msg_id;
    (void)param;
    (void)dest_id;
    (void)src_id;
    bs300_sync_timer_handler();
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
 * Function      : void low_batt_check(uint8_t batt_lvl)
 * ----------------------------------------------------------------------------
 * Description   : 低电量告警。电量跌破 LOW_BATT_PCT 立即播一次提示音，之后只要
 *                 仍低于阈值每 LOW_BATT_CHECK_MS 重复一次；回到阈值以上复位，
 *                 下次跌破重新立即播。
 * Inputs        : - batt_lvl  - 当前电量百分比（app_env.batt_lvl）
 * Outputs       : None
 * Assumptions   : 每次电池采样后调用一次（即每 60s 一次）
 * ------------------------------------------------------------------------- */
static void low_batt_check(uint8_t batt_lvl)
{
    static uint8_t  seen       = 0;
    static uint32_t elapsed_ms = 0;

    if (batt_lvl >= LOW_BATT_PCT)
    {
        seen       = 0;
        elapsed_ms = 0;
        return;
    }

    if (seen && elapsed_ms < LOW_BATT_CHECK_MS)
    {
        elapsed_ms += BAT_SAMPLE_TICKS * 200;    /* 两次调用间隔 = 采样间隔 60s */
        return;
    }

    elapsed_ms = 0;
    seen       = 1;
#ifdef BS300_ENABLE
    bs300_play_low_batt_tone();
#endif    /* ifdef BS300_ENABLE */
}

/* ----------------------------------------------------------------------------
 * Function      : void battery_report(uint32_t raw)
 * ----------------------------------------------------------------------------
 * Description   : 把 ADC 原始值换算成百分比，更新 app_env.batt_lvl、打印 raw，
 *                 随后跑一次低电量告警判定。
 * Inputs        : - raw  - ADC 原始值（battery_adc_read_stop() 的返回）
 * Outputs       : None
 * Assumptions   : 由 battery_sample_tick 在两阶段采样的第二阶段调用
 * ------------------------------------------------------------------------- */
static void battery_report(uint32_t raw)
{
    uint32_t pct;

    if (raw <= BAT_ADC_MIN)
    {
        pct = 0;
    }
    else if (raw >= BAT_ADC_MAX)
    {
        pct = BAT_LVL_MAX;
    }
    else
    {
        pct = (raw - BAT_ADC_MIN) * BAT_LVL_MAX /
              (BAT_ADC_MAX - BAT_ADC_MIN);
    }

    app_env.batt_lvl = (uint8_t)pct;
    PRINTF("__BATT %u%% raw=%u\r\n", app_env.batt_lvl, raw);
    low_batt_check(app_env.batt_lvl);
}

/* ----------------------------------------------------------------------------
 * Function      : void battery_sample_tick(void)
 * ----------------------------------------------------------------------------
 * Description   : 电池周期两阶段采样。第一阶段（到点）只使能 ADC；第二阶段
 *                 （下一个 200ms tick）读值并关掉 ADC，再换算/打印/告警。
 *                 分两阶段的原因见 battery_adc_start() 的注释：配置完立刻读
 *                 会读到非法值（一直报 100%），而 ADC 只开不关会持续耗电。
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : 由 APP_Timer 每 200ms 调用一次
 * ------------------------------------------------------------------------- */
static void battery_sample_tick(void)
{
    static uint32_t tick_no  = 0;
    static uint32_t next_arm = BAT_ARM_FIRST_TICKS;
    static uint8_t  armed    = 0;

    if (armed)
    {
        /* 第二阶段：ADC 已使能 ≥1 个 tick（≥200ms ≫ 8 通道 5.1ms），读+关 */
        armed = 0;
        battery_report(battery_adc_read_stop());
        return;
    }

    tick_no++;
    if (tick_no != next_arm)
    {
        return;
    }

    next_arm = tick_no + BAT_SAMPLE_TICKS;
    battery_adc_start();
    armed = 1;
}

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

    /* 电池 DIO3(IO) 周期采样（每 60s 一次，见 battery_sample_tick） */
    battery_sample_tick();

#ifdef BS300_ENABLE
    /* RM 断开切回助听模式的过渡音量恢复倒计时（2s） */
    rm_trans_volume_tick();
#endif    /* ifdef BS300_ENABLE */

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
