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
 * - Simple application to demonstrate a typical remote microphone application
 * - The application needs to be configured either as a transmitter of audio
 * - or the receiver through app.h
 * ----------------------------------------------------------------------------
 * $Revision: 1.3 $
 * $Date: 2019/12/30 20:50:47 $
 * ------------------------------------------------------------------------- */

#include "app.h"
#include <printf.h>

int main()
{
    /* Initialize the system. */
    App_Initialize();
    printf_init();
    PRINTF("__remote_mic_rx_raw has started! with %s\n", (CODEC_CONFIG & 0x1 ? "CELT" : "G722"));

    /* Initialize Remote Microphone BLE application. */
    APP_RM_Init(ear_side);

    PRINTF("__RM_START\n");
    while (1)
    {
        /* Refresh the watch-dog timer. */
        Sys_Watchdog_Refresh();

        /* Handle Remote Microphone BLE application pending events. */
        RM_StatusHandler();

        /* Wait for an event before executing the scheduler again. */
        SYS_WAIT_FOR_EVENT;
    }
}
