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
 * $Revision: 1.13 $
 * $Date: 2018/03/26 21:33:54 $
 * ------------------------------------------------------------------------- */

#include "app.h"

#if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT)
#include "dsp_pm_dm_enc.h"
#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT) */

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

    /* Disable operation of DIO 13-15 in JTAG mode */
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
#if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT)

    /* ASCC interrupts */
    NVIC_ClearPendingIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_ClearPendingIRQ(AUDIOSINK_PERIOD_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

    SYSCTRL->DSS_CTRL = DSS_LPDSP32_PAUSE;

#if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT)
    Sys_Flash_Copy((uint32_t)&LPDSP32_Prog_40bit_PM_enc[0], DSP_PRAM0_BASE,
                   MEM_PM_SIZE, COPY_TO_MEM_BITBAND);
    Sys_Flash_Copy((uint32_t)&LPDSP32_Data_low_DM_enc[0], DSP_DRAM01_BASE,
                   MEM_DMA_SIZE, COPY_TO_MEM_BITBAND);
    Sys_Flash_Copy((uint32_t)&LPDSP32_Data_low_DM_enc[MEM_DMA_SIZE + 3],
                   DSP_DRAM4_BASE, 8000, COPY_TO_MEM_BITBAND);
    Sys_Flash_Copy((uint32_t)&LPDSP32_Data_low_DM_enc[((MEM_DMA_SIZE + 3) +
                                                       8001)],
                   (DSP_DRAM4_BASE + 0x1F41), (MEM_DMB_SIZE - 8000),
                   COPY_TO_MEM_BITBAND);
#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT) */
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
#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT) */

/*-------------------------  Ezairo side ------------------ */
#if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT)
    NVIC_SetPriority(DSP0_IRQn, 0);
    NVIC_EnableIRQ(DSP0_IRQn);
    NVIC_SetPriority(DSP1_IRQn, 0);
    NVIC_EnableIRQ(DSP1_IRQn);

    Sys_DMA_ChannelConfig(
        ASRC_IN_IDX,
        TX_DMA_ASRC_IN,
        SUBFRAME_LENGTH,
        0,
        (uint32_t)&asrc_in_buf[0],
        (uint32_t)&ASRC->IN
        );
    Sys_DMA_ClearChannelStatus(ASRC_IN_IDX);

    /* Enable DMA for ASRC output */
    Sys_DMA_ChannelConfig(
        ASRC_OUT_IDX,
        TX_DMA_ASRC_OUT,
        1,
        0,
        (uint32_t)&ASRC->OUT,
        (uint32_t)&data_fifo_rec
        );
    Sys_ASRC_Config(0x0, LOW_DELAY | ASRC_DEC_MODE1);

    Sys_DMA_ClearChannelStatus(ASRC_OUT_IDX);
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);

    NVIC_ClearPendingIRQ(DMA_IRQn(ASRC_OUT_IDX));
    NVIC_SetPriority(DMA_IRQn(ASRC_OUT_IDX), 2);
    NVIC_EnableIRQ(DMA_IRQn(ASRC_OUT_IDX));

    NVIC_ClearPendingIRQ(DMA_IRQn(ASRC_IN_IDX));
    NVIC_SetPriority(DMA_IRQn(ASRC_IN_IDX), 2);
    NVIC_EnableIRQ(DMA_IRQn(ASRC_IN_IDX));

#if (INPUT_INTRF == SPI_RX_RAW_INPUT)

    /* Initialize SPI interface */
    Sys_SPI_DIOConfig(0, SPI0_SELECT_SLAVE,
                      DIO_LPF_DISABLE | DIO_WEAK_PULL_UP, SPI_CLK_DO, SPI_CS_DO,
                      SPI_SER_DI, SPI_SER_DO);

    /* Configure the SPI0 interface */
    Sys_SPI_Config(0, SPI0_SELECT_SLAVE | SPI0_ENABLE |
                   SPI0_CLK_POLARITY_NORMAL | SPI0_CONTROLLER_DMA |
                   SPI0_MODE_SELECT_AUTO | SPI0_PRESCALE_16);
    Sys_SPI_TransferConfig(0, SPI0_START | SPI0_RW_DATA | SPI0_CS_1 |
                           SPI0_WORD_SIZE_16);

    Sys_DMA_ChannelConfig(
        RX_DMA_NUM,
        DMA_RX_CONFIG,
        SUBFRAME_LENGTH * 2,
        0,
        (uint32_t)&(SPI0->RX_DATA),
        (uint32_t)&spi_buf[0]);

    while (SPI0_CTRL1->SPI0_CS_ALIAS == SPI0_CS_0_BITBAND);
    while (SPI0_CTRL1->SPI0_CS_ALIAS == SPI0_CS_1_BITBAND);
#else    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT) */

    /* Initialize and Configure PCM interface */
    Sys_PCM_ConfigClk(PCM_SELECT_SLAVE, DIO_WEAK_PULL_UP, PCM_CLK_DO,
                      PCM_FRAME_SYNC,
                      PCM_SER_DI, PCM_SER_DO, DIO_MODE_INPUT);
    Sys_PCM_Config(PCM_CFG_RX);
    Sys_PCM_Enable();

    /* Setup DMA channel for PCM data transfer */
    /* Using counter interrupt to setup dual buffer system to prevent read and
     * write access conflicts that result in audio artifacts */
    Sys_DMA_ChannelConfig(
        RX_DMA_NUM,
        DMA_RX_CONFIG,
        4 * SUBFRAME_LENGTH,    /* because of left/right channel */
        2 * SUBFRAME_LENGTH,
        (uint32_t)&(PCM->RX_DATA),
        (uint32_t)&pcm_buf[0]);

#if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD)

    /* Configure I2C as Master */
    Sys_I2C_Config(I2C_MASTER_SPEED_120  | I2C_CONTROLLER_CM3   |
                   I2C_STOP_INT_ENABLE   | I2C_AUTO_ACK_DISABLE |
                   I2C_SAMPLE_CLK_ENABLE | I2C_SLAVE_DISABLE);

    /* Configure the DIOs for I2C, strong pull-up used to drive the line,
     * if external pull-up is used the DIO_NO_PULL can be used */
    Sys_I2C_DIOConfig(DIO_6X_DRIVE | DIO_LPF_ENABLE | DIO_STRONG_PULL_UP,
                      I2C_SCL_DIO,
                      I2C_SDA_DIO);

    /* Enable interrupts */
    NVIC_EnableIRQ(I2C_IRQn);
#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD) */
#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT) */
#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT) */

#if (INPUT_INTRF == SPI_RX_CODED_INPUT)

    /* Initialize SPI interface */
    Sys_SPI_DIOConfig(0, SPI0_SELECT_SLAVE,
                      DIO_LPF_DISABLE | DIO_WEAK_PULL_UP, SPI_CLK_DO, SPI_CS_DO,
                      SPI_SER_DI,
                      SPI_SER_DO);

    /* Configure the SPI0 interface */
    Sys_SPI_Config(0, SPI0_SELECT_SLAVE | SPI0_ENABLE |
                   SPI0_CLK_POLARITY_NORMAL | SPI0_CONTROLLER_DMA |
                   SPI0_MODE_SELECT_AUTO | SPI0_PRESCALE_16);
    Sys_SPI_TransferConfig(0, SPI0_START | SPI0_RW_DATA | SPI0_CS_1 |
                           SPI0_WORD_SIZE_8);

    Sys_DMA_ChannelConfig(
        RX_DMA_NUM,
        DMA_RX_CONFIG,
        AUDIO_FRAME_SIZE * 2,
        0,
        (uint32_t)&(SPI0->RX_DATA),
        (uint32_t)&spi_buf[0]);
#endif    /* if (INPUT_INTRF == SPI_RX_CODED_INPUT) */

    Sys_DMA_ClearChannelStatus(RX_DMA_NUM);
    Sys_DMA_ChannelEnable(RX_DMA_NUM);

    NVIC_ClearPendingIRQ(DMA_IRQn(RX_DMA_NUM));
    NVIC_SetPriority(DMA_IRQn(RX_DMA_NUM), 2);
    NVIC_EnableIRQ(DMA_IRQn(RX_DMA_NUM));

#if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT)
    Sys_DMA_ChannelConfig(
        MEMCPY_RESTORE_STATE_MEM,
        DMA_RESTORE_STATE_MEM_CONFIG,
        20,
        0,
        (uint32_t)&asrc_state_mem_rx[0][0],
        (uint32_t)&ASRC->PHASE_INC);
    Sys_DMA_ClearChannelStatus(MEMCPY_RESTORE_STATE_MEM);

    NVIC_ClearPendingIRQ(DMA_IRQn(MEMCPY_RESTORE_STATE_MEM));
    NVIC_SetPriority(DMA_IRQn(MEMCPY_RESTORE_STATE_MEM), 2);
    NVIC_EnableIRQ(DMA_IRQn(MEMCPY_RESTORE_STATE_MEM));

    Sys_DMA_ChannelConfig(
        MEMCPY_SAVE_STATE_MEM,
        DMA_SAVE_STATE_MEM_CONFIG,
        20,
        0,
        (uint32_t)&ASRC->PHASE_INC,
        (uint32_t)&asrc_state_mem_rx[0][0]);
    Sys_DMA_ChannelEnable(MEMCPY_SAVE_STATE_MEM);
    Sys_DMA_ClearChannelStatus(MEMCPY_SAVE_STATE_MEM);

    NVIC_ClearPendingIRQ(DMA_IRQn(MEMCPY_SAVE_STATE_MEM));
    NVIC_SetPriority(DMA_IRQn(MEMCPY_SAVE_STATE_MEM), 1);
    NVIC_EnableIRQ(DMA_IRQn(MEMCPY_SAVE_STATE_MEM));
#endif    /* if (INPUT_INTRF == SPI_RX_RAW_INPUT || INPUT_INTRF == PCM_RX_RAW_INPUT) */

    /*-------------- resolve reset sequence issue */
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

#if (SIMUL != 1)
    APP_RM_Init(ear_side);
#endif    /* if (SIMUL != 1) */

    RF_SwitchToBLEMode();

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

/* */
    SYSCTRL->FLASH_OVERLAY_CFG  = 0xf;

    /* Enable CM3 loop cache */
    SYSCTRL->CSS_LOOP_CACHE_CFG = CSS_LOOP_CACHE_ENABLE;

    Sys_DIO_Config(LED_DIO_NUM, DIO_MODE_GPIO_OUT_0);

    Sys_DIO_Config(BUTTON_DIO, DIO_MODE_GPIO_IN_0 | DIO_WEAK_PULL_UP |
                   DIO_LPF_DISABLE);
    Sys_DIO_IntConfig(0, DIO_EVENT_TRANSITION | DIO_SRC(BUTTON_DIO) |
                      DIO_DEBOUNCE_ENABLE,
                      DIO_DEBOUNCE_SLOWCLK_DIV1024, 49);

#if (DEBUG_UART_LOG)
    UartLogInit();
#endif    /* if (DEBUG_UART_LOG) */

    NVIC_EnableIRQ(DIO0_IRQn);

    __set_PRIMASK(PRIMASK_ENABLE_INTERRUPTS);
    __set_FAULTMASK(FAULTMASK_ENABLE_INTERRUPTS);

#if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD)

    /* Setup the audio codec shield */
    for (i = 0; i < WM8731_I2C_MESSAGE_BUFFER_SIZE; i++)
    {
        while ((Sys_I2C_Get_Status() & (1 << I2C_STATUS_BUS_FREE_Pos)) !=
               I2C_BUS_FREE)
        {
            Sys_Watchdog_Refresh();
        }

        i2c_tx_buffer_index   = 0;
        i2c_tx_buffer_data[0] = wm8731_i2c_message_buffer[i].register_address;
        i2c_tx_buffer_data[1] = wm8731_i2c_message_buffer[i].register_data;
        Sys_I2C_StartWrite(WM8731_I2C_SLAVE_ADDRESS);
    }
#endif    /* if (INPUT_INTRF == PCM_RX_RAW_INPUT && PCM_RX_RAW_SOURCE == AUDIO_CODEC_SHIELD) */
}

/* ----------------------------------------------------------------------------
 * Function      : void DIO0_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : Toggle selection for left or right channel
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DIO0_IRQHandler(void)
{
    static uint8_t ignore_next_dio_int = 0;
    if (ignore_next_dio_int)
    {
        ignore_next_dio_int = 0;
    }
    else if (DIO_DATA->ALIAS[BUTTON_DIO] == 0)
    {
        /* Button is pressed: Ignore next interrupt.
         * This is required to deal with the debounce circuit limitations. */
        ignore_next_dio_int = 1;

        ear_side = !ear_side;
#if (SIMUL != 1)
        APP_RM_Init(ear_side);
#endif    /* if (SIMUL != 1) */
    }
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

    /* Initialize the battery service server environment */
    Basc_Env_Initialize();
}
