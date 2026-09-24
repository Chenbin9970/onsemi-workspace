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
 * This sample code demonstrates switching from the Bluetooth low energy mode
 * (during an active connection) to transmitting audio through the Audio Stream
 * Broadcast Custom Protocol for a remote microphone use case (remote microphone
 * custom protocol)
 * ----------------------------------------------------------------------------
 * $Revision: 1.4 $
 * $Date: 2019/12/27 18:50:38 $
 * ------------------------------------------------------------------------- */
#include "app.h"
#include <printf.h>

int main()
{
    App_Initialize();
    /* Debug/trace initialization. In order to enable UART or RTT trace,
     * configure the 'OUTPUT_INTERFACE' macro in printf.h */
    printf_init();
    PRINTF("__remote_mic_tx_coex has started!\r\n");

    while (1)
    {
        Kernel_Schedule();

        if ((ble_env.state == APPM_CONNECTED) &&
            (basc_support_env.enable == true) && (app_env.send_batt_req >= 25))
        {
            app_env.send_batt_req = 0;
            Batt_SendReadInfoReq(ble_env.conidx, 0, BASC_BATT_LVL_VAL);
        }

        RM_StatusHandler();

        /* Refresh the watchdog timer */
        Sys_Watchdog_Refresh();

        /* Wait for an event before executing the scheduler again */
        SYS_WAIT_FOR_EVENT;
    }
}
