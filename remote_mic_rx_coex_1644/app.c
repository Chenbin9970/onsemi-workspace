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
    static uint8_t btn_started_in_rm;   /* 手势按下那一刻是否处于 RM 流中 */

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
        /* 起点落在 RM 流里的手势整条都不记动作：否则"压着按键时 RM 掉线"
         * （压→断只有几百 ms 的窄窗口）会在下一轮被当成新动作执行。 */
        btn_started_in_rm = app_env.audio_streaming;
    }
    else if (btn_now && btn_prev)
    {
        hold_ticks++;
        if (!long_fired && hold_ticks >= BTN_LONG_MS)
        {
            long_fired = 1;   /* 仍要置位：防止松手时被当成短按 */
            if (!btn_started_in_rm)
            {
                pending_action = BTN_LONG;
            }
        }
        Sys_Delay_ProgramROM(SystemCoreClock / 1000);
    }
    else if (!btn_now && btn_prev)
    {
        if (!long_fired && !btn_started_in_rm)
        {
            pending_action = BTN_SHORT;
        }
    }
    btn_prev = btn_now;

    /* RM 连接(流)中按键无效 —— 必须**丢弃**已挂起的动作，不能扣着等条件变好。
     *
     * 否则会延迟补触发：RM 流中长按 → 动作被扣住 → RM 断开时 audio_streaming
     * 已被 RM 回调清 0（同一轮主循环里 RM_StatusHandler 先于本函数跑），而切回
     * 原程序的会话又让 bs300_sync_is_busy() 为真 → 仍被扣住 → 会话一结束
     * （rm_bs300_switch_done 里 bs300_active()）就补切一次程序 0→1：提示音播的是
     * 程序2、上报 App 的却是程序1，与"返回原程序"自相矛盾，还会被下面的
     * bs300_settings_persist() 把程序1 落盘当成用户设定。按键无效 = 不响应，
     * 不是"稍后响应"。
     *
     * 这里兜的是"手势起于 RM 之前、松手时 RM 已连上"：起点不在 RM 里所以动作
     * 已记录，落地时 RM 已接管，同样要丢掉。 */
    if (app_env.audio_streaming)
    {
        pending_action = BTN_NONE;
    }
    else if ((pending_action != BTN_NONE)
        && !bs300_sync_is_busy()
        && bs300_driver_is_cached()
        && (bs300_get_active_prog() != 3))
    {
        uint8_t prog = bs300_get_active_prog();
        if (pending_action == BTN_LONG)
        {
            uint8_t next = (uint8_t)((prog + 1) % 3);
            bs300_switch_program_async(next, 0);
            /* 向手机推送场景/程序切换（参考 sleep：rempro_push_scene_change） */
            rempro_push_scene_change(next);
        }
        else
        {
            uint8_t vol = (uint8_t)((bs300_get_module_volume(prog) + 1) % 10);
            bs300_set_volume_async(vol, 0);
            /* 向手机推送音量变化（参考 sleep：rempro_push_volume_change） */
            rempro_push_volume_change(prog, vol);
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

    /* RM 音频流地址（`APP_RM_Init()` 里已从 Flash 取好）—— 那里中断还没开、不能打印，
     * 所以挪到这里报。出厂默认 0xF2CDE6；`from flash` = 回填了 BLE 89 号存的值。 */
    PRINTF("[RM] stream addr=0x%06lX (%s)\r\n",
           (unsigned long)RM_STREAM_ACCESSWORD_TO_ADDR(
                               app_env.rm_param.accessword),
           rm_stream_addr_from_flash() ? "from flash" : "default");

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
            /* 处理 Rempro 接收到的完整 HDLC 帧。
             * RM 连接(流)期间也照常收帧 —— 指令白名单在分发侧把关：
             * 只放行 GetDeviceConfig(26) / GetBatteryInfo(4) / GetCurrentScene(15)，
             * 其余静默丢弃、不回响应。见 ble_rempro_cmd.c 的 rempro_cmd_process()。 */
            rempro_cmd_process();
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
