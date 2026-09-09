/* ----------------------------------------------------------------------------
 * Copyright (c) 2017 Semiconductor Components Industries, LLC (d/b/a
 * ON Semiconductor), All Rights Reserved
 *
 * This code is the property of ON Semiconductor and may not be redistributed
 * in any form without prior written permission from ON Semiconductor.
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 *
 * This is Reusable Code.
 *
 * ----------------------------------------------------------------------------
 * app.c
 * This sample code demonstrates the coexistence of a Bluetooth low energy
 * connection while simultaneously receiving audio through the Audio Stream
 * Broadcast Custom Protocol for a remote microphone use case (remote microphone
 * custom protocol)
 * ----------------------------------------------------------------------------
 * $Revision: 1.4 $
 * $Date: 2019/12/27 18:50:37 $
 * ------------------------------------------------------------------------- */
#include "app.h"
#include <printf.h>
#ifdef BS300_ENABLE
#include "bs300_driver.h"
#include "bs300_ram_sync.h"
#endif    /* ifdef BS300_ENABLE */

int main()
{
    App_Initialize();
    /* Debug/trace initialization. In order to enable UART or RTT trace,
     * configure the 'OUTPUT_INTERFACE' macro in printf.h */
    PRINTF("__remote_mic_rx_coex has started!\r\n");

#ifdef BS300_ENABLE
    /* BS300 driver init：I2C init + 解锁/启动序列，首启约 2-3s 阻塞（已喂狗） */
    if (!bs300_driver_init())
    {
        PRINTF("__BS300_INIT_FAIL\r\n");
    }
    else
    {
        PRINTF("__BS300_INIT_OK\r\n");
    }
#endif    /* ifdef BS300_ENABLE */

    while (1)
    {
        Kernel_Schedule();

        if (ble_env.state == APPM_CONNECTED)
        {
            if (app_env.send_batt_ntf && bass_support_env.enable)
            {
            	PRINTF("__SEND BATTERY LEVEL\n %d\n",app_env.batt_lvl);
                app_env.send_batt_ntf = 0;
                Batt_LevelUpdateSend(0, app_env.batt_lvl, 0);
            }
        }

        RM_StatusHandler();

#ifdef BS300_ENABLE
        /* BS300 延迟动作在主循环处理（勿在定时器上下文做 flash 擦写等） */
        bs300_process_deferred();
#endif    /* ifdef BS300_ENABLE */

        /* Refresh the watchdog timer */
        Sys_Watchdog_Refresh();

        /* Wait for an event before executing the scheduler again */
        SYS_WAIT_FOR_EVENT;
    }
}
