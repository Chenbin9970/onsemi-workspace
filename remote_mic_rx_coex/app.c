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
#include "dsp_7100_init.h"
#include <rsl10.h>
#include <printf.h>

int main()
{
	App_Initialize();
    /* Debug/trace initialization. In order to enable UART or RTT trace,
     * configure the 'OUTPUT_INTERFACE' macro in printf.h */

    PRINTF("__remote_mic_rx_coex has started!\r\n");
    /* Wait for 3 seconds to allow re-flashing directly after pressing RESET */
    Sys_Delay_ProgramROM(3 * SystemCoreClock);

    /* 7160test: 上电对 7100 做分阶段初始化（对照 star.csv），收发经 UART 打印 */
    dsp_7100_boot_init();

#ifndef DEBUG_UART_ENABLE
    /* Disable DIO4 and DIO5 to avoid current consumption on VDDO */
    Sys_DIO_Config(4, DIO_MODE_DISABLE | DIO_NO_PULL);
    Sys_DIO_Config(5, DIO_MODE_DISABLE | DIO_NO_PULL);
#endif

    while (1)
    {
        Kernel_Schedule();

        RM_StatusHandler();

        /* Refresh the watchdog timer */
        Sys_Watchdog_Refresh();

        /* Wait for an event before executing the scheduler again */
        SYS_WAIT_FOR_EVENT;
    }
}
