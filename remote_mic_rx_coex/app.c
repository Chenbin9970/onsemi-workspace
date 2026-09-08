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
#include "i2c_7100_hal.h"
#include <rsl10.h>
#include <printf.h>

int main()
{
	App_Initialize();
    /* Debug/trace initialization. In order to enable UART or RTT trace,
     * configure the 'OUTPUT_INTERFACE' macro in printf.h */

    PRINTF("__remote_mic_rx_coex has started!\r\n");
    /* Wait for 3 seconds to allow re-flashing directly after pressing RESET */
    //Sys_Delay_ProgramROM(3 * SystemCoreClock);

    /* 上电握手：
     * DIO13 = 7100 输出 → RSL10 输入；DIO11 = RSL10 输出 → 7100 输入。
     * 7100 上电先拉低 DIO13 等待；RSL10 检测到 DIO13 低后，在 DIO11 发一个低脉冲应答。 */
    {
        /* DIO13 配输入弱上拉（读 7100）；DIO11 配输出，空闲高；DIO9/10 配输入观察 */
        Sys_DIO_Config(13, DIO_MODE_INPUT | DIO_WEAK_PULL_UP | DIO_LPF_DISABLE);
        Sys_DIO_Config(11, DIO_MODE_GPIO_OUT_1);
        Sys_DIO_Config(9, DIO_MODE_INPUT | DIO_WEAK_PULL_UP | DIO_LPF_DISABLE);
        Sys_DIO_Config(10, DIO_MODE_INPUT | DIO_WEAK_PULL_UP | DIO_LPF_DISABLE);

        PRINTF("[IO] wait DIO13 low (7100 ready) ...\r\n");
        while (DIO_DATA->ALIAS[13] == 1) {
            Sys_Watchdog_Refresh();
            Sys_Delay_ProgramROM(SystemCoreClock / 1000);   /* 1ms */
        }

        /* DIO13 低：DIO11 立即低再高，做一个极短低脉冲应答（不延时） */
        PRINTF("[IO] DIO13 low -> DIO11 low pulse\r\n");
        Sys_DIO_Config(11, DIO_MODE_GPIO_OUT_0);
        Sys_DIO_Config(11, DIO_MODE_GPIO_OUT_1);

        /* 等 DIO13 回到高（7100 应答握手完成） */
        PRINTF("[IO] wait DIO13 high ...\r\n");
        while (DIO_DATA->ALIAS[13] == 0) {
            Sys_Watchdog_Refresh();
            Sys_Delay_ProgramROM(SystemCoreClock / 1000);   /* 1ms */
        }
        PRINTF("[IO] DIO13 high, run 7100 init\r\n");
    }

    /* 7100 同步初始化(A7 前普通步)；A7-06 由 5s 定时器周期发，poll 在 while 里读回 */
    dsp_7100_boot_init();

    /* 初始化完：DIO11 发一个 ~2ms 低脉冲 */
    Sys_DIO_Config(11, DIO_MODE_GPIO_OUT_0);
    Sys_Delay_ProgramROM(1000 * (SystemCoreClock / 1000));   /* ~2ms */
    Sys_DIO_Config(11, DIO_MODE_GPIO_OUT_1);

    //dsp_7100_a7_arm();   /* DIO13 上升沿中断暂注释 */

#ifndef DEBUG_UART_ENABLE
    /* Disable DIO4 and DIO5 to avoid current consumption on VDDO */
    Sys_DIO_Config(4, DIO_MODE_DISABLE | DIO_NO_PULL);
    Sys_DIO_Config(5, DIO_MODE_DISABLE | DIO_NO_PULL);
#endif

    while (1)
    {
        /* IO 电平有变化才打印：DIO9/10(输入) + DIO13 */
        {
            static uint8_t l9 = 0xFF, l10 = 0xFF, l13 = 0xFF;
            uint8_t n9 = (uint8_t)DIO_DATA->ALIAS[9];
            uint8_t n10 = (uint8_t)DIO_DATA->ALIAS[10];
            uint8_t n13 = (uint8_t)DIO_DATA->ALIAS[13];
            if (n9 != l9 || n10 != l10 || n13 != l13) {
                l9 = n9; l10 = n10; l13 = n13;
                PRINTF("[IO] D9=%u D10=%u D13=%u\r\n", n9, n10, n13);
            }
        }

        Kernel_Schedule();

        RM_StatusHandler();

        /* Refresh the watchdog timer */
        Sys_Watchdog_Refresh();

        /* Wait for an event before executing the scheduler again */
        SYS_WAIT_FOR_EVENT;
    }
}
