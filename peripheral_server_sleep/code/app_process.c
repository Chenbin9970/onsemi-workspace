#if 0
/* ----------------------------------------------------------------------------
 * Copyright (c) 2015-2017 Semiconductor Components Industries, LLC (d/b/a
 * ON Semiconductor), All Rights Reserved
 *
 * Copyright (C) RivieraWaves 2009-2016
 *
 * This module is derived in part from example code provided by RivieraWaves
 * and as such the underlying code is the property of RivieraWaves [a member
 * of the CEVA, Inc. group of companies], together with additional code which
 * is the property of ON Semiconductor. The code (in whole or any part) may not
 * be redistributed in any form without prior written permission from
 * ON Semiconductor.
 *
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 *
 * This is Reusable Code.
 *
 * ----------------------------------------------------------------------------
 * app_process.c
 * - Application task handler definition and support processes
 * ----------------------------------------------------------------------------
 * $Revision: 1.62 $
 * $Date: 2019/09/06 19:54:47 $
 * ------------------------------------------------------------------------- */

#include "app.h"

/* Parameters for RC Oscillator period measurements */
volatile uint32_t loop_cnt = 0;

#define MAX_BUF_CNT                     5
float measure_buf[MAX_BUF_CNT];
uint8_t buf_cnt = 0;

const struct ke_task_desc TASK_DESC_APP =
{
    NULL,
    &appm_default_handler,
    appm_state,
    APPM_STATE_MAX,
    APP_IDX_MAX
};

/* State and event handler definition */
const struct ke_msg_handler appm_default_state[] =
{
    /* Note: Put the default handler on top as this is used for handling any
     *       messages without a defined handler */
    { KE_MSG_DEFAULT_HANDLER, (ke_msg_func_t)Msg_Handler },
    BLE_MESSAGE_HANDLER_LIST,
    BASS_MESSAGE_HANDLER_LIST,
    CS_MESSAGE_HANDLER_LIST,
    APP_MESSAGE_HANDLER_LIST
};

/* Use the state and event handler definition for all states. */
const struct ke_state_handler appm_default_handler
    = KE_STATE_HANDLER(appm_default_state);

/* Defines a place holder for all task instance's state */
ke_state_t appm_state[APP_IDX_MAX];

/* ----------------------------------------------------------------------------
 * Function      : void Sleep_Mode_Configure(
 *                         struct sleep_mode_env_tag *sleep_mode_env)
 * ----------------------------------------------------------------------------
 * Description   : Configure the sleep mode
 * Inputs        : Pre-defined parameters and configurations
 *                 for the sleep mode
 * Outputs       : sleep_mode_env   - Parameters and configurations
 *                                    for the sleep mode
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Sleep_Mode_Configure(struct sleep_mode_env_tag *sleep_mode_env)
{
    struct sleep_mode_init_env_tag sleep_mode_init_env;

    /* Set the clock source for RTC */
    sleep_mode_init_env.rtc_ctrl = RTC_CLK_SRC;

    /* if RTC clock source is XTAL 32 kHz oscillator */
    if (RTC_CLK_SRC == RTC_CLK_SRC_XTAL32K)
    {
        /* Enable XTAL32K oscillator amplitude control
         * Set XTAL32K load capacitance to 0x38: 22.4 pF
         * Enable XTAL32K oscillator */
        ACS->XTAL32K_CTRL = \
            (XTAL32K_XIN_CAP_BYPASS_DISABLE                                |
             XTAL32K_AMPL_CTRL_ENABLE                                      |
             XTAL32K_NOT_FORCE_READY                                       |
             (XTAL32K_CLOAD_TRIM_VALUE << ACS_XTAL32K_CTRL_CLOAD_TRIM_Pos) |
             (XTAL32K_ITRIM_VALUE << ACS_XTAL32K_CTRL_ITRIM_Pos)           |
             XTAL32K_IBOOST_DISABLE                                        |
             XTAL32K_ENABLE);

        /* Wait for XTAL32K oscillator to be ready */
        while (ACS_XTAL32K_CTRL->READY_ALIAS != XTAL32K_OK_BITBAND);

        LowPowerClock_Source_Set(0);
    }

    /* else: if RTC clock source is RC 32 kHz oscillator */
    else if (RTC_CLK_SRC == RTC_CLK_SRC_RC_OSC)
    {
        /* Start the RC oscillator */
        Sys_Clocks_Osc32kHz(RC_OSC_ENABLE | RC_OSC_NOM);

        /* Read the OSC_32K calibration trim data from NVR4 */
        unsigned int osc_calibration_value = 0;
        Sys_ReadNVR4(MANU_INFO_OSC_32K, 1, (unsigned
                                            int *)&osc_calibration_value);

        /* Use calibrated value for RC clock */
        if (osc_calibration_value != 0xFFFFFFFF)
        {
            ACS_RCOSC_CTRL->FTRIM_32K_BYTE = (uint8_t)(osc_calibration_value);
        }

        LowPowerClock_Source_Set(1);

        /* In us, for typical RCOSC until measurement is obtained. */
        RTCCLK_Period_Value_Set(RCCLK_PERIOD_VALUE);

        /* Delay for 4 ms */
        Sys_Delay_ProgramROM(4 * (SystemCoreClock / 1000));

        /* Set-up the Audiosink block for frequency measurement */
        Sys_Audiosink_ResetCounters();
        Sys_Audiosink_InputClock(0, AUDIOSINK_CLK_SRC_STANDBYCLK);
        Sys_Audiosink_Config(AUDIO_SINK_PERIODS_16, 0, 0);

        /* Enable interrupts */
        NVIC_ClearPendingIRQ(AUDIOSINK_PERIOD_IRQn);
        NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

        /* Start period counter to start period measurement */
        AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = 1;

    }

    /* else: if RTC clock source is external oscillator */
    else
    {
        DIO->CFG[EXT_LOW_POWER_CLK_GPIO_NUM] = (DIO_2X_DRIVE     |
                                                DIO_LPF_DISABLE  |
                                                DIO_NO_PULL      |
                                                DIO_MODE_INPUT);

        LowPowerClock_Source_Set(1);

        /* Clock period in us for external clock */
        RTCCLK_Period_Value_Set(EXT_LOW_POWER_CLK_PERIOD_VALUE);

        /* Set-up the Audiosink block for frequency measurement */
        Sys_Audiosink_ResetCounters();
        Sys_Audiosink_InputClock(0, AUDIOSINK_CLK_SRC_STANDBYCLK);
        Sys_Audiosink_Config(AUDIO_SINK_PERIODS_16, 0, 0);

        /* Enable interrupts */
        NVIC_ClearPendingIRQ(AUDIOSINK_PERIOD_IRQn);
        NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

        /* Start period counter to start period measurement */
        AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = 1;

    }

    /* Set delay and wake-up sources, use
     *    WAKEUP_DELAY_[ 1 | 2 | 4 | ... | 128],
     *    WAKEUP_DCDC_OVERLOAD_[ENABLE | DISABLE],
     *    WAKEUP_WAKEUP_PAD_[RISING | FALLING],
     *    WAKEUP_DIO*_[RISING | FALLING],
     *    WAKEUP_DIO*_[ENABLE | DISABLE] */
    sleep_mode_init_env.wakeup_cfg = WAKEUP_DELAY_32          |
                                     WAKEUP_WAKEUP_PAD_RISING |
                                     WAKEUP_DIO3_DISABLE      |
                                     WAKEUP_DIO2_DISABLE      |
                                     WAKEUP_DIO1_DISABLE      |
                                     WAKEUP_DIO0_DISABLE;

    /* Set wake-up control/status registers, use
     *    PADS_RETENTION_[ENABLE | DISABLE],
     *    BOOT_FLASH_APP_REBOOT_[ENABLE | DISABLE],
     *    BOOT_[CUSTOM | FLASH_XTAL_*],
     *    WAKEUP_DCDC_OVERLOAD_CLEAR,
     *    WAKEUP_PAD_EVENT_CLEAR,
     *    WAKEUP_RTC_ALARM_CLEAR,
     *    WAKEUP_BB_TIMER_CLEAR,
     *    WAKEUP_DIO3_EVENT_CLEAR,
     *    WAKEUP_DIO2_EVENT_CLEAR,
     *    WAKEUP_DIO1_EVENT_CLEAR],
     *    WAKEUP_DIO0_EVENT_CLEAR */
    sleep_mode_env->wakeup_ctrl = PADS_RETENTION_ENABLE         |
                                  BOOT_FLASH_APP_REBOOT_DISABLE |
                                  BOOT_CUSTOM                   |
                                  WAKEUP_DCDC_OVERLOAD_CLEAR    |
                                  WAKEUP_PAD_EVENT_CLEAR        |
                                  WAKEUP_RTC_ALARM_CLEAR        |
                                  WAKEUP_BB_TIMER_CLEAR         |
                                  WAKEUP_DIO3_EVENT_CLEAR       |
                                  WAKEUP_DIO2_EVENT_CLEAR       |
                                  WAKEUP_DIO1_EVENT_CLEAR       |
                                  WAKEUP_DIO0_EVENT_CLEAR;

    /* Set wake-up application start address (LSB must be set) */
    sleep_mode_init_env.app_addr =
        (uint32_t)(&Wakeup_From_Sleep_Application_asm) | 1;

    /* Set wake-up restore address */
    sleep_mode_init_env.wakeup_addr = (uint32_t)(DRAM2_TOP + 1 -
                                                 POWER_MODE_WAKEUP_INFO_SIZE);

    /* Configure memory retention */
    sleep_mode_env->mem_power_cfg = (DRAM0_POWER_ENABLE |
                                     DRAM1_POWER_ENABLE |
                                     DRAM2_POWER_ENABLE |
                                     BB_DRAM0_POWER_ENABLE);

    /* Configure memory at wake-up (PROM must be part of this) */
    sleep_mode_init_env.mem_power_cfg_wakeup = (PROM_POWER_ENABLE  |
                                                DRAM0_POWER_ENABLE |
                                                DRAM1_POWER_ENABLE |
                                                DRAM2_POWER_ENABLE |
                                                BB_DRAM0_POWER_ENABLE);

    /* Set DMA channel used to save/restore RF registers
     * in each sleep/wake-up cycle */
    sleep_mode_init_env.DMA_channel_RF = DMA_CHAN_SLP_WK_RF_REGS_COPY;

    /* Set VDDxRet Trim Values */
    sleep_mode_init_env.VDDTRET_trim = RTE_VDDTRET_TRIM_VALUE;
    sleep_mode_init_env.VDDMRET_trim = RTE_VDDMRET_TRIM_VALUE;
    sleep_mode_init_env.VDDCRET_trim = RTE_VDDCRET_TRIM_VALUE;

    /* Perform initializations required for sleep mode */
#ifdef APP_SLEEP_2MBPS_SUPPORT
    Sys_PowerModes_Sleep_Init_2Mbps(&sleep_mode_init_env);
#else    /* ifdef APP_SLEEP_2MBPS_SUPPORT */
    Sys_PowerModes_Sleep_Init(&sleep_mode_init_env);
#endif    /* ifdef APP_SLEEP_2MBPS_SUPPORT */
}

/* ----------------------------------------------------------------------------
 * Function      : void Wakeup_From_Sleep_Application(void)
 * ----------------------------------------------------------------------------
 * Description   : Restore system states from retention RAM and continue
 *                 application from flash
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Wakeup_From_Sleep_Application(void)
{
    /* Execute steps required to wake-up the system from sleep mode */
#ifdef APP_SLEEP_2MBPS_SUPPORT
    Sys_PowerModes_Wakeup_2Mbps();
#else    /* ifdef APP_SLEEP_2MBPS_SUPPORT */
    Sys_PowerModes_Wakeup();
#endif    /* ifdef APP_SLEEP_2MBPS_SUPPORT */

    /* The system is awake from this point, continue application from flash */
    Continue_Application();
}

/* ----------------------------------------------------------------------------
 * Function      : void Continue_Application(void)
 * ----------------------------------------------------------------------------
 * Description   : Restore application states, wait until BLE is awake and
 *                 go to the main loop
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Continue_Application(void)
{
    /* Lower drive strength (required when VDDO > 2.7)*/
    DIO->PAD_CFG = PAD_LOW_DRIVE;

    /* Turn LED on */
    Sys_DIO_Config(LED_DIO, DIO_MODE_GPIO_OUT_1);

#ifndef DEBUG_UART_ENABLE
    /* Disable DIO4 and DIO5 to avoid current consumption on VDDO */
    Sys_DIO_Config(4, DIO_MODE_DISABLE | DIO_NO_PULL);
    Sys_DIO_Config(5, DIO_MODE_DISABLE | DIO_NO_PULL);
#endif

    /* Turn off pad retention */
    ACS_WAKEUP_CTRL->PADS_RETENTION_EN_BYTE = PADS_RETENTION_DISABLE_BYTE;

    /* Configure ADC channel 0 to measure VBAT/2 */
    Sys_ADC_Set_Config(ADC_VBAT_DIV2_NORMAL | ADC_NORMAL |
                       ADC_PRESCALE_200);
    Sys_ADC_InputSelectConfig(0, (ADC_NEG_INPUT_GND |
                                  ADC_POS_INPUT_VBAT_DIV2));

    /* Configure clock dividers */
    CLK->DIV_CFG0 = (SLOWCLK_PRESCALE_8 | BBCLK_PRESCALE_2 |
                     USRCLK_PRESCALE_1);
    CLK->DIV_CFG2 = (CPCLK_PRESCALE_8 | DCCLK_PRESCALE_4);

    /* Update Flash timing */
    FLASH->DELAY_CTRL = DEFAULT_READ_MARGIN | FLASH_DELAY_VALUE;

    /* Switch to RF clock */
    CLK_SYS_CFG->SYSCLK_SRC_SEL_BYTE = SYSCLK_CLKSRC_RFCLK_BYTE;

    /* Configure the baseband divider and force wake-up in case it is required
     * due to an early ACS wake-up condition (e.g. PAD, RTC) */
    BBIF->CTRL = BB_CLK_ENABLE | BBCLK_DIVIDER_VALUE | BB_WAKEUP;

    /* Disable interrupts */
    __disable_irq();
    while (!(BLE_Is_Awake()))
    {
        SYS_WAIT_FOR_INTERRUPT;

        /* Enable interrupts */
        __enable_irq();

        /* Allow pended interrupts to be recognized */
        __ISB();

        /* Disable interrupts */
        __disable_irq();
    }

    if (RTC_CLK_SRC != RTC_CLK_SRC_XTAL32K)
    {
#if (LOW_POWER_CLK_UPDATE == LOW_POWER_CLK_UPDATE_ENABLE)
        Enable_Audiosink_Measurement();
#endif
    }

    /* Stop masking interrupts */
    __enable_irq();

    /* Stop forcing baseband wake-up */
    BBIF->CTRL = BB_CLK_ENABLE | BBCLK_DIVIDER_VALUE | BB_DEEP_SLEEP;

    /* Main application loop */
    Main_Loop();
}

/* ----------------------------------------------------------------------------
 * Function      : void Enable_Audiosink_Measurement(void)
 * ----------------------------------------------------------------------------
 * Description   : - Wait for approximately RC_OSC_MEASUREMENT_INTERVAL seconds
 *                   before enabling audiosink interrupt.
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Enable_Audiosink_Measurement(void)
{
    /* The number of times we wait for the device to go through a wake-
     * sleep cycle */
    uint16_t num_wakeup;

    /* num_wakeup cycles should be determined by the connection interval
     * of peer device */
    /* Update RC_OSC period every num_wakeup cycles of sleep. */
    if(ble_env.state == APPM_CONNECTED)
    {
    	num_wakeup = ((LOW_POWER_CLK_MEASUREMENT_INTERVAL_S *
                   LOW_POWER_CLK_SCALE_MEASUREMENT_INTERVAL) /
                  (ble_env.actual_con_interval * (ble_env.actual_con_latency + 1)));
    }
    else
    {
    	num_wakeup = (LOW_POWER_CLK_MEASUREMENT_INTERVAL_S * 1600 / ADV_INT_CONNECTABLE_MODE);
    }


    if(num_wakeup == 0)
    {
    	num_wakeup = 1;
    }

    /* Update RC_OSC period every num_wakeup cycles of sleep. */
    loop_cnt++;
    if ((loop_cnt % num_wakeup) == 0)
    {
        /* Set-up the Audiosink block for frequency measurement */
        Sys_Audiosink_ResetCounters();
        Sys_Audiosink_InputClock(0, AUDIOSINK_CLK_SRC_STANDBYCLK);
        Sys_Audiosink_Config(AUDIO_SINK_PERIODS_16, 0, 0);

        /* Enable interrupts */
        NVIC_ClearPendingIRQ(AUDIOSINK_PERIOD_IRQn);
        NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

        /* Start period counter to start period measurement */
        AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = 1;

        /*Disable sleep mode */
#if (LOW_POWER_CLK_UPDATE == LOW_POWER_CLK_UPDATE_DISABLE)
        if (low_power_clk_param.dynamic_measurement_enable == false)
#endif    /* if !RC_OSC_UPDATE */
        {
            low_power_clk_param.low_power_enable = false;
        }
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void Measure_Battery_Level(void)
 * ----------------------------------------------------------------------------
 * Description   : - Read the battery level using ADC, calculate and update
 *                   its average value when applicable
 *                 - If the average value changes, set the notification flag
 *                   for battery service
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Measure_Battery_Level(void)
{
    uint16_t level;

    /* Calculate the battery level as a percentage, scaling the battery
     * voltage between 1.4V (max) and 1.1V (min) */
    level = ((ADC->DATA_TRIM_CH[0] - VBAT_1P1V_MEASURED) * BAT_LVL_MAX
             / (VBAT_1P4V_MEASURED - VBAT_1P1V_MEASURED));
    level = ((level > BAT_LVL_MAX) ? BAT_LVL_MAX : level);

    /* Add to the current sum and increment the number of reads */
    app_env.sum_batt_lvl += level;
    app_env.num_batt_read++;

    /* Calculate the average over the past 16 voltage reads */
    if (app_env.num_batt_read == 16)
    {
        if ((app_env.sum_batt_lvl >> 4) != app_env.batt_lvl)
        {
            app_env.send_batt_ntf = 1;

            /* Update the average value of battery level */
            app_env.batt_lvl = (app_env.sum_batt_lvl >> 4);
        }

        /* Reset parameters for the next round of battery measurement */
        app_env.num_batt_read = 0;
        app_env.sum_batt_lvl = 0;
    }
}

/* ----------------------------------------------------------------------------
 * Function      : uint8_t Emulate_CS_Val_Notif_Change(uint8_t val_notif)
 * ----------------------------------------------------------------------------
 * Description   : Emulate the change of custom service notification data
 * Inputs        : - val_notif  - value of custom service notification
 * Outputs       : return value - updated value of custom service notification
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
uint8_t Emulate_CS_Val_Notif_Change(uint8_t val_notif)
{
    val_notif++;
    if ((val_notif & 0x0F) == 0x0A)
    {
        val_notif = val_notif & 0xF0;
        val_notif = val_notif + 0x10;
        if ((val_notif & 0xF0) == 0xA0)
        {
            val_notif = val_notif & 0x0F;
        }
    }

    return (val_notif);
}

/* ----------------------------------------------------------------------------
 * Function      : int Msg_Handler(ke_msg_id_t const msg_id,
 *                                 void const *param,
 *                                 ke_task_id_t const dest_id,
 *                                 ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle any message received from kernel that doesn't have
 *                 a dedicated handler
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameter (unused)
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
#ifdef APP_RM_ENABLE
int APP_Timer(ke_msg_id_t const msg_id, void const *param,
              ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    ke_timer_set(APP_TEST_TIMER, TASK_APP, TIMER_200MS_SETTING);

    if (ble_env.state == APPM_CONNECTED)
        Sys_GPIO_Set_High(LED_DIO);
    else if (ble_env.state == APPM_ADVERTISING)
        Sys_GPIO_Toggle(LED_DIO);
    else
        Sys_GPIO_Set_Low(LED_DIO);

    return (KE_MSG_CONSUMED);
}
#endif

int Msg_Handler(ke_msg_id_t const msg_id, void *param,
                ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : void AUDIOSINK_PERIOD_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : Calculates the average period for every 16 measurements and
 *                 then averages RC_OSC_INITIAL_MEASUREMENT number of those
 *                 measurements before updating the RC oscillator.
 *                 Subsequent updates are done after RC_OSC_DYNAMIC_MEAUREMENT
 *                 number of measurements.
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : Using 8 MHz System core clock.
 * ------------------------------------------------------------------------- */
/* AUDIOSINK_PERIOD_IRQHandler moved to app_func.c (alias to Ascc_period_isr) */
#if 0
void AUDIOSINK_PERIOD_IRQHandler(void)
{
#ifdef APP_RM_ENABLE
    if (app_env.audio_streaming)
    {
        Ascc_period_isr();
        return;
    }
#endif

    /* Parameters for RC oscillator period measurements */
    static uint32_t num_measurement = LOW_POWER_CLK_INITIAL_MEASUREMENT;
    static uint32_t audiosink_period = 0;
    static uint32_t audiosink_period_cnt = 0;
    static uint32_t audiosink_period_sum = 0;
    float average_period;
    uint8_t i;

    /* Record period count value and add it to the total sum*/
    audiosink_period = Sys_Audiosink_PeriodCounter();
    audiosink_period_cnt++;
    audiosink_period_sum += audiosink_period;

#if (LOW_POWER_CLK_UPDATE == LOW_POWER_CLK_UPDATE_DISABLE)

    /* Allow the RC clock period to be set once */
    if (low_power_clk_param.dynamic_measurement_enable == false)

#endif    /* if LOW_POWER_CLK_UPDATE */
    {
        if (audiosink_period_cnt == num_measurement)
        {
            /* Calculate the average period for the number of audiosink cycles,
             * each taking audiosink_period_cnt samples */
            average_period = (audiosink_period_sum /
                             (audiosink_period_cnt * LOW_POWER_CLK_SCALE_AVERAGE_PERIOD));

            /* Reset our total sum and count */
            audiosink_period_cnt = 0;
            audiosink_period_sum = 0;

            /* On first iteration make the previous average period value the
             * same as the current average value */
            if (low_power_clk_param.dynamic_measurement_enable == false)
            {
                measure_buf[buf_cnt] = average_period;
                buf_cnt = ((buf_cnt + 1) % MAX_BUF_CNT);


                for (i = 0; i < MAX_BUF_CNT; i++)
                {
                    measure_buf[i] = average_period;
                }
            }

            else
            {
                measure_buf[buf_cnt] = average_period;
                buf_cnt = ((buf_cnt + 1) % MAX_BUF_CNT);

                float max = measure_buf[0];
                float min = measure_buf[0];
                for (i = 1; i < MAX_BUF_CNT; i++ )
                {
                    if(measure_buf[i] > max)
                    {
                        max = measure_buf[i];
                    }
                    else if (measure_buf[i] < min)
                    {
                        min = measure_buf[i];
                    }
                }

                average_period = 0;
                for (i = 0; i < MAX_BUF_CNT; i++ )
                {
                    average_period = (average_period + measure_buf[i]);
                }

                average_period = (average_period - min - max);
                average_period = (average_period / (MAX_BUF_CNT - 2));
            }

            NVIC_DisableIRQ(AUDIOSINK_PERIOD_IRQn);

            if (RTC_CLK_SRC == RTC_CLK_SRC_RC_OSC)
            {
                RTCCLK_Period_Value_Set(average_period * 1.00035);
            }
            else
            {
               RTCCLK_Period_Value_Set(average_period);
            }

            /* Allow the device to go into sleep mode */
            low_power_clk_param.low_power_enable = true;

            /* Enable dynamic measurements */
#if (LOW_POWER_CLK_UPDATE == LOW_POWER_CLK_UPDATE_DISABLE)
            low_power_clk_param.dynamic_measurement_enable = true;
#endif

        }
    }

    AUDIOSINK->PERIOD_CNT = 0;

    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = 1;
}
#endif /* 0 */
#else
/* ----------------------------------------------------------------------------
 * Copyright (c) 2015-2017 Semiconductor Components Industries, LLC (d/b/a
 * ON Semiconductor), All Rights Reserved
 *
 * Copyright (C) RivieraWaves 2009-2016
 *
 * This module is derived in part from example code provided by RivieraWaves
 * and as such the underlying code is the property of RivieraWaves [a member
 * of the CEVA, Inc. group of companies], together with additional code which
 * is the property of ON Semiconductor. The code (in whole or any part) may not
 * be redistributed in any form without prior written permission from
 * ON Semiconductor.
 *
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 *
 * This is Reusable Code.
 *
 * ----------------------------------------------------------------------------
 * app_process.c
 * - Application task handler definition and support processes
 * ----------------------------------------------------------------------------
 * $Revision: 1.62 $
 * $Date: 2019/09/06 19:54:47 $
 * ------------------------------------------------------------------------- */

#include "app.h"

/* Parameters for RC Oscillator period measurements */
volatile uint32_t loop_cnt = 0;



const struct ke_task_desc TASK_DESC_APP =
{
    NULL,
    &appm_default_handler,
    appm_state,
    APPM_STATE_MAX,
    APP_IDX_MAX
};

/* State and event handler definition */
const struct ke_msg_handler appm_default_state[] =
{
    /* Note: Put the default handler on top as this is used for handling any
     *       messages without a defined handler */
    { KE_MSG_DEFAULT_HANDLER, (ke_msg_func_t)Msg_Handler },
    BLE_MESSAGE_HANDLER_LIST,
    BASS_MESSAGE_HANDLER_LIST,
    CS_MESSAGE_HANDLER_LIST
   // APP_MESSAGE_HANDLER_LIST
};

/* Use the state and event handler definition for all states. */
const struct ke_state_handler appm_default_handler
    = KE_STATE_HANDLER(appm_default_state);

/* Defines a place holder for all task instance's state */
ke_state_t appm_state[APP_IDX_MAX];

/* ----------------------------------------------------------------------------
 * Function      : void Sleep_Mode_Configure(
 *                         struct sleep_mode_env_tag *sleep_mode_env)
 * ----------------------------------------------------------------------------
 * Description   : Configure the sleep mode
 * Inputs        : Pre-defined parameters and configurations
 *                 for the sleep mode
 * Outputs       : sleep_mode_env   - Parameters and configurations
 *                                    for the sleep mode
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Sleep_Mode_Configure(struct sleep_mode_env_tag *sleep_mode_env)
{
    struct sleep_mode_init_env_tag sleep_mode_init_env;

    /* Set the clock source for RTC */
    sleep_mode_init_env.rtc_ctrl = RTC_CLK_SRC;

    /* if RTC clock source is XTAL 32 kHz oscillator */
    if (RTC_CLK_SRC == RTC_CLK_SRC_XTAL32K)
    {
        /* Enable XTAL32K oscillator amplitude control
         * Set XTAL32K load capacitance to 0x38: 22.4 pF
         * Enable XTAL32K oscillator */
        ACS->XTAL32K_CTRL = \
            (XTAL32K_XIN_CAP_BYPASS_DISABLE                                |
             XTAL32K_AMPL_CTRL_ENABLE                                      |
             XTAL32K_NOT_FORCE_READY                                       |
             (XTAL32K_CLOAD_TRIM_VALUE << ACS_XTAL32K_CTRL_CLOAD_TRIM_Pos) |
             (XTAL32K_ITRIM_VALUE << ACS_XTAL32K_CTRL_ITRIM_Pos)           |
             XTAL32K_IBOOST_DISABLE                                        |
             XTAL32K_ENABLE);

        /* Wait for XTAL32K oscillator to be ready */
        while (ACS_XTAL32K_CTRL->READY_ALIAS != XTAL32K_OK_BITBAND);

        LowPowerClock_Source_Set(0);
    }

    /* else: if RTC clock source is RC 32 kHz oscillator */
    else if (RTC_CLK_SRC == RTC_CLK_SRC_RC_OSC)
    {
        /* Start the RC oscillator */
        Sys_Clocks_Osc32kHz(RC_OSC_ENABLE | RC_OSC_NOM);

        /* Read the OSC_32K calibration trim data from NVR4 */
        unsigned int osc_calibration_value = 0;
        Sys_ReadNVR4(MANU_INFO_OSC_32K, 1, (unsigned
                                            int *)&osc_calibration_value);

        /* Use calibrated value for RC clock */
        if (osc_calibration_value != 0xFFFFFFFF)
        {
            ACS_RCOSC_CTRL->FTRIM_32K_BYTE = (uint8_t)(osc_calibration_value);
        }

        LowPowerClock_Source_Set(1);
#if 1
        /* In us, for typical RCOSC until measurement is obtained. */
        RTCCLK_Period_Value_Set(RCCLK_PERIOD_VALUE);

        /* Delay for 4 ms */
        Sys_Delay_ProgramROM(4 * (SystemCoreClock / 1000));

        /* Set-up the Audiosink block for frequency measurement */
        Sys_Audiosink_ResetCounters();
        Sys_Audiosink_InputClock(0, AUDIOSINK_CLK_SRC_STANDBYCLK);
        Sys_Audiosink_Config(AUDIO_SINK_PERIODS_16, 0, 0);

        /* Enable interrupts */
        NVIC_ClearPendingIRQ(AUDIOSINK_PERIOD_IRQn);
        NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

        /* Start period counter to start period measurement */
        AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = 1;
#endif
    }

    /* else: if RTC clock source is external oscillator */
    else
    {
        DIO->CFG[EXT_LOW_POWER_CLK_GPIO_NUM] = (DIO_2X_DRIVE     |
                                                DIO_LPF_DISABLE  |
                                                DIO_NO_PULL      |
                                                DIO_MODE_INPUT);

        LowPowerClock_Source_Set(1);

        /* Clock period in us for external clock */
        RTCCLK_Period_Value_Set(EXT_LOW_POWER_CLK_PERIOD_VALUE);

        /* Set-up the Audiosink block for frequency measurement */
        Sys_Audiosink_ResetCounters();
        Sys_Audiosink_InputClock(0, AUDIOSINK_CLK_SRC_STANDBYCLK);
        Sys_Audiosink_Config(AUDIO_SINK_PERIODS_16, 0, 0);

        /* Enable interrupts */
        NVIC_ClearPendingIRQ(AUDIOSINK_PERIOD_IRQn);
        NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

        /* Start period counter to start period measurement */
        AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = 1;

    }

    /* Set delay and wake-up sources, use
     *    WAKEUP_DELAY_[ 1 | 2 | 4 | ... | 128],
     *    WAKEUP_DCDC_OVERLOAD_[ENABLE | DISABLE],
     *    WAKEUP_WAKEUP_PAD_[RISING | FALLING],
     *    WAKEUP_DIO*_[RISING | FALLING],
     *    WAKEUP_DIO*_[ENABLE | DISABLE] */
    sleep_mode_init_env.wakeup_cfg = WAKEUP_DELAY_32          |
                                     WAKEUP_WAKEUP_PAD_RISING |
                                     (WAKEUP_DIO0_ENABLE << WAKEUP_DIO);

    /* Set wake-up control/status registers, use
     *    PADS_RETENTION_[ENABLE | DISABLE],
     *    BOOT_FLASH_APP_REBOOT_[ENABLE | DISABLE],
     *    BOOT_[CUSTOM | FLASH_XTAL_*],
     *    WAKEUP_DCDC_OVERLOAD_CLEAR,
     *    WAKEUP_PAD_EVENT_CLEAR,
     *    WAKEUP_RTC_ALARM_CLEAR,
     *    WAKEUP_BB_TIMER_CLEAR,
     *    WAKEUP_DIO3_EVENT_CLEAR,
     *    WAKEUP_DIO2_EVENT_CLEAR,
     *    WAKEUP_DIO1_EVENT_CLEAR],
     *    WAKEUP_DIO0_EVENT_CLEAR */
    sleep_mode_env->wakeup_ctrl = PADS_RETENTION_ENABLE         |
                                  BOOT_FLASH_APP_REBOOT_DISABLE |
                                  BOOT_CUSTOM;

    /* Set wake-up application start address (LSB must be set) */
    sleep_mode_init_env.app_addr =
        (uint32_t)(&Wakeup_From_Sleep_Application_asm) | 1;

    /* Set wake-up restore address */
    sleep_mode_init_env.wakeup_addr = (uint32_t)(DRAM2_TOP + 1 -
                                                 POWER_MODE_WAKEUP_INFO_SIZE);

    /* Configure memory retention */
    sleep_mode_env->mem_power_cfg = (DRAM0_POWER_ENABLE |
                                     DRAM1_POWER_ENABLE |
                                     DRAM2_POWER_ENABLE |
                                     BB_DRAM0_POWER_ENABLE);

    /* Configure memory at wake-up (PROM must be part of this) */
    sleep_mode_init_env.mem_power_cfg_wakeup = (PROM_POWER_ENABLE  |
                                                DRAM0_POWER_ENABLE |
                                                DRAM1_POWER_ENABLE |
                                                DRAM2_POWER_ENABLE |
                                                BB_DRAM0_POWER_ENABLE);

    /* Set DMA channel used to save/restore RF registers
     * in each sleep/wake-up cycle */
    sleep_mode_init_env.DMA_channel_RF = DMA_CHAN_SLP_WK_RF_REGS_COPY;

    /* Set VDDxRet Trim Values */
    sleep_mode_init_env.VDDTRET_trim = RTE_VDDTRET_TRIM_VALUE;
    sleep_mode_init_env.VDDMRET_trim = RTE_VDDMRET_TRIM_VALUE;
    sleep_mode_init_env.VDDCRET_trim = RTE_VDDCRET_TRIM_VALUE;

    /* Perform initializations required for sleep mode */
#ifdef APP_SLEEP_2MBPS_SUPPORT
    Sys_PowerModes_Sleep_Init_2Mbps(&sleep_mode_init_env);
#else    /* ifdef APP_SLEEP_2MBPS_SUPPORT */
    Sys_PowerModes_Sleep_Init(&sleep_mode_init_env);
#endif    /* ifdef APP_SLEEP_2MBPS_SUPPORT */

    /* Configure DIO0 IRQ interrupt with WAKEUP_DIO as the source. */
    Sys_DIO_Config(WAKEUP_DIO, DIO_MODE_GPIO_IN_0);
    Sys_DIO_IntConfig(0, (WAKEUP_DIO << DIO_INT_CFG_SRC_Pos) | DIO_EVENT_RISING_EDGE, 0, 0);

    /* Clear Pending DIO0 IRQ and Wakeup IRQ*/
    NVIC_ClearPendingIRQ(DIO0_IRQn);
    NVIC_ClearPendingIRQ(WAKEUP_IRQn);

    /* Enable DIO0 IRQ and Wakeup IRQ*/
    NVIC_EnableIRQ(DIO0_IRQn);
    NVIC_EnableIRQ(WAKEUP_IRQn);

    /* Clear all wakeup flags for enabled wakeup sources */
    ACS->WAKEUP_CTRL |= CLEAR_ALL_ENABLED_WAKEUP_FLAGS;
}

/* ----------------------------------------------------------------------------
 * Function      : void Wakeup_From_Sleep_Application(void)
 * ----------------------------------------------------------------------------
 * Description   : Restore system states from retention RAM and continue
 *                 application from flash
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Wakeup_From_Sleep_Application(void)
{
    /* Execute steps required to wake-up the system from sleep mode */
#ifdef APP_SLEEP_2MBPS_SUPPORT
    Sys_PowerModes_Wakeup_2Mbps();
#else    /* ifdef APP_SLEEP_2MBPS_SUPPORT */
    Sys_PowerModes_Wakeup();
#endif    /* ifdef APP_SLEEP_2MBPS_SUPPORT */

    /* The system is awake from this point, continue application from flash */
    Continue_Application();

    /* Re-Enable Wakeup IRQ and DIO0 IRQ*/
    NVIC_EnableIRQ(WAKEUP_IRQn);
    NVIC_EnableIRQ(DIO0_IRQn);
   // RM_Enable(500);
    /* Return to main application loop */
    Main_Loop();
}

/* ----------------------------------------------------------------------------
 * Function      : void Continue_Application(void)
 * ----------------------------------------------------------------------------
 * Description   : Restore application states, wait until BLE is awake and
 *                 go to the main loop
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Continue_Application(void)
{
    /* Lower drive strength (required when VDDO > 2.7)*/
    DIO->PAD_CFG = PAD_LOW_DRIVE;

    /* Turn LED on */
    Sys_DIO_Config(LED_DIO, DIO_MODE_GPIO_OUT_1);

    /* Disable DIO4 and DIO5 to avoid current consumption on VDDO */
    Sys_DIO_Config(4, DIO_MODE_DISABLE | DIO_NO_PULL);
    Sys_DIO_Config(5, DIO_MODE_DISABLE | DIO_NO_PULL);

    /* Configure DIO0 IRQ interrupt with WAKEUP_DIO as the source. */
    Sys_DIO_Config(WAKEUP_DIO, DIO_MODE_GPIO_IN_0);
    Sys_DIO_IntConfig(0, (WAKEUP_DIO << DIO_INT_CFG_SRC_Pos) | DIO_EVENT_RISING_EDGE, 0, 0);

    /* Turn off pad retention */
    ACS_WAKEUP_CTRL->PADS_RETENTION_EN_BYTE = PADS_RETENTION_DISABLE_BYTE;

    /* Configure ADC channel 0 to measure VBAT/2 */
    Sys_ADC_Set_Config(ADC_VBAT_DIV2_NORMAL | ADC_NORMAL |
                       ADC_PRESCALE_200);
    Sys_ADC_InputSelectConfig(0, (ADC_NEG_INPUT_GND |
                                  ADC_POS_INPUT_VBAT_DIV2));

    /* Configure clock dividers */
    CLK->DIV_CFG0 = SLOWCLK_PRESCALE_VALUE | BBCLK_PRESCALE_VALUE |
                    USRCLK_PRESCALE_1;
    CLK_DIV_CFG2->DCCLK_BYTE = DCCLK_BYTE_VALUE;

    /* Update Flash timing */
    FLASH->DELAY_CTRL = DEFAULT_READ_MARGIN | FLASH_DELAY_VALUE;

    /* Switch to RF clock */
    CLK_SYS_CFG->SYSCLK_SRC_SEL_BYTE = SYSCLK_CLKSRC_RFCLK_BYTE;

    /* Configure the baseband divider and force wake-up in case it is required
     * due to an early ACS wake-up condition (e.g. PAD, RTC) */
    BBIF->CTRL = BB_CLK_ENABLE | BBCLK_DIVIDER_VALUE | BB_WAKEUP;

    /* Disable interrupts */
    __disable_irq();
    while (!(BLE_Is_Awake()))
    {
        SYS_WAIT_FOR_INTERRUPT;

        /* Enable interrupts */
        __enable_irq();

        /* Allow pended interrupts to be recognized */
        __ISB();

        /* Disable interrupts */
        __disable_irq();
    }

    if (RTC_CLK_SRC != RTC_CLK_SRC_XTAL32K)
    {
#if (LOW_POWER_CLK_UPDATE == LOW_POWER_CLK_UPDATE_ENABLE)
        Enable_Audiosink_Measurement();
#endif
    }

    /* Stop masking interrupts */
    __enable_irq();

    /* Stop forcing baseband wake-up */
    BBIF->CTRL = BB_CLK_ENABLE | BBCLK_DIVIDER_VALUE | BB_DEEP_SLEEP;
    ACS->WAKEUP_CTRL |= WAKEUP_BB_TIMER_CLEAR;
}

/* ----------------------------------------------------------------------------
 * Function      : void Enable_Audiosink_Measurement(void)
 * ----------------------------------------------------------------------------
 * Description   : - Wait for approximately RC_OSC_MEASUREMENT_INTERVAL seconds
 *                   before enabling audiosink interrupt.
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Enable_Audiosink_Measurement(void)
{
    /* The number of times we wait for the device to go through a wake-
     * sleep cycle */
    uint16_t num_wakeup;

    /* num_wakeup cycles should be determined by the connection interval
     * of peer device */
    /* Update RC_OSC period every num_wakeup cycles of sleep. */
    if(ble_env.state == APPM_CONNECTED)
    {
    	num_wakeup = ((LOW_POWER_CLK_MEASUREMENT_INTERVAL_S *
                   LOW_POWER_CLK_SCALE_MEASUREMENT_INTERVAL) /
                  (ble_env.actual_con_interval * (ble_env.actual_con_latency + 1)));
    }
    else
    {
    	num_wakeup = (LOW_POWER_CLK_MEASUREMENT_INTERVAL_S * 1600 / ADV_INT_CONNECTABLE_MODE);
    }


    if(num_wakeup == 0)
    {
    	num_wakeup = 1;
    }

    /* Update RC_OSC period every num_wakeup cycles of sleep. */
    loop_cnt++;
    if ((loop_cnt % num_wakeup) == 0)
    {
        /* Set-up the Audiosink block for frequency measurement */
        Sys_Audiosink_ResetCounters();
        Sys_Audiosink_InputClock(0, AUDIOSINK_CLK_SRC_STANDBYCLK);
        Sys_Audiosink_Config(AUDIO_SINK_PERIODS_16, 0, 0);

        /* Enable interrupts */
        NVIC_ClearPendingIRQ(AUDIOSINK_PERIOD_IRQn);
        NVIC_EnableIRQ(AUDIOSINK_PERIOD_IRQn);

        /* Start period counter to start period measurement */
        AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = 1;

        /*Disable sleep mode */
#if (LOW_POWER_CLK_UPDATE == LOW_POWER_CLK_UPDATE_DISABLE)
        if (low_power_clk_param.dynamic_measurement_enable == false)
#endif    /* if !RC_OSC_UPDATE */
        {
            low_power_clk_param.low_power_enable = false;
        }
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void Measure_Battery_Level(void)
 * ----------------------------------------------------------------------------
 * Description   : - Read the battery level using ADC, calculate and update
 *                   its average value when applicable
 *                 - If the average value changes, set the notification flag
 *                   for battery service
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Measure_Battery_Level(void)
{
    uint16_t level;

    /* Calculate the battery level as a percentage, scaling the battery
     * voltage between 1.4V (max) and 1.1V (min) */
    level = ((ADC->DATA_TRIM_CH[0] - VBAT_1P1V_MEASURED) * BAT_LVL_MAX
             / (VBAT_1P4V_MEASURED - VBAT_1P1V_MEASURED));
    level = ((level > BAT_LVL_MAX) ? BAT_LVL_MAX : level);

    /* Add to the current sum and increment the number of reads */
    app_env.sum_batt_lvl += level;
    app_env.num_batt_read++;

    /* Calculate the average over the past 16 voltage reads */
    if (app_env.num_batt_read == 16)
    {
        if ((app_env.sum_batt_lvl >> 4) != app_env.batt_lvl)
        {
            app_env.send_batt_ntf = 1;

            /* Update the average value of battery level */
            app_env.batt_lvl = (app_env.sum_batt_lvl >> 4);
        }

        /* Reset parameters for the next round of battery measurement */
        app_env.num_batt_read = 0;
        app_env.sum_batt_lvl = 0;
    }
}

/* ----------------------------------------------------------------------------
 * Function      : uint8_t Emulate_CS_Val_Notif_Change(uint8_t val_notif)
 * ----------------------------------------------------------------------------
 * Description   : Emulate the change of custom service notification data
 * Inputs        : - val_notif  - value of custom service notification
 * Outputs       : return value - updated value of custom service notification
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
uint8_t Emulate_CS_Val_Notif_Change(uint8_t val_notif)
{
    val_notif++;
    if ((val_notif & 0x0F) == 0x0A)
    {
        val_notif = val_notif & 0xF0;
        val_notif = val_notif + 0x10;
        if ((val_notif & 0xF0) == 0xA0)
        {
            val_notif = val_notif & 0x0F;
        }
    }

    return (val_notif);
}

/* ----------------------------------------------------------------------------
 * Function      : int Msg_Handler(ke_msg_id_t const msg_id,
 *                                 void const *param,
 *                                 ke_task_id_t const dest_id,
 *                                 ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle any message received from kernel that doesn't have
 *                 a dedicated handler
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameter (unused)
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int Msg_Handler(ke_msg_id_t const msg_id, void *param,
                ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    return (KE_MSG_CONSUMED);
}

#if 0
/* ----------------------------------------------------------------------------
 * Function      : void AUDIOSINK_PERIOD_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : Calculates the average period for every 16 measurements and
 *                 then averages RC_OSC_INITIAL_MEASUREMENT number of those
 *                 measurements before updating the RC oscillator.
 *                 Subsequent updates are done after RC_OSC_DYNAMIC_MEAUREMENT
 *                 number of measurements.
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : Using 8 MHz System core clock.
 * ------------------------------------------------------------------------- */
void AUDIOSINK_PERIOD_IRQHandler(void)
{
    /* Parameters for RC oscillator period measurements */
    static uint32_t num_measurement = LOW_POWER_CLK_INITIAL_MEASUREMENT;
    static uint32_t audiosink_period = 0;
    static uint32_t audiosink_period_cnt = 0;
    static uint32_t audiosink_period_sum = 0;
    float average_period;
    uint8_t i;

    /* Record period count value and add it to the total sum*/
    audiosink_period = Sys_Audiosink_PeriodCounter();
    audiosink_period_cnt++;
    audiosink_period_sum += audiosink_period;

#if (LOW_POWER_CLK_UPDATE == LOW_POWER_CLK_UPDATE_DISABLE)

    /* Allow the RC clock period to be set once */
    if (low_power_clk_param.dynamic_measurement_enable == false)

#endif    /* if LOW_POWER_CLK_UPDATE */
    {
        if (audiosink_period_cnt == num_measurement)
        {
            /* Calculate the average period for the number of audiosink cycles,
             * each taking audiosink_period_cnt samples */
            average_period = (audiosink_period_sum /
                             (audiosink_period_cnt * LOW_POWER_CLK_SCALE_AVERAGE_PERIOD));

            /* Reset our total sum and count */
            audiosink_period_cnt = 0;
            audiosink_period_sum = 0;

            /* On first iteration make the previous average period value the
             * same as the current average value */
            if (low_power_clk_param.dynamic_measurement_enable == false)
            {
                measure_buf[buf_cnt] = average_period;
                buf_cnt = ((buf_cnt + 1) % MAX_BUF_CNT);


                for (i = 0; i < MAX_BUF_CNT; i++)
                {
                    measure_buf[i] = average_period;
                }
            }

            else
            {
                measure_buf[buf_cnt] = average_period;
                buf_cnt = ((buf_cnt + 1) % MAX_BUF_CNT);

                float max = measure_buf[0];
                float min = measure_buf[0];
                for (i = 1; i < MAX_BUF_CNT; i++ )
                {
                    if(measure_buf[i] > max)
                    {
                        max = measure_buf[i];
                    }
                    else if (measure_buf[i] < min)
                    {
                        min = measure_buf[i];
                    }
                }

                average_period = 0;
                for (i = 0; i < MAX_BUF_CNT; i++ )
                {
                    average_period = (average_period + measure_buf[i]);
                }

                average_period = (average_period - min - max);
                average_period = (average_period / (MAX_BUF_CNT - 2));
            }

            NVIC_DisableIRQ(AUDIOSINK_PERIOD_IRQn);

            if (RTC_CLK_SRC == RTC_CLK_SRC_RC_OSC)
            {
                RTCCLK_Period_Value_Set(average_period * 1.00035);
            }
            else
            {
               RTCCLK_Period_Value_Set(average_period);
            }

            /* Allow the device to go into sleep mode */
            low_power_clk_param.low_power_enable = true;

            /* Enable dynamic measurements */
#if (LOW_POWER_CLK_UPDATE == LOW_POWER_CLK_UPDATE_DISABLE)
            low_power_clk_param.dynamic_measurement_enable = true;
#endif

        }
    }

    AUDIOSINK->PERIOD_CNT = 0;

    AUDIOSINK_CTRL->PERIOD_CNT_START_ALIAS = 1;
}
#endif
/* ----------------------------------------------------------------------------
 * Function      : void DIO0_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : Process the DIO0 interrupts
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DIO0_IRQHandler(void)
{

}

/* ----------------------------------------------------------------------------
 * Function      : void DIO0_Wakeup_Process_Handler(void)
 * ----------------------------------------------------------------------------
 * Description   : Gets called when DIO0 triggered wake-up events
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void DIO0_Wakeup_Process_Handler(void)
{
    NVIC_SetPendingIRQ(DIO0_IRQn);
}

/* ----------------------------------------------------------------------------
 * Function      : void WAKEUP_IRQHandler(void)
 * ----------------------------------------------------------------------------
 * Description   : Process the wake-up interrupts
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void WAKEUP_IRQHandler(void)
{
    /* Check if BB Timer wakeup event is set. If so, clear BB wakeup event*/
    if (ACS->WAKEUP_CTRL & WAKEUP_BB_TIMER_EVENT_SET )
    {
        ACS->WAKEUP_CTRL |= WAKEUP_BB_TIMER_CLEAR;
    }

    /* Check if DIO0 wakeup event set */
    if (ACS->WAKEUP_CTRL & WAKEUP_DIO0_EVENT_SET )
    {
        /* Call DIO0 event handler */
        DIO0_Wakeup_Process_Handler();

        /* Clear DIO0 wakeup event */
        ACS->WAKEUP_CTRL |= WAKEUP_DIO0_EVENT_CLEAR;
    }
}

extern uint32_t bb_registers_image[sizeof(BB_Type) / 4];

/* ----------------------------------------------------------------------------
 * Function      : void Sys_PowerModes_Sleep(
 *                        struct sleep_mode_env_tag *sleep_mode_env)
 * ----------------------------------------------------------------------------
 * Description   : Configure the system, save register and memory banks
 *                 of the BLE, then enter Sleep Mode
 * Inputs        : sleep_mode_env    - Parameters and configurations
 *                                     for the Sleep Mode
 * Outputs       : None
 * Assumptions   : It is safe to enter Sleep Mode (this should be checked
 *                 before calling this function), DMA channel 0 is available
 * ------------------------------------------------------------------------- */
void Sys_PowerModes_Sleep(struct sleep_mode_env_tag *sleep_mode_env)
{
    uint32_t nvic_iser0, nvic_iser1, nvic_iser2;

   /* Back up NVIC configuration */
    nvic_iser0 = NVIC->ISER[0];
    nvic_iser1 = NVIC->ISER[1];
    nvic_iser2 = NVIC->ISER[2];

    /* Disable NVIC */
    Sys_NVIC_DisableAllInt();

    /* Request the baseband low power timer to go
     * into deep sleep mode (takes a few 32kHz cycles) */
    *((volatile uint16_t *)&BB->DEEPSLCNTL) = OSC_SLEEP_EN_1   |
                                              RADIO_SLEEP_EN_1 |
                                              DEEP_SLEEP_ON_1;

    /* Initialize SYSTICK counter value (32 us/step: 3 => 112 us +/- 16 us) */
    SysTick->LOAD = 3;

    /* Start the SYSTICK counter */
    SysTick->CTRL = SYSTICK_ENABLE | SYSTICK_TICKINT_ENABLE | SYSTICK_CLKSOURCE_EXTREF_CLK;

    /* Disable all unused memories */
    SYSCTRL->MEM_POWER_CFG = sleep_mode_env->mem_power_cfg;

    /* Enable boot on RAM and clear wake-up event register */
    ACS->WAKEUP_CTRL = sleep_mode_env->wakeup_ctrl;

    /* Setup and start DMA channel to save BB registers to retention memory */

    /* Setup the base addresses for the source and destination */
    DMA->SRC_BASE_ADDR[0] = (uint32_t) BB_BASE;
    DMA->DEST_BASE_ADDR[0] = (uint32_t) bb_registers_image;

    /* Setup the transfer length and transfer counter interrupt setting */
    DMA->CTRL1[0] = (sizeof(BB_Type) / 4) << DMA_CTRL1_TRANSFER_LENGTH_Pos;

    /* Configure and start the DMA channel */
    DMA->CTRL0[0] = DMA_DISABLE_INT_DISABLE  |
                    DMA_ERROR_INT_DISABLE    |
                    DMA_COMPLETE_INT_DISABLE |
                    DMA_COUNTER_INT_DISABLE  |
                    DMA_START_INT_DISABLE    |
                    DMA_DEST_WORD_SIZE_32    |
                    DMA_SRC_WORD_SIZE_32     |
                    DMA_SRC_PBUS             |
                    DMA_PRIORITY_1           |
                    DMA_TRANSFER_P_TO_M      |
                    DMA_SRC_ADDR_INC         |
                    DMA_DEST_ADDR_INC        |
                    DMA_ADDR_LIN             |
                    DMA_ENABLE;

    /* Wait for SYSTICK interrupt
     * (to avoid continuously polling the DMA_CTRL0 and BBIF_STATUS) */
    SYS_WAIT_FOR_INTERRUPT;

    /* Acknowledge SYSTICK interrupt manually */
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;

    /* Disable the SYSTICK interrupt and timer */
    SysTick->CTRL = SYSTICK_DISABLE;

    /* Wait until DMA is completed */
    while (DMA_CTRL0->ENABLE_ALIAS);

    /* Wait until the baseband has switched to the low power clock */
    while (BBIF_STATUS->CLK_STATUS_ALIAS == MASTER_CLK_BITBAND);

    /* Switch the system clock to RC OSC */
    CLK_SYS_CFG->SYSCLK_SRC_SEL_BYTE = SYSCLK_CLKSRC_RCCLK_BYTE;

    /* Disable the RF front-end (access is automatically removed) */
    SYSCTRL->RF_POWER_CFG = RF_POWER_DISABLE;

    /* Lower DC-DC max current to 16 mA minimize in-rush current */
    ACS_VCC_CTRL->ICH_TRIM_BYTE = VCC_ICHTRIM_16MA_BYTE;

    /* Ensure that the DEEPSLCNTL baseband register (0x30) is reset
     * (to prevent the baseband going back to sleep at wake-up) */
    *((uint8_t *)&bb_registers_image[0x30 / 4]) = OSC_SLEEP_EN_0 | RADIO_SLEEP_EN_0 | DEEP_SLEEP_ON_0;

    if ((ACS->WAKEUP_CTRL & WAKEUP_EVENT_SET_FLAGS) == 0)
    {
        /* Wait until the baseband low power timer is in deep sleep mode
         * and properly isolated */
        while (BBIF_STATUS->OSC_EN_ALIAS == OSC_ENABLED_BITBAND);

        /* Enter Sleep Mode (becomes effective after WFI instruction) */
        ACS->PWR_MODES_CTRL = PWR_SLEEP_MODE;

        /* Wait for interrupt */
        SYS_WAIT_FOR_INTERRUPT;
    }
    else
    {
        /* If the processor does not enter sleep due to a pending wakeup event,
         * start the wakeup process */
#ifdef APP_SLEEP_2MBPS_SUPPORT
        Sys_PowerModes_Wakeup_2Mbps();
#else
        Sys_PowerModes_Wakeup();
#endif
        /* Resume with the application */
        Continue_Application();

        /* Restore NVIC */
        NVIC->ISER[0] = nvic_iser0;
        NVIC->ISER[1] = nvic_iser1;
        NVIC->ISER[2] = nvic_iser2;
    }
}

#endif
