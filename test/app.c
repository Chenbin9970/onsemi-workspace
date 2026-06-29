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
 */

#include "app.h"

int main()
{
    App_Initialize();

    Sys_Delay_ProgramROM(3 * SystemCoreClock);

    Sys_DIO_Config(LED_DIO, DIO_MODE_GPIO_OUT_1);

    Main_Loop();
}

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
            if (cs_env.tx_value_changed && (cs_env.tx_cccd_value & 1))
            {
                cs_env.tx_value_changed = 0;
                cs_env.val_notif = Emulate_CS_Val_Notif_Change(cs_env.val_notif);
                memset(cs_env.tx_value, cs_env.val_notif, APP_CS_TX_VALUE_NOTF_LENGTH);
                CustomService_SendNotification(ble_env.conidx,
                    CS_IDX_TX_VALUE_VAL, &cs_env.tx_value[0],
                    APP_CS_TX_VALUE_NOTF_LENGTH);
            }
        }
        Sys_Watchdog_Refresh();
        if (low_power_clk_param.low_power_enable ||
            (RTC_CLK_SRC == RTC_CLK_SRC_XTAL32K))
        {
            Sys_DIO_Config(LED_DIO, DIO_MODE_GPIO_OUT_0);
            GLOBAL_INT_DISABLE();
            BLE_Power_Mode_Enter(&sleep_mode_env, POWER_MODE_SLEEP);
            GLOBAL_INT_RESTORE();
        }
        SYS_WAIT_FOR_INTERRUPT;
    }
}
