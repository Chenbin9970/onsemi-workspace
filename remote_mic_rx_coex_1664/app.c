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
#include "ble_rempro_cmd.h"
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

        /* 分块发送 rempro TX（每次通知完成后推进下一块，无 ke_timer） */
        rempro_tx_poll();

        if (ble_env.state == APPM_CONNECTED)
        {
            /* RM 连接(流)期间不处理任何 BLE 指令，并清掉残留 RX 帧 */
            if (app_env.audio_streaming)
            {
                rempro_reasm_reset();
            }
            else
            {
                /* 处理 Rempro 接收到的完整 HDLC 帧 */
                rempro_cmd_process();
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
