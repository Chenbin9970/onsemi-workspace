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

int main()
{
    App_Initialize();
    //App_sleep_Initialize();
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
