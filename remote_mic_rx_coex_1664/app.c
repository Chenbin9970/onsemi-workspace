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
#include "dsp_7100_init.h"
#include "dsp_7100_cmd.h"
#include "i2c_7100_hal.h"

int main()
{
    App_Initialize();
    /* Debug/trace initialization. In order to enable UART or RTT trace,
     * configure the 'OUTPUT_INTERFACE' macro in printf.h */
    PRINTF("__remote_mic_rx_coex has started!\r\n");

    /* 本次开机 RM 音频流地址（from flash = 89 号写入的持久化值已被采用；
     * default = 无记录，用出厂值。两者取值可能恰好相同，只能靠这个标签区分）。
     * 必须在 App_Initialize() 之后打 —— APP_RM_Init() 里打会死锁，见开发文档 §19。 */
    PRINTF("[RM] stream addr=0x%06lX (%s)\r\n",
           (unsigned long)RM_STREAM_ACCESSWORD_TO_ADDR(app_env.rm_param.accessword),
           rm_stream_addr_from_flash() ? "from flash" : "default");

    /* 上电握手（照 remote_mic_rx_coex 参考设计）：
     * DIO13 = 7100 输出 → RSL10 输入；DIO11 = RSL10 输出 → 7100 输入。
     * 7100 上电先拉低 DIO13 等待；RSL10 检测到低后在 DIO11 发一个低脉冲应答。
     *
     * 2026-09-30 做过一轮 DIO11 极性/脉冲/0xFC 调试，**已全部回退**到参考设计原样 ——
     * 试了什么、测到什么，记录在 docs/remote_mic_rx_coex_1664_开发文档.md §20。
     * 唯一保留的偏离：引导后那个脉冲的宽度由参考设计的 1 秒改成 1ms（来历见下方 §21）。 */
    Sys_DIO_Config(13, DIO_MODE_INPUT | DIO_WEAK_PULL_UP | DIO_LPF_DISABLE);
    Sys_DIO_Config(11, DIO_MODE_GPIO_OUT_1);
    Sys_DIO_Config(9,  DIO_MODE_INPUT | DIO_WEAK_PULL_UP | DIO_LPF_DISABLE);
    Sys_DIO_Config(10, DIO_MODE_INPUT | DIO_WEAK_PULL_UP | DIO_LPF_DISABLE);

    /* 先装 DIO13 边沿中断再握手（见 DSP7100_DIO13_IRQ_ENABLE）：让握手本身当
     * 阳性对照 —— 中断若通，握手必然产生 1 次下降 + 1 次上升；之后再看 106 步
     * 引导有没有多出来的边沿。98–106 步的 A2 写每次开机都跑，不用擦 storage。 */
    dsp_7100_dio13_irq_arm();

    PRINTF("[IO] wait DIO13 low (7100 ready) ...\r\n");
    while (DIO_DATA->ALIAS[13] == 1)
    {
        Sys_Watchdog_Refresh();
        Sys_Delay_ProgramROM(SystemCoreClock / 1000);   /* 1ms */
    }

    /* DIO13 低：DIO11 做一个极短低脉冲应答 */
    PRINTF("[IO] DIO13 low -> DIO11 low pulse\r\n");
    Sys_DIO_Config(11, DIO_MODE_GPIO_OUT_0);
    Sys_DIO_Config(11, DIO_MODE_GPIO_OUT_1);

    /* 等 DIO13 回到高（7100 应答握手完成） */
    PRINTF("[IO] wait DIO13 high ...\r\n");
    while (DIO_DATA->ALIAS[13] == 0)
    {
        Sys_Watchdog_Refresh();
        Sys_Delay_ProgramROM(SystemCoreClock / 1000);   /* 1ms */
    }
    PRINTF("[IO] DIO13 high, run 7100 init\r\n");

    /* 握手结束，打印累计边沿数当基线（ISR 此时早已跑过）。主循环里那条减去
     * 这条，差值就是下面 106 步引导的贡献。 */
    dsp_7100_dio13_irq_poll();

    /* 7100 同步引导（A7 之前的普通步）；读回由 200ms tick 推进 */
    dsp_7100_boot_init();

    /* 开机优先用 flash 缓存的读回结果；未命中才走 I2C 读回（读完自动落盘） */
    dsp_7100_cache_try_load();

    /* 引导完：DIO11 发一个低脉冲（与开机握手那个脉冲同形）。
     *
     * ⚠ 宽度 **1ms 是安全余量，实测 0 也够**（两条背靠背的寄存器写）—— 7100 认的是
     *   这条线的**边沿**，不是电平宽度。**别删这个脉冲**：删了读回会卡死（见下）。
     *
     * 来历（完整对照表见 docs/remote_mic_rx_coex_1664_开发文档.md §21）：
     *   参考设计这里写的是 `Sys_Delay_ProgramROM(1000 * (SystemCoreClock / 1000))`、
     *   注释标「~2ms」—— 其实该函数收的是**时钟周期**不是 ms，算出来是 **1 秒**
     *   （串口时间戳实证 1018ms），也就是白等 1 秒才进 while(1)，BLE 广播跟着晚 1 秒。
     *   实测四态（每轮 28 步读回）：
     *     脉冲 1s + 延时 1s   → 28/28，1.035s
     *     脉冲 1ms、无 1s 延时 → 28/28，1.029s   ← 本版，数据 cache CRC 与上一行逐字节相同
     *     只有延时、无脉冲     → 第 28 步永久卡死（读回头错位一字节 → hlen 被夹到 700B）
     *     两者都没有           → 卡第 9 步，整轮 8.8s
     *   即：**脉冲不能删，但它带的 1 秒可以。** */
    Sys_DIO_Config(11, DIO_MODE_GPIO_OUT_0);
    Sys_Delay_ProgramROM(SystemCoreClock / 1000);   /* 1ms（0 也够，留余量） */
    Sys_DIO_Config(11, DIO_MODE_GPIO_OUT_1);

    while (1)
    {
        /* IO 电平有变化才打印：DIO9/10/13（7100 ready 等，照 remote_mic_rx_coex） */
        {
            static uint8_t l9 = 0xFF, l10 = 0xFF, l13 = 0xFF;
            uint8_t n9  = (uint8_t)DIO_DATA->ALIAS[9];
            uint8_t n10 = (uint8_t)DIO_DATA->ALIAS[10];
            uint8_t n13 = (uint8_t)DIO_DATA->ALIAS[13];
            if (n9 != l9 || n10 != l10 || n13 != l13) {
                l9 = n9; l10 = n10; l13 = n13;
                PRINTF("[IO] D9=%u D10=%u D13=%u\r\n", n9, n10, n13);
            }
        }

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

        /* 7100 读回落盘（flash 擦写不在定时器上下文做） */
        dsp_7100_process_deferred();

        /* DIO13 边沿（7100 响应就绪）：边沿在 ISR 里计数，这里打印 */
        dsp_7100_dio13_irq_poll();

        /* 命令会话推进（切程序 / 调音量 / 降噪 / DFBC / WDRC / EQ / 纯音 / 静音 / 测听）：
         * 与读回同一套模型 —— 发完命令等上升沿、读完发了 82 等下降沿，
         * 边沿不来则退回 200ms tick 兜底（见 dsp_7100_cmd.c 的 dsp_7100_cmd_poll）。 */
        dsp_7100_cmd_poll();

        /* 读回推进：两个相位都由 DIO13 边沿驱动 —— 发完等上升沿就开读、
         * 读完发了 82 等下降沿就发下一条，都不等满 200ms。边沿不来则退回 200ms tick
         * 兜底（见 dsp_7100_init.c 的 dsp_7100_rb_poll）。
         * 命令会话占着 I2C 时一律不推进读回 —— 会话的每条 82 也会产生 DIO13 边沿，
         * 不挡住的话读回会把这些边沿当成自己的节奏，插进会话的事务里。 */
        if (!dsp_7100_cmd_busy())
        {
            dsp_7100_rb_poll();
        }

        /* Refresh the watchdog timer */
        Sys_Watchdog_Refresh();

        /* Wait for an event before executing the scheduler again */
        SYS_WAIT_FOR_EVENT;
    }
}
