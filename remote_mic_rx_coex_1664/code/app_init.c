/* ----------------------------------------------------------------------------
 * Copyright (c) 2015-2017 Semiconductor Components Industries, LLC (d/b/a
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
 * app_init.c
 * - Application initialization
 * ----------------------------------------------------------------------------
 * $Revision: 1.16 $
 * $Date: 2018/03/26 21:32:24 $
 * ------------------------------------------------------------------------- */

#include "app.h"

#if (OUTPUT_DECODE_PATH)
#include "dsp_pm_dm.h"
#endif    /* if (OUTPUT_DECODE_PATH) */

/* OD 直驱输出缓存（ASRC OUT DMA → BufferOut → OD DMA → OD_DATA） */
int16_t BufferOut[2 * FRAME_LENGTH];

uint8_t buff_test[100] = {
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x11, 0x12, 0x13, 0x14,
    0x15, 0x16, 0x17, 0x18, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18
};

/* ----------------------------------------------------------------------------
 * Function      : void App_Initialize(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize the system for proper application execution
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void App_Initialize(void)
{
    SCB->VTOR = (unsigned int)(&ISR_Vector_Table);

    /* Mask all interrupts */
    __set_PRIMASK(PRIMASK_DISABLE_INTERRUPTS);

    /* Disable all interrupts and clear any pending interrupts */
    Sys_NVIC_DisableAllInt();
    Sys_NVIC_ClearAllPendingInt();

    DIO_JTAG_SW_PAD_CFG->CM3_JTAG_DATA_EN_ALIAS =
        CM3_JTAG_DATA_DISABLED_BITBAND;
    DIO_JTAG_SW_PAD_CFG->CM3_JTAG_TRST_EN_ALIAS =
        CM3_JTAG_TRST_DISABLED_BITBAND;

    /* Test DIO13 to pause the program to make it easy to re-flash */
    DIO->CFG[RECOVERY_DIO] = DIO_MODE_INPUT  | DIO_WEAK_PULL_UP |
                             DIO_LPF_DISABLE | DIO_6X_DRIVE;
    while (DIO_DATA->ALIAS[RECOVERY_DIO] == 0);

    /* Configure the current trim settings for VCC, VDDA */
    ACS_VCC_CTRL->ICH_TRIM_BYTE  = VCC_ICHTRIM_16MA_BYTE;
    ACS_VDDA_CP_CTRL->PTRIM_BYTE = VDDA_PTRIM_16MA_BYTE;

    /* Start 48 MHz XTAL oscillator */
    ACS_VDDRF_CTRL->ENABLE_ALIAS = VDDRF_ENABLE_BITBAND;
    ACS_VDDRF_CTRL->CLAMP_ALIAS  = VDDRF_DISABLE_HIZ_BITBAND;

    /* Wait until VDDRF supply has powered up */
    while (ACS_VDDRF_CTRL->READY_ALIAS != VDDRF_READY_BITBAND);

    ACS_VDDPA_CTRL->ENABLE_ALIAS = VDDPA_DISABLE_BITBAND;
    ACS_VDDPA_CTRL->VDDPA_SW_CTRL_ALIAS    = VDDPA_SW_VDDRF_BITBAND;

    /* Enable RF power switches */
    SYSCTRL_RF_POWER_CFG->RF_POWER_ALIAS   = RF_POWER_ENABLE_BITBAND;

    /* Remove RF isolation */
    SYSCTRL_RF_ACCESS_CFG->RF_ACCESS_ALIAS = RF_ACCESS_ENABLE_BITBAND;

    /* Start the 48 MHz oscillator without changing the other register bits */
    RF->XTAL_CTRL = ((RF->XTAL_CTRL & ~XTAL_CTRL_DISABLE_OSCILLATOR) |
                     XTAL_CTRL_REG_VALUE_SEL_INTERNAL);

    /* Enable the 48 MHz oscillator divider using the desired prescale value */
    RF_REG2F->CK_DIV_1_6_CK_DIV_1_6_BYTE = CK_DIV_1_6_PRESCALE_3_BYTE;

    /* Wait until 48 MHz oscillator is started */
    while (RF_REG39->ANALOG_INFO_CLK_DIG_READY_ALIAS !=
           ANALOG_INFO_CLK_DIG_READY_BITBAND);

    /* Switch to (divided 48 MHz) oscillator clock */
    Sys_Clocks_SystemClkConfig(JTCK_PRESCALE_1   |
                               EXTCLK_PRESCALE_1 |
                               SYSCLK_CLKSRC_RFCLK);

    /* Configure clock dividers */
    CLK->DIV_CFG0 = (SLOWCLK_PRESCALE_8 | BBCLK_PRESCALE_2 |
                     USRCLK_PRESCALE_1);
    CLK->DIV_CFG2 = (CPCLK_PRESCALE_12 | DCCLK_PRESCALE_4);

    BBIF->CTRL    = (BB_CLK_ENABLE | BBCLK_DIVIDER_8 | BB_WAKEUP);

    /* Configuration of Audio Sink Clock Counters */
    Sys_Audiosink_ResetCounters();
    Sys_Audiosink_InputClock(0,
                             ((uint32_t)(SAMPL_CLK <<
                                         DIO_AUDIOSINK_SRC_CLK_Pos)));
    Sys_Audiosink_Config(AUDIO_SINK_PERIODS_16, 0, 0);

    AUDIOSINK_CTRL->PHASE_CNT_START_ALIAS  = PHASE_CNT_START_BITBAND;
    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = PERIOD_CNT_START_BITBAND;

    uint32_t i = 0;
#if (OUTPUT_DECODE_PATH)

    /* ASCC interrupts */
    NVIC_ClearPendingIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_ClearPendingIRQ(AUDIOSINK_PERIOD_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

    SYSCTRL->DSS_CTRL = DSS_LPDSP32_PAUSE;

    Sys_Flash_Copy((uint32_t)&LPDSP32_Prog_40bit_PM[0], DSP_PRAM0_BASE,
                   MEM_PM_SIZE, COPY_TO_MEM_BITBAND);
    Sys_Flash_Copy((uint32_t)&LPDSP32_Data_low_DM[0], DSP_DRAM01_BASE,
                   MEM_DMA_SIZE, COPY_TO_MEM_BITBAND);
    Sys_Flash_Copy((uint32_t)&LPDSP32_Data_low_DM[MEM_DMA_SIZE + 3],
                   DSP_DRAM4_BASE, 8000, COPY_TO_MEM_BITBAND);
    Sys_Flash_Copy((uint32_t)&LPDSP32_Data_low_DM[((MEM_DMA_SIZE + 3) + 8001)],
                   (DSP_DRAM4_BASE + 0x1F41), (MEM_DMB_SIZE - 8000),
                   COPY_TO_MEM_BITBAND);

    while (FLASH_COPY_CTRL->BUSY_ALIAS != COPY_IDLE_BITBAND)
    {
        Sys_Watchdog_Refresh();
    }

    SYSCTRL->DSS_CTRL = DSS_LPDSP32_PAUSE;
    SYSCTRL->DSS_CTRL = DSS_RESET;

    while (i < 7000)
    {
        i++;
        Sys_Watchdog_Refresh();
    }

    SYSCTRL->DSS_CTRL = DSS_LPDSP32_RESUME;

    /* Setup G722 decoder in LPDSP32 */
    uint8_t *message = MEM_MESSAGE;

    /* Encoder Frame size */
    *message = SUBFRAME_LENGTH;

    /* Encoder block size */
    *(message + 1) = SUBFRAME_LENGTH;

    /* Decoder Frame size */
    /*TODO: varibale decoder frame length is not supported yet!*/
    *(message + 2) = SUBFRAME_LENGTH;

    /* Decoder block size */
    *(message + 3) = SUBFRAME_LENGTH;

    /* Codec Mode */
    *(message + 4) = CODEC_MODE;
#endif    /* if (OUTPUT_DECODE_PATH) */

#if (OUTPUT_INTRF == SPI_TX_CODED_OUTPUT)

    /* Initialize SPI interface */
    Sys_SPI_DIOConfig(0, SPI0_SELECT_MASTER,
                      DIO_LPF_DISABLE | DIO_WEAK_PULL_UP, SPI_CLK_DO, SPI_CS_DO,
                      SPI_SER_DI, SPI_SER_DO);

    /* Configure the SPI0 interface */
    Sys_SPI_Config(0, SPI0_SELECT_MASTER | SPI0_ENABLE |
                   SPI0_CLK_POLARITY_NORMAL | SPI0_CONTROLLER_DMA |
                   SPI0_MODE_SELECT_AUTO | SPI0_PRESCALE_32);
    Sys_SPI_TransferConfig(0, SPI0_START | SPI0_WRITE_DATA | SPI0_CS_1 |
                           SPI0_WORD_SIZE_8);

    NVIC_ClearPendingIRQ(DMA_IRQn(TX_DMA_NUM));
    NVIC_SetPriority(DMA_IRQn(TX_DMA_NUM), 2);
    NVIC_EnableIRQ(DMA_IRQn(TX_DMA_NUM));
#endif    /* if (OUTPUT_INTRF == SPI_TX_CODED_OUTPUT) */

#if (OUTPUT_DECODE_PATH)
    NVIC_SetPriority(DSP1_IRQn, 4);
    NVIC_EnableIRQ(DSP1_IRQn);

    NVIC_SetPriority(TIMER_IRQn(TIMER_REGUL), 4);

    NVIC_SetPriority(AUDIOSINK_PERIOD_IRQn, 4);
    NVIC_SetPriority(AUDIOSINK_PHASE_IRQn, 4);

    Sys_DMA_ChannelConfig(
        ASRC_IN_IDX,
        RX_DMA_ASRC_IN,
        SUBFRAME_LENGTH,
        0,
        (uint32_t)Dsp2CmBuff0dec,
        (uint32_t)&ASRC->IN
        );

#if (OUTPUT_INTRF == SPI_TX_RAW_OUTPUT)
    /* Initialize SPI interface */
    Sys_SPI_DIOConfig(0, SPI0_SELECT_MASTER,
                      DIO_LPF_DISABLE | DIO_WEAK_PULL_UP, SPI_CLK_DO, SPI_CS_DO,
                      SPI_SER_DI, SPI_SER_DO);

    /* Configure the SPI0 interface */
    Sys_SPI_Config(0, SPI0_SELECT_MASTER | SPI0_ENABLE |
                   SPI0_CLK_POLARITY_NORMAL | SPI0_CONTROLLER_DMA |
                   SPI0_MODE_SELECT_AUTO | SPI0_PRESCALE_32);
    Sys_SPI_TransferConfig(0, SPI0_START | SPI0_WRITE_DATA | SPI0_CS_1 |
                           SPI0_WORD_SIZE_16);

    Sys_DMA_ChannelConfig(
        ASRC_OUT_IDX,
        RX_DMA_ASRC_OUT,
        1,
        0,
        (uint32_t)&ASRC->OUT,
        (uint32_t)&SPI0->TX_DATA
        );
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);
#elif (OUTPUT_INTRF == OD_OUTPUT)
    /* OD 直驱受话器 sink（照抄 peripheral_server_sleep Audio_Init 末尾） */
    Sys_Clocks_SystemClkPrescale1(AUDIOCLK_PRESCALE_5);
    Sys_Audio_Set_Config(AUDIO_CONFIG);
    AUDIO->OD_CFG = (DCRM_CUTOFF_240HZ | DITHER_ENABLE);
    AUDIO->SDM_CFG = 0x00002;
    AUDIO->OD_GAIN = 0xfff;
    Sys_DIO_Config(OD_P_DIO, DIO_6X_DRIVE | DIO_LPF_DISABLE |
                   DIO_NO_PULL | DIO_MODE_OD_P);

    /* DMA ch5: BufferOut → OD_DATA */
    Sys_DMA_ChannelDisable(OD_DMA_NUM);
    Sys_DMA_ChannelConfig(OD_DMA_NUM, RX_DMA_OD, 16, 0,
                          (uint32_t)BufferOut, (uint32_t)&(AUDIO->OD_DATA));
    DMA_CTRL1[OD_DMA_NUM].TRANSFER_LENGTH_SHORT = 2 * FRAME_LENGTH;

    /* DMA ch4: ASRC OUT → BufferOut */
    Sys_DMA_ChannelDisable(ASRC_OUT_IDX);
    Sys_DMA_ChannelConfig(ASRC_OUT_IDX, OD_RX_DMA_ASRC_OUT,
                          2 * FRAME_LENGTH, 0,
                          (uint32_t)&ASRC->OUT, (uint32_t)BufferOut);
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);
#endif    /* sink: SPI_TX_RAW_OUTPUT(SPI0) vs OD_OUTPUT(OD) */

#if (SIMUL == 1)
    Sys_Timer_Set_Control(TIMER_SIMUL, TIMER_FREE_RUN | (10000 - 1) |
                          TIMER_SLOWCLK_DIV2);
    Sys_Timers_Start(1 << TIMER_SIMUL);
    SYSCTRL->RF_ACCESS_CFG = RF_ACCESS_ENABLE_BITBAND | RF_IRQ_ACCESS_ENABLE;
    NVIC_EnableIRQ(TIMER_IRQn(TIMER_SIMUL));

    Sys_ASRC_Reset();

    NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

    /* DMA interrupts */
    NVIC_EnableIRQ(DMA_IRQn(ASRC_IN_IDX));

    /* LPDSP32 interrupt */
    NVIC_EnableIRQ(DSP1_IRQn);

    /* Timer interrupts */
    NVIC_EnableIRQ(TIMER_IRQn(TIMER_REGUL));
#endif    /* if (SIMUL == 1) */
#endif    /* if (OUTPUT_DECODE_PATH) */

    /* Delay added to handle reset sequencing */
    Sys_GPIO_Set_High(DIO_SYNC_PULSE);
    Sys_Watchdog_Refresh();

    for (i = 0; i < 10000; i++)
    {
        Sys_Watchdog_Refresh();
        Sys_Delay_ProgramROM(1000);
    }

    /* Initialize the baseband and BLE stack */
    BLE_Initialize();

    /* Initialize environment */
    App_Env_Initialize();
    printf_init();
    /* 打印口改到 DIO12（pack printf.c 内硬编码 TX=DIO5，此处覆写并释放 DIO5） */
    Sys_UART_DIOConfig(DIO_6X_DRIVE | DIO_WEAK_PULL_UP | DIO_LPF_ENABLE,
                       PRINT_TX_DIO, PRINT_RX_DIO);
    Sys_DIO_Config(5, DIO_MODE_DISABLE);
#if (SIMUL != 1)
    APP_RM_Init(ear_side);
#endif    /* if (SIMUL != 1) */

    RF_SwitchToCPMode();
    RM_Enable(1000);

    Sys_DIO_Config(DEBUG_DIO_FIRST, DIO_MODE_GPIO_OUT_0);
    Sys_DIO_Config(DEBUG_DIO_SECOND, DIO_MODE_GPIO_OUT_0);
    Sys_DIO_Config(DIO_SYNC_PULSE, DIO_MODE_GPIO_OUT_0);
    Sys_GPIO_Set_Low(DEBUG_DIO_FIRST);

    /* Enable 6dBM or 0dBM mode*/
#if (OUTPUT_POWER_6DBM)
    Sys_RFFE_SetTXPower(6);
#else    /* 0DBM */
    Sys_RFFE_SetTXPower(0);
#endif    /* CFG_6DBM */

    /* Enable Flash overlay */
    memcpy((uint8_t *)PRAM0_BASE, (uint8_t *)FLASH_MAIN_BASE, PRAM0_SIZE);
    memcpy((uint8_t *)PRAM1_BASE, (uint8_t *)(FLASH_MAIN_BASE + PRAM0_SIZE),
           PRAM1_SIZE);
    memcpy((uint8_t *)PRAM2_BASE, (uint8_t *)(FLASH_MAIN_BASE + PRAM0_SIZE +
                                              PRAM1_SIZE), PRAM2_SIZE);
    memcpy((uint8_t *)PRAM3_BASE, (uint8_t *)(FLASH_MAIN_BASE + PRAM0_SIZE +
                                              PRAM1_SIZE + PRAM2_SIZE),
           PRAM3_SIZE);

    SYSCTRL->FLASH_OVERLAY_CFG  = 0xf;

    /* Enable CM3 loop cache */
    SYSCTRL->CSS_LOOP_CACHE_CFG = CSS_LOOP_CACHE_ENABLE;

#if (DEBUG_UART_LOG)
    UartLogInit();
#endif    /* if (DEBUG_UART_LOG) */

    __set_PRIMASK(PRIMASK_ENABLE_INTERRUPTS);
    __set_FAULTMASK(FAULTMASK_ENABLE_INTERRUPTS);
}

/* ----------------------------------------------------------------------------
 * Function      : void App_Env_Initialize(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize application environment
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void App_Env_Initialize(void)
{
    /* Reset the application manager environment */
    memset(&app_env, 0, sizeof(app_env));

    /* Create the application task handler */
    ke_task_create(TASK_APP, &TASK_DESC_APP);

    /* Initialize the custom service environment */
    CustomService_Env_Initialize();

    /* Initialize the Rempro service environment */
    RemproService_Env_Initialize();

    /* Initialize the battery service server environment */
    Bass_Env_Initialize();
}
