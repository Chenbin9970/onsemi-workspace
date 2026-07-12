/* ----------------------------------------------------------------------------
 * Copyright (c) 2016 Semiconductor Components Industries, LLC (d/b/a
 * ON Semiconductor), All Rights Reserved
 *
 * This code is the property of ON Semiconductor and may not be redistributed
 * in any form without prior written permission from ON Semiconductor.
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 * ----------------------------------------------------------------------------
 * app.c
 * - Main application file
 * ----------------------------------------------------------------------------
 * $Revision: 1.74 $
 * $Date: 2019/09/04 13:40:50 $
 * ------------------------------------------------------------------------- */

#include "app.h"
#include "bs300_ram_sync.h"
#include "bs300_storage.h"
#include "ble_rempro_cmd.h"
#include "button.h"

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

/* Called when async BS300 program switch completes — re-activate DSP + notify */
static void on_bs300_switch_done(void)
{
    cs_env.tx_value_changed = 1;
    bs300_async_done_callback();
}

static void on_bs300_volume_done(void)
{
    cs_env.tx_value_changed = 1;
    bs300_async_done_callback();
}

static void on_btn_volume_done(void)
{
    cs_env.tx_value_changed = 1;
    button_done_callback();
}

static void on_btn_switch_done(void)
{
    cs_env.tx_value_changed = 1;
    button_done_callback();
}

int main()
{
    App_Initialize();

#ifdef BS300_TEST_ENABLE
    bs300_test_run();
#endif

    /* Wait for 3 seconds to allow re-flashing directly after pressing RESET */
    Sys_Delay_ProgramROM(3 * SystemCoreClock);

    /* Turn LED on */
    Sys_DIO_Config(LED_DIO, DIO_MODE_GPIO_OUT_1);

#ifndef DEBUG_UART_ENABLE
    /* Disable DIO4 and DIO5 to avoid current consumption on VDDO */
    Sys_DIO_Config(4, DIO_MODE_DISABLE | DIO_NO_PULL);
    Sys_DIO_Config(5, DIO_MODE_DISABLE | DIO_NO_PULL);
#endif

    /* Main application loop */
    Main_Loop();
}

/* ----------------------------------------------------------------------------
 * Function      : Main_Loop(void)
 * ----------------------------------------------------------------------------
 * Description   : - Run the kernel scheduler
 *                 - Update the battery voltage when applicable
 *                 - Update custom service data when applicable
 *                 - Attempt to go to sleep mode if possible
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Main_Loop(void)
{
    Sys_Watchdog_Refresh();

    if ((cs_env.sentSuccess == 1) &&
        (app_env.sleep_cycles % APP_CS_TX_VALUE_NOTF_SLEEP_CYCLE == 0))
    {
        cs_env.sentSuccess = 0;
        cs_env.tx_value_changed = 1;
    }
    (app_env.sleep_cycles)++;

    /* Read the battery level and update the average value */
    Measure_Battery_Level();

    while (true)
    {
        button_wakeup_check();

        Kernel_Schedule();

#ifdef APP_RM_ENABLE
        RM_StatusHandler();

        if (app_env.rm_start_requested)
        {
            app_env.rm_start_requested = 0;
            APP_RM_Init(ear_side);
            Audio_Init();
            RF_SwitchToCPMode();
            RM_Enable(1000);
            app_env.audio_streaming = 1;
        }

        if (app_env.rm_stop_requested)
        {
            app_env.rm_stop_requested = 0;
            /* Stop audio pipeline before RF switch */
            NVIC_DisableIRQ(AUDIOSINK_PHASE_IRQn);
            NVIC_DisableIRQ(AUDIOSINK_PERIOD_IRQn);
            NVIC_DisableIRQ(DMA_IRQn(ASRC_IN_IDX));
            NVIC_DisableIRQ(DSP1_IRQn);
            NVIC_DisableIRQ(TIMER_IRQn(TIMER_REGUL));
            Sys_Timers_Stop(1 << TIMER_REGUL);
            Sys_DMA_ChannelDisable(ASRC_OUT_IDX);
            Sys_DMA_ChannelDisable(OD_DMA_NUM);
            SYSCTRL->DSS_CTRL = DSS_LPDSP32_PAUSE;
            BBIF->CTRL = BB_CLK_ENABLE | BBCLK_DIVIDER_8 | BB_DEEP_SLEEP;
            RM_Disable();
            Sys_Timers_Stop(SELECT_TIMER0);
            Sys_Timers_Stop(SELECT_TIMER1);
            NVIC_ClearPendingIRQ(TIMER0_IRQn);
            NVIC_ClearPendingIRQ(TIMER1_IRQn);
            RF_SwitchToBLEMode();
            app_env.audio_streaming = 0;
        }
#endif

        Sys_Watchdog_Refresh();

#ifdef DEBUG_UART_ENABLE
        {
            static uint32_t tick_cnt = 0;
            if (++tick_cnt >= 10000)
            {
                tick_cnt = 0;
                PRINTF("tick\r\n");
            }
        }
#endif

        /* ---- button actions (works in all BLE states) ---- */
        if (!bs300_sync_is_busy()) {
            btn_action_t btn = button_get_action();
            if (btn == BTN_ACTION_SWITCH_PROGRAM) {
                uint8_t prog = bs300_get_active_prog();
                uint8_t next = (uint8_t)((prog + 1) % 3);  /* 0→1→2→0, skip 3 */
                button_clear_action();
                int ret = bs300_switch_program_async(next,
                                                     on_btn_switch_done);
                PRINTF("[BTN] switch %u->%u ret=%d\r\n", prog, next, ret);
                (void)ret;
            } else if (btn == BTN_ACTION_VOLUME_UP) {
                uint8_t vol = bs300_get_module_volume(bs300_get_active_prog());
                uint8_t new_vol = (vol >= 9) ? 0 : (uint8_t)(vol + 1);
                button_clear_action();
                app_env.volume = new_vol;
                int ret = bs300_set_volume_async(new_vol,
                                                 on_btn_volume_done);
                PRINTF("[BTN] vol %u->%u ret=%d\r\n", vol, new_vol, ret);
                (void)ret;
            }
        }

        if (ble_env.state == APPM_CONNECTED)
        {
#ifdef DEBUG_UART_ENABLE
            {
                static uint8_t ble_connected_printed = 0;
                if (!ble_connected_printed)
                {
                    ble_connected_printed = 1;
                    PRINTF("__BLE_CONNECTED\r\n");
                }
            }
#endif
            if (app_env.send_batt_ntf && bass_support_env.enable)
            {
                app_env.send_batt_ntf = 0;

                Batt_LevelUpdateSend(0, app_env.batt_lvl, 0);
            }

            /* Handle BS300 commands from BLE RX characteristic */
            if (cs_env.rx_value_changed)
            {
                cs_env.rx_value_changed = 0;
                uint8_t cmd = cs_env.rx_value[0];
                uint8_t arg = cs_env.rx_value[1];
                PRINTF("[BS300] RX cmd=%02X arg=%02X\r\n", cmd, arg);
                if (cmd == 0x01 && arg < 4)
                {
                    int ret = bs300_switch_program_async(arg,
                                                    on_bs300_switch_done);
                    PRINTF("[BS300] switch_async ret=%d\r\n", ret);
                    if (ret < 0) {
                        cs_env.rx_value_changed = 1; /* retry next tick */
                    }
                }
                else if (cmd == 0x02)
                {
                    app_env.volume = arg;
                    bs300_set_volume_async(arg, on_bs300_volume_done);
                    PRINTF("[BS300] volume=%d\r\n", arg);
                }
                else if (cmd == 0xFE)
                {
                    uint8_t i;
                    for (i = 0; i < 4; i++) bs300_storage_invalidate(i);
                    bs300_settings_invalidate();
                    PRINTF("[BS300] cache cleared, reset to reload\r\n");
                }
            }

            /* Handle REMPRO (RT App) commands from ROLE characteristic */
            rempro_cmd_process();

            /* Update custom service characteristics, send notifications if
             * notification is enabled */
            if (cs_env.tx_value_changed && (cs_env.tx_cccd_value & 1))
            {
                cs_env.tx_value_changed = 0;

                /* Emulate value change with notification data */
                cs_env.val_notif = Emulate_CS_Val_Notif_Change(
                    cs_env.val_notif);
                memset(cs_env.tx_value, cs_env.val_notif,
                       APP_CS_TX_VALUE_NOTF_LENGTH);

                CustomService_SendNotification(ble_env.conidx,
                                               CS_IDX_TX_VALUE_VAL,
                                               &cs_env.tx_value[0],
                                               APP_CS_TX_VALUE_NOTF_LENGTH);
            }
        }

        Sys_Watchdog_Refresh();

        /* If not in the middle of a period measurement for RSOSC, allow the
         * application to go to sleep power mode.
         * Skip sleep when RM audio streaming is active. */
#ifdef APP_RM_ENABLE
        if (!app_env.audio_streaming)
        {
#endif
#ifndef DEBUG_UART_ENABLE
        if (low_power_clk_param.low_power_enable ||
            (RTC_CLK_SRC == RTC_CLK_SRC_XTAL32K))
        {
            Sys_DIO_Config(LED_DIO, DIO_MODE_GPIO_OUT_0);

            GLOBAL_INT_DISABLE();
            BLE_Power_Mode_Enter(&sleep_mode_env, POWER_MODE_SLEEP);
            GLOBAL_INT_RESTORE();
        }
#endif
#ifdef APP_RM_ENABLE
        }
#endif

#ifdef APP_RM_ENABLE
        if (app_env.audio_streaming)
        {
            SYS_WAIT_FOR_EVENT;
        }
        else
#endif
        {
#ifndef DEBUG_UART_ENABLE
            /* Wait for an interrupt before executing the scheduler again */
            SYS_WAIT_FOR_INTERRUPT;
#endif
        }
    }
}
