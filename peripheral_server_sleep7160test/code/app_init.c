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
 * $Revision: 1.70 $
 * $Date: 2019/08/27 15:35:04 $
 * ------------------------------------------------------------------------- */

#include "app.h"

/* Application Environment Structure */
struct app_env_tag app_env;

/* Sleep Mode Environment Structure */
struct sleep_mode_env_tag sleep_mode_env;

/* Low power clock related parameters */
struct low_power_clk_param_tag low_power_clk_param;
uint32_t i = 0;
void App_sleep_Initialize(void)
{
    /* Customized parameters for the LLD SLEEP module
     * respect to OSC wake-up timings in us */
    struct lld_sleep_params_t desired_lld_sleep_params;

    /* Mask all interrupts */
    __set_PRIMASK(PRIMASK_DISABLE_INTERRUPTS);

    /* Disable all interrupts and clear any pending interrupts */
    Sys_NVIC_DisableAllInt();
    Sys_NVIC_ClearAllPendingInt();

    /* Test DIO12 to pause the program to make it easy to re-flash */
    DIO->CFG[RECOVERY_DIO] = DIO_MODE_INPUT  | DIO_WEAK_PULL_UP |
                             DIO_LPF_DISABLE | DIO_6X_DRIVE;
    while (DIO_DATA->ALIAS[RECOVERY_DIO] == 0);

    /* Calibrate the board
     * The supplemental calibrated values are loaded by the user-defined
     * initialization function during the system boot process for supplemental mode.*/
#if (CALIB_RECORD == MANU_CALIB)
    if (Load_Trim_Values_And_Calibrate_MANU_CALIB() !=
        VOLTAGES_CALIB_NO_ERROR)
    {
        /* Hold here to notify error(s) in voltage calibrations */
        while (true)
        {
            Sys_Watchdog_Refresh();
        }
    }
#elif (CALIB_RECORD == USER_CALIB)
    if (Calculate_Trim_Values_And_Calibrate() !=
        VOLTAGES_CALIB_NO_ERROR)
    {
        /* Hold here to notify error(s) in voltage calibrations */
        while (true)
        {
            Sys_Watchdog_Refresh();
        }
    }
#endif    /* CALIB_RECORD */

    /* Configure the current trim settings for VCC, VDDA */
    ACS_VCC_CTRL->ICH_TRIM_BYTE = VCC_ICHTRIM_80MA_BYTE;

    /* Start 48 MHz XTAL oscillator */
    ACS_VDDRF_CTRL->ENABLE_ALIAS = VDDRF_ENABLE_BITBAND;
    ACS_VDDRF_CTRL->CLAMP_ALIAS = VDDRF_DISABLE_HIZ_BITBAND;

    /* Wait until VDDRF supply has powered up */
    while (ACS_VDDRF_CTRL->READY_ALIAS != VDDRF_READY_BITBAND);

#if (CALIB_RECORD == USER_CALIB)

    /* Configure VDDPA */
#ifdef POWER_AMPLIFIER_ON

    /* Configure the current trim settings for VDDA */
    ACS_VDDA_CP_CTRL->PTRIM_BYTE = VDDA_PTRIM_16MA_BYTE;

    /* Enable power amplifier */
    ACS_VDDPA_CTRL->ENABLE_ALIAS = VDDPA_ENABLE_BITBAND;
    ACS_VDDPA_CTRL->VDDPA_SW_CTRL_ALIAS = VDDPA_SW_HIZ_BITBAND;
#else    /* ifdef POWER_AMPLIFIER_ON */

    /* Disable power amplifier */
    ACS_VDDPA_CTRL->ENABLE_ALIAS = VDDPA_DISABLE_BITBAND;
    ACS_VDDPA_CTRL->VDDPA_SW_CTRL_ALIAS = VDDPA_SW_VDDRF_BITBAND;
#endif    /* POWER_AMPLIFIER_ON */
#endif    /* (CALIB_RECORD == USER_CALIB) */

    /* Enable/disable buck converter */
    ACS_VCC_CTRL->BUCK_ENABLE_ALIAS = VCC_BUCK_LDO_CTRL;

    /* Enable RF power switches */
    SYSCTRL_RF_POWER_CFG->RF_POWER_ALIAS = RF_POWER_ENABLE_BITBAND;

    /* Remove RF isolation */
    SYSCTRL_RF_ACCESS_CFG->RF_ACCESS_ALIAS = RF_ACCESS_ENABLE_BITBAND;

    /* Start the 48 MHz oscillator without changing the other register bits */
    RF->XTAL_CTRL = ((RF->XTAL_CTRL & ~XTAL_CTRL_DISABLE_OSCILLATOR) |
                     XTAL_CTRL_REG_VALUE_SEL_INTERNAL);

    /* Enable the 48 MHz oscillator divider using the desired prescale value */
    RF_REG2F->CK_DIV_1_6_CK_DIV_1_6_BYTE = RF_CK_DIV_PRESCALE_VALUE;

    /* Wait until 48 MHz oscillator is started */
    while (RF_REG39->ANALOG_INFO_CLK_DIG_READY_ALIAS !=
           ANALOG_INFO_CLK_DIG_READY_BITBAND);

    /* Switch to (divided 48 MHz) oscillator clock */
    Sys_Clocks_SystemClkConfig(JTCK_PRESCALE_1   |
                               EXTCLK_PRESCALE_1 |
                               SYSCLK_CLKSRC_RFCLK);

    /* Configure clock dividers */
    CLK->DIV_CFG0 = SLOWCLK_PRESCALE_VALUE | BBCLK_PRESCALE_VALUE |
                    USRCLK_PRESCALE_1;
    CLK_DIV_CFG2->DCCLK_BYTE = DCCLK_BYTE_VALUE;

    /* - The baseband clock (master1) is a scaled down version of SYSCLK that
    *   can be configured by setting the field BBCLK_PRESCALE of the
    *   CLK_DIV_CFG0 control register: 8 MHz or 12 MHz
    * - The internal baseband controller clock divider must be set according
    *   to the baseband clock frequency in order to generate a 1 MHz clock */
    BBIF->CTRL = BB_CLK_ENABLE | BBCLK_DIVIDER_VALUE | BB_DEEP_SLEEP;

    /* Seed the random number generator */
    srand(1);

    /* Customized parameters for the LLD SLEEP module
     * respect to OSC wake-up timings in us */
    if (RTC_CLK_SRC == RTC_CLK_SRC_RC_OSC)
    {
        desired_lld_sleep_params.twosc = TWOSC_RC_OSC;
    }
    else
    {
        desired_lld_sleep_params.twosc = TWOSC;
    }
    BLE_LLD_Sleep_Params_Set(desired_lld_sleep_params);

    /* Initialize the baseband and BLE stack */
    BLE_Initialize();

#if ((CALIB_RECORD == SUPPLEMENTAL_CALIB) || (CALIB_RECORD == MANU_CALIB))

    /* Set radio output power of RF
     * Note - This function configures ADC channel 0 and disables it after use. */
    int setTXPowerStatus = Sys_RFFE_SetTXPower(Load_Tx_Power_Value());

    /* To demonstrate low power consumption, VCC_TARGET is set to 1.10V in calibration.h.
     * The optimal VCC_TARGET to achieve 0dBm in Sys_RFFE_SetTXPower() is 1.12V. So, the
     * function will return the status ERRNO_RFFE_INSUFFICIENTVCC_ERROR indicating the
     * VCC_TARGET may not be enough to reach 0dBm. So, we ignore this type of error here. */
    if(setTXPowerStatus != ERRNO_NO_ERROR &&
       setTXPowerStatus != ERRNO_RFFE_INSUFFICIENTVCC_ERROR)
    {
    	while(1); /* Wait for watchdog reset! */
    }

#else    /* if ((CALIB_RECORD == SUPPLEMENTAL_CALIB) || (CALIB_RECORD == MANU_CALIB)) */

    /* Initialize PA_PWR trim value for both memory banks */
    uint8_t bank_select_backup = RF_REG05->BANK_BYTE;
    RF_REG05->BANK_BYTE = 0;
    RF_REG19->PA_PWR_BYTE = PA_PWR_TRIM_VAL;
    RF_REG05->BANK_BYTE = 1;
    RF_REG19->PA_PWR_BYTE = PA_PWR_TRIM_VAL;
    RF_REG05->BANK_BYTE = bank_select_backup;
#endif    /* if ((CALIB_RECORD == SUPPLEMENTAL_CALIB) || (CALIB_RECORD == MANU_CALIB)) */

    /* Trim RC oscillator to 3 MHz (required by Sys_PowerModes_Wakeup) */
    Sys_Clocks_OscRCCalibratedConfig(3000);

    /* Configure the sleep mode parameters and configurations */
    Sleep_Mode_Configure(&sleep_mode_env);

    /* BLE not in sleep mode and ready for normal operations */
    BLE_Is_Awake_Flag_Set();

#ifdef BAT_ADC_ENABLE
    /* Configure ADC channel 0 for DIO3 battery measurement */
    Sys_DIO_Config(BAT_ADC_DIO, DIO_MODE_GPIO_IN_0 | DIO_NO_PULL | DIO_LPF_DISABLE);
    Sys_ADC_Set_Config(ADC_NORMAL | ADC_PRESCALE_200);
    Sys_ADC_InputSelectConfig(BAT_ADC_CHANNEL, (ADC_NEG_INPUT_GND | ADC_POS_INPUT_DIO3));
#endif

    /* Initialize environment */
    App_Env_Initialize();

    if (RTC_CLK_SRC != RTC_CLK_SRC_XTAL32K)
    {
        low_power_clk_param.dynamic_measurement_enable = false;
        low_power_clk_param.low_power_enable = false;
    }

#ifdef VOLTAGES_CALIB_VERIFY

    /* Hold here to verify calibrated voltages */
    while (true)
    {
        Sys_Watchdog_Refresh();
    }
#endif    /* ifdef VOLTAGES_CALIB_VERIFY */

    /* Stop masking interrupts */
    __set_PRIMASK(PRIMASK_ENABLE_INTERRUPTS);
    __set_FAULTMASK(FAULTMASK_ENABLE_INTERRUPTS);
}
void App_RM_BLE_Initialize(void)
{
    /* Mask all interrupts */
    __set_PRIMASK(PRIMASK_DISABLE_INTERRUPTS);

    /* Disable all interrupts and clear any pending interrupts */
    Sys_NVIC_DisableAllInt();
    Sys_NVIC_ClearAllPendingInt();

    DIO_JTAG_SW_PAD_CFG->CM3_JTAG_DATA_EN_ALIAS =
        CM3_JTAG_DATA_DISABLED_BITBAND;
    DIO_JTAG_SW_PAD_CFG->CM3_JTAG_TRST_EN_ALIAS =
        CM3_JTAG_TRST_DISABLED_BITBAND;

    /* Test DIO12 to pause the program to make it easy to re-flash */
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
    ACS_VDDPA_CTRL->VDDPA_SW_CTRL_ALIAS = VDDPA_SW_VDDRF_BITBAND;

    /* Enable RF power switches */
    SYSCTRL_RF_POWER_CFG->RF_POWER_ALIAS = RF_POWER_ENABLE_BITBAND;

    /* Remove RF isolation */
    SYSCTRL_RF_ACCESS_CFG->RF_ACCESS_ALIAS = RF_ACCESS_ENABLE_BITBAND;

    /* Start the 48 MHz oscillator without changing the other register bits */
    RF->XTAL_CTRL = ((RF->XTAL_CTRL & ~XTAL_CTRL_DISABLE_OSCILLATOR) |
                     XTAL_CTRL_REG_VALUE_SEL_INTERNAL);

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
    CLK->DIV_CFG2 = (CPCLK_PRESCALE_8 | DCCLK_PRESCALE_4);

    BBIF->CTRL = (BB_CLK_ENABLE | BBCLK_DIVIDER_8 | BB_DEEP_SLEEP);

#ifdef BAT_ADC_ENABLE
    /* Configure ADC channel 0 for DIO3 battery measurement */
    Sys_DIO_Config(BAT_ADC_DIO, DIO_MODE_GPIO_IN_0 | DIO_NO_PULL | DIO_LPF_DISABLE);
    Sys_ADC_Set_Config(ADC_NORMAL | ADC_PRESCALE_6400);
    Sys_ADC_InputSelectConfig(BAT_ADC_CHANNEL,
                              (ADC_NEG_INPUT_GND |
                               ADC_POS_INPUT_DIO3));
#endif

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
#ifdef APP_RM_ENABLE
#if (SIMUL != 1)
    APP_RM_Init(0);
#endif
    RF_SwitchToCPMode();
    RM_Enable(500);
    app_env.audio_streaming = 1;
#endif

    Sys_RFFE_SetTXPower(0);

    /* Enable CM3 loop cache */
    SYSCTRL->CSS_LOOP_CACHE_CFG = CSS_LOOP_CACHE_ENABLE;

    __set_PRIMASK(PRIMASK_ENABLE_INTERRUPTS);
    __set_FAULTMASK(FAULTMASK_ENABLE_INTERRUPTS);
}

/* ----------------------------------------------------------------------------
 * Function      : void App_Initialize(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize the system for proper application execution
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int16_t BufferOut[2 * FRAME_LENGTH];

/* PCM output buffer (double). ch4 fills one buffer while ch5 streams the
   other, swapping on complete. */
uint32_t pcm_tx_buf[2][PCM_FRAME_WORDS];

/* ----------------------------------------------------------------------------
 * Function      : void Initialize_Raw_PCM_Output_Type(void)
 * ----------------------------------------------------------------------------
 * Description   : Configure the PCM interface as slave for raw audio output.
 *                 7100 提供 BCLK/FS，RSL10 从机移位输出 SERO(DIO14)。
 * ------------------------------------------------------------------------- */
#ifndef OD_DIO12_OUTPUT
void Initialize_Raw_PCM_Output_Type(void)
{
    /* Slave mode: external 7100 provides BCLK (DIO2) and FS (DIO3); RSL10
       shifts data out on SERO (DIO14). PCM stays disabled here; it is
       enabled last, after the LIN DMA is armed. */
    Sys_PCM_ConfigClk(PCM_SELECT_SLAVE, DIO_WEAK_PULL_UP, PCM_CLK_DO,
                      PCM_FRAME_SYNC, PCM_SER_DI, PCM_SER_DO, DIO_MODE_INPUT);
    Sys_PCM_Config(PCM_CFG_TX);

    /* LIN DMA: pcm_tx_buf -> PCM->TX_DATA, re-armed by the complete
       interrupt (documented config; CIRC underruns at the wrap boundary). */
    Sys_DMA_ChannelConfig(PCM_DMA_NUM, RX_DMA_PCM_STEREO, PCM_FRAME_WORDS, 0,
                          (uint32_t)&pcm_tx_buf[0][0], (uint32_t)&PCM->TX_DATA);
}
#endif    /* ifndef OD_DIO12_OUTPUT */

#ifdef PCM_TONE_TEST
/* 24k 采样 8kHz ±6000 16-bit 正弦，周期 3 采样（24 项 = {0,5196,-5196} 重复 8 次）。
   每 32-bit 字低16(word1/右)=一个采样（7100 读低 16），高16=0。
   两缓冲共 240 字 = 240 采样，跨缓冲连续。
   发现（2026-09-01）：PCM WORD_SIZE_16 + MULTIWORD_2 → 每帧 2×16-bit = 有效 24k，
   7100 按 24k 读，8k 表直接播 8k（不是 4k）。 */
static const int16_t pcm_sine24k_8k[24] = {
      0,  5196, -5196,     0,  5196, -5196,
      0,  5196, -5196,     0,  5196, -5196,
      0,  5196, -5196,     0,  5196, -5196,
      0,  5196, -5196,     0,  5196, -5196
};
static void Pcm_Sine24k_Fill(void)
{
    uint32_t k;
    for (k = 0; k < 2 * PCM_FRAME_WORDS; k++)
    {
        int16_t s = pcm_sine24k_8k[k % 24];
        pcm_tx_buf[k / PCM_FRAME_WORDS][k % PCM_FRAME_WORDS] =
            (uint32_t)(uint16_t)s;
    }
}
#endif    /* ifdef PCM_TONE_TEST */

/* Audio pipeline init — called before RM_Enable in BLE+switch flow */
void Audio_Init(void)
{
    ACS_VCC_CTRL->ICH_TRIM_BYTE  = VCC_ICHTRIM_16MA_BYTE;
    ACS_VDDA_CP_CTRL->PTRIM_BYTE = VDDA_PTRIM_16MA_BYTE;
    BBIF->CTRL = BB_CLK_ENABLE | BBCLK_DIVIDER_8 | BB_WAKEUP;

    SYSCTRL->DSS_CTRL = DSS_LPDSP32_PAUSE;
    SYSCTRL->DSS_CTRL = DSS_RESET;
    {
        uint32_t d;
        for (d = 0; d < 7000; d++) { Sys_Watchdog_Refresh(); }
    }
    {
        uint8_t *m = MEM_MESSAGE;
        m[0] = SUBFRAME_LENGTH;
        m[1] = SUBFRAME_LENGTH;
        m[2] = SUBFRAME_LENGTH;
        m[3] = SUBFRAME_LENGTH;
        m[4] = CODEC_MODE;
    }
    SYSCTRL->DSS_CTRL = DSS_LPDSP32_RESUME;

    Sys_Audiosink_ResetCounters();
    Sys_Audiosink_InputClock(0, SAMPLING_CLK_SRC);
    Sys_Audiosink_Config(AUDIO_SINK_PERIODS_16, 0, 0);
    AUDIOSINK_CTRL->PHASE_CNT_START_ALIAS  = PHASE_CNT_START_BITBAND;
    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = PERIOD_CNT_START_BITBAND;

    NVIC_ClearPendingIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_ClearPendingIRQ(AUDIOSINK_PERIOD_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

    Sys_DMA_ChannelDisable(ASRC_IN_IDX);
    Sys_DMA_ChannelConfig(ASRC_IN_IDX, RX_DMA_ASRC_IN, SUBFRAME_LENGTH, 0,
                          (uint32_t)Dsp2CmBuff0dec, (uint32_t)&ASRC->IN);

#ifdef RM_TX_POWER_BOOST
    Sys_RFFE_SetTXPower(Load_Tx_Power_Value());
#else
    Sys_RFFE_SetTXPower(0);
#endif

    NVIC_SetPriority(DSP1_IRQn, 4);
    NVIC_ClearPendingIRQ(DSP1_IRQn);
    NVIC_EnableIRQ(DSP1_IRQn);
    NVIC_SetPriority(TIMER_IRQn(TIMER_REGUL), 4);
    NVIC_ClearPendingIRQ(TIMER_IRQn(TIMER_REGUL));
    NVIC_EnableIRQ(TIMER_IRQn(TIMER_REGUL));
    NVIC_SetPriority(AUDIOSINK_PERIOD_IRQn, 4);
    NVIC_ClearPendingIRQ(AUDIOSINK_PERIOD_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);
    NVIC_SetPriority(AUDIOSINK_PHASE_IRQn, 4);
    NVIC_ClearPendingIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);

    Sys_Clocks_SystemClkPrescale1(AUDIOCLK_PRESCALE_5);
    Sys_Audio_Set_Config(AUDIO_CONFIG);
    AUDIO->OD_CFG = (DCRM_CUTOFF_240HZ | DITHER_ENABLE);
    AUDIO->SDM_CFG = 0x00002;
    AUDIO->OD_GAIN = 0x800;

#ifdef OD_DIO12_OUTPUT
    /* OD 单端输出：DIO12 做 OD_P（OD 模式已关打印） */
    Sys_DIO_Config(OD_P_DIO, DIO_6X_DRIVE | DIO_LPF_DISABLE |
                   DIO_NO_PULL | DIO_MODE_OD_P);

    Sys_DMA_ChannelDisable(OD_DMA_NUM);
    Sys_DMA_ChannelConfig(OD_DMA_NUM, RX_DMA_OD, 16, 0,
                          (uint32_t)BufferOut, (uint32_t)&(AUDIO->OD_DATA));
    {
        uint32_t i;
        for (i = 0; i < 10000; i++)
        {
            Sys_Watchdog_Refresh();
            Sys_Delay_ProgramROM(1000);
        }
    }
    DMA_CTRL1[OD_DMA_NUM].TRANSFER_LENGTH_SHORT = 2 * FRAME_LENGTH;

    /* ASRC->OUT -> BufferOut（CIRC 连续，同 sleep OD 路径） */
    Sys_DMA_ChannelDisable(ASRC_OUT_IDX);
    Sys_DMA_ChannelConfig(ASRC_OUT_IDX, RX_DMA_ASRC_OUT,
                          2 * FRAME_LENGTH, 0,
                          (uint32_t)&ASRC->OUT, (uint32_t)BufferOut);
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);
#else
    /* PCM 从机输出：7100 提供 BCLK/FS，RSL10 移位输出 SERO(DIO14) */
    Initialize_Raw_PCM_Output_Type();

#ifdef PCM_TONE_TEST
    /* 24k 1kHz 正弦：填两缓冲 + ch5 直接流，绕过 ch4/ASRC（Pcm_tx_dma_isr 自续轮流） */
    Pcm_Sine24k_Fill();
    pcm_fill = 1;   /* ch5 首次已由 Initialize 武装 buffer0，ISR 从 buffer1 开始轮流 */
    pcm_ready = 0xFF;
    pcm_waiting = 0;

    NVIC_SetPriority(DMA_IRQn(PCM_DMA_NUM), 3);
    NVIC_ClearPendingIRQ(DMA_IRQn(PCM_DMA_NUM));
    NVIC_EnableIRQ(DMA_IRQn(PCM_DMA_NUM));
    Sys_DMA_ClearChannelStatus(PCM_DMA_NUM);
    Sys_DMA_ChannelEnable(PCM_DMA_NUM);
    Sys_PCM_Enable();
#else    /* ifdef PCM_TONE_TEST */
#ifndef RESAMP_SW
    /* ch4: ASRC->OUT -> pcm_tx_buf（16-bit 采样 → 32-bit 字低 16 位） */
    Sys_DMA_ChannelDisable(ASRC_OUT_IDX);
    Sys_DMA_ChannelConfig(ASRC_OUT_IDX, RX_DMA_ASRC_OUT, PCM_FRAME_WORDS, 0,
                          (uint32_t)&ASRC->OUT, (uint32_t)&pcm_tx_buf[0][0]);
#endif    /* ifndef RESAMP_SW */

    /* 双缓冲：ch4 填 pcm_tx_buf[pcm_fill]，ch5 流 pcm_tx_buf[pcm_ready]。
       ch5 由 ch4 的完成中断启动（见 app_func.c）。
       RESAMP_SW：pcm_tx_buf 由软件重采样器填（Resamp_Process），ch5 由它武装。 */
    pcm_fill = 0;
    pcm_ready = 0xFF;
    pcm_waiting = 1;

#ifndef RESAMP_SW
    NVIC_SetPriority(DMA_IRQn(ASRC_OUT_IDX), 3);
    NVIC_ClearPendingIRQ(DMA_IRQn(ASRC_OUT_IDX));
    NVIC_EnableIRQ(DMA_IRQn(ASRC_OUT_IDX));
    Sys_DMA_ClearChannelStatus(ASRC_OUT_IDX);
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);
#endif    /* ifndef RESAMP_SW */

    NVIC_SetPriority(DMA_IRQn(PCM_DMA_NUM), 3);
    NVIC_ClearPendingIRQ(DMA_IRQn(PCM_DMA_NUM));
    NVIC_EnableIRQ(DMA_IRQn(PCM_DMA_NUM));
    Sys_PCM_Enable();
#endif    /* ifdef PCM_TONE_TEST */
#endif    /* ifdef OD_DIO12_OUTPUT */
}

/* Minimal audio pipeline resume after LINK_DISCONNECTED shutdown.
 * No DSP reset / OD delay — assumes firmware and clock config are retained. */
void Audio_Resume(void)
{
    SYSCTRL->DSS_CTRL = DSS_LPDSP32_RESUME;

    Sys_Audiosink_ResetCounters();
    AUDIOSINK_CTRL->PHASE_CNT_START_ALIAS  = PHASE_CNT_START_BITBAND;
    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = PERIOD_CNT_START_BITBAND;

    Sys_DMA_ChannelDisable(ASRC_IN_IDX);
    Sys_DMA_ChannelConfig(ASRC_IN_IDX, RX_DMA_ASRC_IN, SUBFRAME_LENGTH, 0,
                          (uint32_t)Dsp2CmBuff0dec, (uint32_t)&ASRC->IN);

#ifdef OD_DIO12_OUTPUT
    Sys_DMA_ChannelDisable(OD_DMA_NUM);
    Sys_DMA_ChannelConfig(OD_DMA_NUM, RX_DMA_OD, 16, 0,
                          (uint32_t)BufferOut, (uint32_t)&(AUDIO->OD_DATA));
    DMA_CTRL1[OD_DMA_NUM].TRANSFER_LENGTH_SHORT = 2 * FRAME_LENGTH;

    Sys_DMA_ChannelDisable(ASRC_OUT_IDX);
    Sys_DMA_ChannelConfig(ASRC_OUT_IDX, RX_DMA_ASRC_OUT,
                          2 * FRAME_LENGTH, 0,
                          (uint32_t)&ASRC->OUT, (uint32_t)BufferOut);
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);
#else
    Sys_DMA_ChannelDisable(PCM_DMA_NUM);
    Sys_PCM_Config(PCM_CFG_TX);

#ifndef RESAMP_SW
    Sys_DMA_ChannelDisable(ASRC_OUT_IDX);
    Sys_DMA_ChannelConfig(ASRC_OUT_IDX, RX_DMA_ASRC_OUT, PCM_FRAME_WORDS, 0,
                          (uint32_t)&ASRC->OUT, (uint32_t)&pcm_tx_buf[0][0]);
#endif

    pcm_fill = 0;
    pcm_ready = 0xFF;
    pcm_waiting = 1;

#ifndef RESAMP_SW
    NVIC_ClearPendingIRQ(DMA_IRQn(ASRC_OUT_IDX));
    NVIC_EnableIRQ(DMA_IRQn(ASRC_OUT_IDX));
    Sys_DMA_ClearChannelStatus(ASRC_OUT_IDX);
    Sys_DMA_ChannelEnable(ASRC_OUT_IDX);
#endif

    NVIC_ClearPendingIRQ(DMA_IRQn(PCM_DMA_NUM));
    NVIC_EnableIRQ(DMA_IRQn(PCM_DMA_NUM));
    Sys_PCM_Enable();
#endif    /* ifdef OD_DIO12_OUTPUT */

    Sys_Timers_Start(1 << TIMER_REGUL);
}

void App_Initialize(void)
{
    /* Mask all interrupts */
    __set_PRIMASK(PRIMASK_DISABLE_INTERRUPTS);

    /* Disable all interrupts and clear any pending interrupts */
    Sys_NVIC_DisableAllInt();
    Sys_NVIC_ClearAllPendingInt();

    DIO_JTAG_SW_PAD_CFG->CM3_JTAG_DATA_EN_ALIAS =
        CM3_JTAG_DATA_DISABLED_BITBAND;
    DIO_JTAG_SW_PAD_CFG->CM3_JTAG_TRST_EN_ALIAS =
        CM3_JTAG_TRST_DISABLED_BITBAND;

    /* Test DIO12 to pause the program to make it easy to re-flash */
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
    ACS_VDDPA_CTRL->VDDPA_SW_CTRL_ALIAS = VDDPA_SW_VDDRF_BITBAND;

    /* Enable/disable buck converter */
    ACS_VCC_CTRL->BUCK_ENABLE_ALIAS = VCC_BUCK_LDO_CTRL;

    /* Enable RF power switches */
    SYSCTRL_RF_POWER_CFG->RF_POWER_ALIAS = RF_POWER_ENABLE_BITBAND;

    /* Remove RF isolation */
    SYSCTRL_RF_ACCESS_CFG->RF_ACCESS_ALIAS = RF_ACCESS_ENABLE_BITBAND;

    /* Start the 48 MHz oscillator without changing the other register bits */
    RF->XTAL_CTRL = ((RF->XTAL_CTRL & ~XTAL_CTRL_DISABLE_OSCILLATOR) |
                     XTAL_CTRL_REG_VALUE_SEL_INTERNAL);

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
    CLK->DIV_CFG2 = (CPCLK_PRESCALE_8 | DCCLK_PRESCALE_4);

    BBIF->CTRL = (BB_CLK_ENABLE | BBCLK_DIVIDER_8 | BB_DEEP_SLEEP);

    Sys_Watchdog_Refresh();

    /* Seed the random number generator */
    srand(1);

    /* Delay for hardware stabilization */
    {
        uint32_t delay_i;
        for (delay_i = 0; delay_i < 10000; delay_i++)
        {
            Sys_Watchdog_Refresh();
            Sys_Delay_ProgramROM(1000);
        }
    }

    /* Load DSP firmware (retained during sleep) */
    {
        uint32_t dsp_i;

        SYSCTRL->DSS_CTRL = DSS_LPDSP32_PAUSE;

        Sys_Flash_Copy((uint32_t)&LPDSP32_Prog_40bit_PM[0], DSP_PRAM0_BASE,
                       MEM_PM_SIZE, COPY_TO_MEM_BITBAND);
        Sys_Flash_Copy((uint32_t)&LPDSP32_Data_low_DM[0], DSP_DRAM01_BASE,
                       MEM_DMA_SIZE, COPY_TO_MEM_BITBAND);
        Sys_Flash_Copy((uint32_t)&LPDSP32_Data_low_DM[MEM_DMA_SIZE + 3],
                       DSP_DRAM4_BASE, 8000, COPY_TO_MEM_BITBAND);
        Sys_Flash_Copy((uint32_t)&LPDSP32_Data_low_DM[(MEM_DMA_SIZE + 3) + 8001],
                       (DSP_DRAM4_BASE + 0x1F41), (MEM_DMB_SIZE - 8000),
                       COPY_TO_MEM_BITBAND);
        while (FLASH_COPY_CTRL->BUSY_ALIAS != COPY_IDLE_BITBAND)
        {
            Sys_Watchdog_Refresh();
        }

        SYSCTRL->DSS_CTRL = DSS_LPDSP32_PAUSE;
        SYSCTRL->DSS_CTRL = DSS_RESET;
        for (dsp_i = 0; dsp_i < 7000; dsp_i++)
        {
            Sys_Watchdog_Refresh();
        }

        uint8_t *msg = MEM_MESSAGE;
        msg[0] = SUBFRAME_LENGTH;
        msg[1] = SUBFRAME_LENGTH;
        msg[2] = SUBFRAME_LENGTH;
        msg[3] = SUBFRAME_LENGTH;
        msg[4] = CODEC_MODE;

        SYSCTRL->DSS_CTRL = DSS_LPDSP32_RESUME;
    }

    /* Trim RC oscillator to 3 MHz (required by Sys_PowerModes_Wakeup) */
    Sys_Clocks_OscRCCalibratedConfig(3000);

    /* Customized parameters for the LLD SLEEP module */
    {
        struct lld_sleep_params_t desired_lld_sleep_params;
        if (RTC_CLK_SRC == RTC_CLK_SRC_RC_OSC)
        {
            desired_lld_sleep_params.twosc = TWOSC_RC_OSC;
        }
        else
        {
            desired_lld_sleep_params.twosc = TWOSC;
        }
        BLE_LLD_Sleep_Params_Set(desired_lld_sleep_params);
    }

    /* Initialize the baseband and BLE stack */
    BLE_Initialize();

    /* Initialize environment */
    App_Env_Initialize();
    printf_init();

#ifdef APP_RM_ENABLE
    APP_RM_Init(ear_side);
    /* Boot default is BLE low-power mode.
     * On cold boot, Main_Loop will switch to RM mode immediately. */
    RF_SwitchToBLEMode();
    app_env.audio_streaming = 0;
#endif

    Sys_RFFE_SetTXPower(0);

    /* Configure the sleep mode parameters and configurations */
    Sleep_Mode_Configure(&sleep_mode_env);

    /* BLE not in sleep mode and ready for normal operations */
    BLE_Is_Awake_Flag_Set();

    if (RTC_CLK_SRC != RTC_CLK_SRC_XTAL32K)
    {
        low_power_clk_param.dynamic_measurement_enable = false;
        low_power_clk_param.low_power_enable = true;
    }

    /* Enable CM3 loop cache */
    SYSCTRL->CSS_LOOP_CACHE_CFG = CSS_LOOP_CACHE_ENABLE;

    app_env.init_done = 1;

#ifdef BAT_ADC_ENABLE
    Sys_DIO_Config(3, DIO_MODE_DISABLE | DIO_NO_PULL);

    /* Set the ADC configuration */
    Sys_ADC_Set_Config(ADC_NORMAL | ADC_PRESCALE_1280H);

    Sys_ADC_InputSelectConfig(0, ADC_POS_INPUT_DIO3 |
                              ADC_NEG_INPUT_GND);
#endif

    /* Stop masking interrupts */
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

    /* 200ms periodic timer for RM LED */
    ke_timer_set(APP_TEST_TIMER, TASK_APP, TIMER_200MS_SETTING);

    /* Initialize the custom service environment */
    CustomService_Env_Initialize();
    RemproService_Env_Initialize();

    /* Initialize the battery service server environment */
    Bass_Env_Initialize();
}
