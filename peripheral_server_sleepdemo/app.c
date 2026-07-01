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

    /* Wait for 3 seconds to allow re-flashing directly after pressing RESET */
    Sys_Delay_ProgramROM(3 * SystemCoreClock);

    /* Turn LED on */
    Sys_DIO_Config(LED_DIO, DIO_MODE_GPIO_OUT_1);

    /* Disable DIO4 and DIO5 to avoid current consumption on VDDO */
    Sys_DIO_Config(4, DIO_MODE_DISABLE | DIO_NO_PULL);
    Sys_DIO_Config(5, DIO_MODE_DISABLE | DIO_NO_PULL);

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

        if (ble_env.state == APPM_CONNECTED)
        {
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
         * application to go to sleep power mode. */
        if (low_power_clk_param.low_power_enable ||
            (RTC_CLK_SRC == RTC_CLK_SRC_XTAL32K))
        {
            Sys_DIO_Config(LED_DIO, DIO_MODE_GPIO_OUT_0);

            GLOBAL_INT_DISABLE();
            BLE_Power_Mode_Enter(&sleep_mode_env, POWER_MODE_SLEEP);
            GLOBAL_INT_RESTORE();
        }

        /* Wait for an interrupt before executing the scheduler again */
        SYS_WAIT_FOR_INTERRUPT;
    }
}
