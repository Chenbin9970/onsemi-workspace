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

/* 按键 DIO12（参考 peripheral_server_sleep）：
 * 短按 = 当前程序音量 +1（0..9 循环）；长按(≥500ms) = 切程序 0→1→2→0（跳过 3）。
 * RM 音频中（程序3）屏蔽；BS300 忙/未初始化时不动作。主循环内周期调用。 */
static void Button_Process(void)
{
    enum { BTN_NONE, BTN_SHORT, BTN_LONG };
    static uint8_t btn_prev;
    static uint32_t hold_ticks;
    static uint8_t long_fired;
    static uint8_t pending_action;

    uint8_t i;
    uint8_t cnt_low = 0;
    uint8_t btn_now;

    for (i = 0; i < 5; i++)
    {
        if (DIO_DATA->ALIAS[BTN_DIO] == 0) cnt_low++;
    }
    btn_now = (cnt_low >= 3) ? 1 : 0;

    if (btn_now && !btn_prev)
    {
        hold_ticks = 0;
        long_fired = 0;
        pending_action = BTN_NONE;
    }
    else if (btn_now && btn_prev)
    {
        hold_ticks++;
        if (!long_fired && hold_ticks >= BTN_LONG_MS)
        {
            long_fired = 1;
            pending_action = BTN_LONG;
        }
        Sys_Delay_ProgramROM(SystemCoreClock / 1000);
    }
    else if (!btn_now && btn_prev)
    {
        if (!long_fired)
        {
            pending_action = BTN_SHORT;
        }
    }
    btn_prev = btn_now;

    if ((pending_action != BTN_NONE) && !bs300_sync_is_busy()
        && bs300_driver_is_cached()
        && (bs300_get_active_prog() != 3))
    {
        uint8_t prog = bs300_get_active_prog();
        if (pending_action == BTN_LONG)
        {
            bs300_switch_program_async((uint8_t)((prog + 1) % 3), 0);
        }
        else
        {
            bs300_set_volume_async(
                (uint8_t)((bs300_get_module_volume(prog) + 1) % 10), 0);
        }
        bs300_settings_persist();
        pending_action = BTN_NONE;
    }
}
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
        Button_Process();
        /* BS300 延迟动作在主循环处理（勿在定时器上下文做 flash 擦写等） */
        bs300_process_deferred();
#endif    /* ifdef BS300_ENABLE */

        /* Refresh the watchdog timer */
        Sys_Watchdog_Refresh();

#ifdef BS300_ENABLE
        /* 按键按住期间不要进 SYS_WAIT 休眠等待，否则主循环 ~200ms 才醒一次，
         * 长按计时(按迭代累加)会被稀释而永远到不了 BTN_LONG_MS。 */
        if (DIO_DATA->ALIAS[BTN_DIO] == 1)
        {
            SYS_WAIT_FOR_EVENT;
        }
#else    /* ifdef BS300_ENABLE */
        /* Wait for an event before executing the scheduler again */
        SYS_WAIT_FOR_EVENT;
#endif    /* ifdef BS300_ENABLE */
    }
}
