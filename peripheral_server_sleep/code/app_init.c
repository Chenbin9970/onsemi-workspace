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

#ifdef APP_RM_ENABLE
    /* ---- Audio Sink Clock Counters (for ASRC) ---- */
    Sys_Audiosink_ResetCounters();
    Sys_Audiosink_InputClock(0, ((uint32_t)(SAMPL_CLK << DIO_AUDIOSINK_SRC_CLK_Pos)));
    Sys_Audiosink_Config(AUDIO_SINK_PERIODS_16, 0, 0);
    AUDIOSINK_CTRL->PHASE_CNT_START_ALIAS  = PHASE_CNT_START_BITBAND;
    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = PERIOD_CNT_START_BITBAND;

    NVIC_ClearPendingIRQ(AUDIOSINK_PHASE_IRQn);
    NVIC_EnableIRQ(AUDIOSINK_PHASE_IRQn);

    /* ---- LPDSP32 G722 Decoder Loading ---- */
    {
        uint32_t dsp_wait = 0;
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

        while (dsp_wait < 7000)
        {
            dsp_wait++;
            Sys_Watchdog_Refresh();
        }

        SYSCTRL->DSS_CTRL = DSS_LPDSP32_RESUME;
    }

    /* Setup G722 decoder parameters via shared memory */
    {
        uint8_t *message = MEM_MESSAGE;
        *message       = SUBFRAME_LENGTH;
        *(message + 1) = SUBFRAME_LENGTH;
        *(message + 2) = SUBFRAME_LENGTH;
        *(message + 3) = SUBFRAME_LENGTH;
        *(message + 4) = CODEC_MODE;
    }

    /* ---- ASRC DMA Channel (DSP decoded data -> ASRC input) ---- */
    Sys_DMA_ChannelConfig(ASRC_IN_IDX, RX_DMA_ASRC_IN, SUBFRAME_LENGTH, 0,
                          (uint32_t)Dsp2CmBuff0dec, (uint32_t)&ASRC->IN);

    /* ---- NVIC Priority / Enable for Audio ISRs ---- */
    NVIC_SetPriority(DSP1_IRQn, 4);
    NVIC_EnableIRQ(DSP1_IRQn);

    NVIC_SetPriority(TIMER_IRQn(TIMER_REGUL), 4);

    NVIC_SetPriority(AUDIOSINK_PERIOD_IRQn, 4);
    NVIC_SetPriority(AUDIOSINK_PHASE_IRQn, 4);

    /* ---- OD (Sigma-Delta Output Driver) Initialization ---- */
    OD_Init();
#endif /* APP_RM_ENABLE */

    /* Configure ADC channel 0 to measure VBAT/2 */
    Sys_ADC_Set_Config(ADC_VBAT_DIV2_NORMAL | ADC_NORMAL |
                       ADC_PRESCALE_200);
    Sys_ADC_InputSelectConfig(0, (ADC_NEG_INPUT_GND |
                                  ADC_POS_INPUT_VBAT_DIV2));

    /* Initialize environment */
    App_Env_Initialize();

#ifdef APP_RM_ENABLE
    APP_RM_Init(ear_side);
    RF_SwitchToBLEMode();
#endif

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

#ifdef APP_RM_ENABLE
    ke_timer_set(APP_TEST_TIMER, TASK_APP, TIMER_200MS_SETTING);
#endif

    /* Initialize the custom service environment */
    CustomService_Env_Initialize();

    /* Initialize the battery service server environment */
    Bass_Env_Initialize();
}
