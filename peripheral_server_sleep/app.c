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

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

/* Called when async BS300 program switch completes — re-activate DSP + notify */
static void on_bs300_switch_done(void)
{
    cs_env.tx_value_changed = 1;
    bs300_async_done_callback();
    if (!app_env.sync_from_remote && ble_env.peer_ear_connected
        && ble_env.peer_ear_gatt_ready)
    {
        uint8_t prog = bs300_get_active_prog();
        CS_Peer_WriteRX(ble_env.peer_ear_conidx, 0x01, prog);
    }
    app_env.sync_from_remote = false;
}

static void on_bs300_volume_done(void)
{
    cs_env.tx_value_changed = 1;
    bs300_async_done_callback();
    if (!app_env.sync_from_remote && ble_env.peer_ear_connected
        && ble_env.peer_ear_gatt_ready)
    {
        uint8_t prog = bs300_get_active_prog();
        uint8_t vol  = bs300_get_module_volume(prog);
        CS_Peer_WriteRX(ble_env.peer_ear_conidx, 0x02, vol);
    }
    app_env.sync_from_remote = false;
}

/* Button-path done callbacks: also restore low-power (I2C complete → can sleep) */
static void on_btn_switch_done(void)
{
    cs_env.tx_value_changed = 1;
    bs300_async_done_callback();
    if (!app_env.sync_from_remote && ble_env.peer_ear_connected
        && ble_env.peer_ear_gatt_ready)
    {
        uint8_t prog = bs300_get_active_prog();
        CS_Peer_WriteRX(ble_env.peer_ear_conidx, 0x01, prog);
    }
    app_env.sync_from_remote = false;
    low_power_clk_param.low_power_enable = true;
}

static void on_btn_volume_done(void)
{
    cs_env.tx_value_changed = 1;
    bs300_async_done_callback();
    if (!app_env.sync_from_remote && ble_env.peer_ear_connected
        && ble_env.peer_ear_gatt_ready)
    {
        uint8_t prog = bs300_get_active_prog();
        uint8_t vol  = bs300_get_module_volume(prog);
        CS_Peer_WriteRX(ble_env.peer_ear_conidx, 0x02, vol);
    }
    app_env.sync_from_remote = false;
    low_power_clk_param.low_power_enable = true;
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

    /* Button DIO12: must be re-init after each wakeup (see Continue_Application) */
    Sys_DIO_Config(12, DIO_MODE_GPIO_IN_0 | DIO_WEAK_PULL_UP | DIO_LPF_DISABLE);

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

#ifdef APP_RM_ENABLE
    /* Cold-boot RM init disabled for peer ear testing */
    //{
    //    static uint8_t rm_cold_boot_done = 0;
    //    if (!rm_cold_boot_done) {
    //        rm_cold_boot_done = 1;
    //        app_env.saved_prog_before_rm = bs300_get_active_prog();
    //        Audio_Init();
    //        RF_SwitchToCPMode();
    //        RM_Enable(500);
    //        app_env.audio_streaming = 1;
    //        app_env.rm_disc_state = RM_DISC_HEARING_AID;
    //        app_env.rm_timeout_ticks = RM_TIMEOUT_TICKS;
    //    }
    //}
#endif

    while (true)
    {
        Kernel_Schedule();

#ifdef APP_RM_ENABLE
        RM_StatusHandler();

        if (app_env.tx_connect_detected)
        {
            app_env.tx_connect_detected = 0;

            if (!app_env.audio_streaming)
            {
                app_env.rm_disc_state = RM_DISC_NONE;
                app_env.rm_timeout_ticks = 0;
                app_env.saved_prog_before_rm = bs300_get_active_prog();

                APP_RM_Init(ear_side);
                Audio_Init();
                RF_SwitchToCPMode();
                RM_Enable(500);
                app_env.audio_streaming = 1;
                app_env.rm_disc_state = RM_DISC_HEARING_AID;
                app_env.rm_timeout_ticks = RM_TIMEOUT_TICKS;
            }
        }

        if (app_env.rm_start_requested)
        {
            app_env.rm_start_requested = 0;

            /* Only start if RM is completely off (audio_streaming=0).
             * Skip when already connected, searching, or debouncing.
             * Program switch is deferred to LINK_ESTABLISHED. */
            if (!app_env.audio_streaming)
            {
                app_env.rm_disc_state = RM_DISC_NONE;
                app_env.rm_timeout_ticks = 0;
                app_env.saved_prog_before_rm = bs300_get_active_prog();

                APP_RM_Init(ear_side);
                Audio_Init();
                RF_SwitchToCPMode();
                RM_Enable(500);
                app_env.audio_streaming = 1;
            }
        }

        if (app_env.rm_stop_requested)
        {
            app_env.rm_stop_requested = 0;
            app_env.rm_disc_state = RM_DISC_NONE;
            app_env.rm_timeout_ticks = 0;

            /* BS300 mute/active only needed if actually on program 3.
             * During link-establish window, BS300 is still on hearing aid program. */
            if (bs300_get_active_prog() == 3)
            {
                bs300_mute();
            }

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
            app_env.audio_streaming = 0;
            RM_Disable();
            Sys_Timers_Stop(SELECT_TIMER0);
            Sys_Timers_Stop(SELECT_TIMER1);
            NVIC_ClearPendingIRQ(TIMER0_IRQn);
            NVIC_ClearPendingIRQ(TIMER1_IRQn);
            RF_SwitchToBLEMode();
#ifdef RM_TX_POWER_BOOST
            Sys_RFFE_SetTXPower(0);
#endif

            /* Restore pre-RM program for normal hearing aid operation */
            if (bs300_get_active_prog() == 3
                && app_env.saved_prog_before_rm != 3)
            {
                bs300_switch_program(app_env.saved_prog_before_rm);
                bs300_active();
            }

            /* Restart BLE advertising so phone can reconnect */
            ble_env.is_advertising = false;
            ble_env.state = APPM_READY;
            Advertising_Start();

            low_power_clk_param.low_power_enable = true;
        }

        /* RM disconnect → keep RM state, fall back to hearing aid.
         * DEBOUNCE: wait for transient disconnects to resolve.
         * On timeout: switch to hearing aid program + restart RM search. */
        if (app_env.rm_disc_state == RM_DISC_DEBOUNCE) {
            app_env.rm_disc_counter++;
            if (app_env.rm_disc_counter >= RM_DISC_DEBOUNCE_THRESHOLD) {
                app_env.rm_disc_state = RM_DISC_HEARING_AID;
                if (app_env.saved_prog_before_rm != 3) {
                    bs300_switch_program(app_env.saved_prog_before_rm);
                }
                bs300_active();
                RM_Enable(500);
            }
        }

        /* 200ms tick: peer ear retry countdown + RM timeout.
         * Re-arm here because handler self-re-arm fails in CP. */
        if (app_env.timer_200ms) {
            app_env.timer_200ms = 0;

            /* Peer ear retry countdown — driven by 200ms timer. */
            if (ble_env.peer_ear_retry_ticks > 0) {
                ble_env.peer_ear_retry_ticks--;
            }

            /* Connection attempt timeout: cancel if peer is unreachable */
            if (ble_env.peer_ear_retry_ticks == 0
                && ble_env.peer_ear_state == PEER_EAR_CONNECTING)
            {
                PRINTF("[PEER_EAR] connect timeout, cancelling\r\n");
                struct gapm_cancel_cmd *cancel;
                cancel = KE_MSG_ALLOC(GAPM_CANCEL_CMD, TASK_GAPM, TASK_APP,
                                      gapm_cancel_cmd);
                cancel->operation = GAPM_CANCEL;
                ke_msg_send(cancel);
            }

            if (app_env.audio_streaming) {
                if (app_env.rm_timeout_ticks > 0
                    && app_env.rm_disc_state != RM_DISC_NONE) {
                    app_env.rm_timeout_ticks--;
                    if (app_env.rm_timeout_ticks == 0) {
                        app_env.rm_stop_requested = 1;
                    }
                }
            }

            /* Re-arm 200ms timer when either countdown is active */
            if (ble_env.peer_ear_retry_ticks > 0
                || (app_env.audio_streaming
                    && app_env.rm_timeout_ticks > 0
                    && app_env.rm_disc_state != RM_DISC_NONE))
            {
                ke_timer_set(APP_TEST_TIMER, TASK_APP,
                             TIMER_200MS_SETTING);
            }
        }
#endif

        Sys_Watchdog_Refresh();

#ifdef DEBUG_UART_ENABLE
        {
            static uint32_t tick_cnt = 0;
            if (++tick_cnt >= 10000)
            {
                tick_cnt = 0;
            }
        }
#endif

        /* Process deferred BS300 ops (aborted switch etc.) */
        bs300_process_deferred();

        /* Peer ear connection state machine (central role).
         * Only the left ear (ear_side == RM_LEFT) acts as Central.
         * Countdown driven by 200ms timer. Skip when RM streaming. */
        if (ear_side == RM_LEFT
            && ble_env.peer_ear_retry_ticks == 0 &&
            (ble_env.peer_ear_state == PEER_EAR_IDLE ||
             ble_env.peer_ear_state == PEER_EAR_RETRY_WAIT)
#ifdef APP_RM_ENABLE
            && !app_env.audio_streaming
#endif
            )
        {
            ble_env.peer_ear_retry_ticks = 1;
            PeerEar_TryConnect();
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
                    bs300_set_volume_notone_async(arg, on_bs300_volume_done);
                    PRINTF("[BS300] volume=%d\r\n", arg);
                }
                else if (cmd == 0xFE)
                {
                    uint8_t i;
                    for (i = 0; i < 4; i++) bs300_storage_invalidate(i);
                    bs300_settings_invalidate();
                    bs300_reset_to_defaults();
                    PRINTF("[BS300] cache cleared, reset to reload\r\n");
                }
            }

            /* Handle REMPRO (RT App) commands from ROLE characteristic */
            rempro_cmd_process();

            /* Update custom service characteristics, send notifications if
             * notification is enabled */
            if (cs_env.tx_value_changed && (cs_env.tx_cccd_value & 1))
            {
                uint8_t prog = bs300_get_active_prog();
                uint8_t vol  = bs300_get_module_volume(prog);

                cs_env.tx_value_changed = 0;

                /* TX notification: [counter, prog, vol, 0, 0]
                 * Byte 0: rolling counter (phone keepalive)
                 * Byte 1-2: actual program/volume (ear-to-ear sync) */
                cs_env.val_notif = Emulate_CS_Val_Notif_Change(
                    cs_env.val_notif);
                cs_env.tx_value[0] = cs_env.val_notif;
                cs_env.tx_value[1] = prog;
                cs_env.tx_value[2] = vol;
                cs_env.tx_value[3] = 0;
                cs_env.tx_value[4] = 0;

                CustomService_SendNotification(ble_env.conidx,
                                               CS_IDX_TX_VALUE_VAL,
                                               &cs_env.tx_value[0],
                                               APP_CS_TX_VALUE_NOTF_LENGTH);
            }
        }

        Sys_Watchdog_Refresh();


        /* Button on DIO2 (active low, pull-up).
         * Short press (< 1.5s):  volume +1, 0→1→...→9→0
         * Long  press (>= 1.5s): switch program, 0→1→2→0, skip Program 3 */
        {
            enum { BTN_NONE, BTN_SHORT, BTN_LONG };
            static uint8_t btn_prev;
            static uint32_t hold_ticks;
            static uint8_t long_fired;
            static uint8_t pending_action;

            uint8_t i, cnt_low = 0;
            uint8_t btn_now;

            /* Multi-sample filter: 5x read, majority vote */
            for (i = 0; i < 5; i++)
            {
                if (DIO_DATA->ALIAS[12] == 0) cnt_low++;
            }
            btn_now = (cnt_low >= 3) ? 1 : 0;

            if (btn_now && !btn_prev)
            {
                /* Press edge — start hold timer */
                low_power_clk_param.low_power_enable = false;
                hold_ticks = 0;
                long_fired = 0;
                pending_action = BTN_NONE;
            }
            else if (btn_now && btn_prev)
            {
                /* Held — block sleep, count ~1ms ticks */
                low_power_clk_param.low_power_enable = false;
                hold_ticks++;
                if (!long_fired && hold_ticks >= 500)
                {
                    long_fired = 1;
                    pending_action = BTN_LONG;
                }
                Sys_Delay_ProgramROM(SystemCoreClock / 1000);
            }
            else if (!btn_now && btn_prev)
            {
                /* Release edge */
                if (!long_fired)
                {
                    pending_action = BTN_SHORT;
                }
                else if (!bs300_sync_is_busy())
                {
                    /* Long press I2C already done — safe to sleep now */
                    low_power_clk_param.low_power_enable = true;
                }
            }
            btn_prev = btn_now;

            /* Process pending action when I2C is free.
             * Block button actions in program 3 (RM audio mode). */
            if (pending_action != BTN_NONE && !bs300_sync_is_busy()
                && bs300_get_active_prog() != 3)
            {
                if (pending_action == BTN_LONG)
                {
                    uint8_t prog = bs300_get_active_prog();
                    uint8_t next = (prog + 1) % 3;
                    rempro_push_scene_change(next);
                    /* Sync to peer ear immediately */
                    if (ble_env.peer_ear_connected && ble_env.peer_ear_gatt_ready) {
                        CS_Peer_WriteRX(ble_env.peer_ear_conidx, 0x01, next);
                        app_env.sync_from_remote = true;
                    }
                    bs300_switch_program_async(next, on_btn_switch_done);
                    bs300_settings_persist();
                }
                else
                {
                    uint8_t prog = bs300_get_active_prog();
                    uint8_t vol = (bs300_get_module_volume(prog) + 1) % 10;
                    rempro_push_volume_change(prog, vol);
                    /* Sync to peer ear immediately */
                    if (ble_env.peer_ear_connected && ble_env.peer_ear_gatt_ready) {
                        CS_Peer_WriteRX(ble_env.peer_ear_conidx, 0x02, vol);
                        app_env.sync_from_remote = true;
                    }
                    bs300_set_volume_async(vol, on_btn_volume_done);
                    bs300_settings_persist();
                }
                pending_action = BTN_NONE;
            }
        }

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
            if (low_power_clk_param.low_power_enable)
                SYS_WAIT_FOR_EVENT;
        }
        else
#endif
        {
#ifndef DEBUG_UART_ENABLE
            if (low_power_clk_param.low_power_enable)
                SYS_WAIT_FOR_INTERRUPT;
#endif
        }
    }
}
