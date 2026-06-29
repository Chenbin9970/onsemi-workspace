/* ----------------------------------------------------------------------------
 * Copyright (c) 2019 Semiconductor Components Industries, LLC (d/b/a
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
 * rsl10_sys_rffe.c
 * - Library that provides functions to calibrate the internal power supplies
 * ----------------------------------------------------------------------------
 * $Revision: 1.49 $
 * $Date: 2019/06/03 19:39:00 $
 * ------------------------------------------------------------------------- */

#include <rsl10.h>

/* ----------------------------------------------------------------------------
 * IMacro: CONVERT(t)
 * ----------------------------------------------------------------------------
 * Description   : Converts an adc code to a voltage, calculated as follows:
 *                 voltage = adc_code * (2 V * 1000 [mV*10]/1 V / 2^14.)
 * Inputs        : t               - The ADC code input
 * Outputs       : return value    - The voltage output in mV*10
 * Assumptions   : Low frequency mode for the ADC is used, meaning that the
 *                 resolution of the ADC is 14-bits. CONVERT provides voltage
 *                 level as a milliVolt value based on the input ADC code.
 * ------------------------------------------------------------------------- */
#define CONVERT(t)                      ((uint32_t)((t * 1000) >> 13))

/* ----------------------------------------------------------------------------
 * IMacro: LINEAR_INTERPOLATE_PAPWR(t)
 * ----------------------------------------------------------------------------
 * Description   : Applies the linear interpolation formula for PAPWR
 *                 interpolation at point
 * Inputs        : t               - The interpolation point
 * Outputs       : return value    - The target voltage in 10*mV
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
#define LINEAR_INTERPOLATE_PAPWR(t)     (RFFE_PAPWR_LICOEFF_ALPHA * t \
                                         + RFFE_PAPWR_LICOEFF_BETA)

/* ----------------------------------------------------------------------------
 * Internal function prototype
 * ------------------------------------------------------------------------- */
unsigned int Sys_RFFE_Interpolate(uint8_t target, bool vddrf_vddpa);

/* Known and linearly interpolated VDDPA values in *10mV*/
const uint8_t targetVoltage[7] = { 107U, 113U, 120U, 126U, 135U, 145U, 160U };
const uint8_t targetTrim[7] = {
    VDDRF_TRIM_1P07V_BYTE, VDDRF_TRIM_1P13V_BYTE,
    VDDRF_TRIM_1P20V_BYTE, VDDPA_TRIM_1P26V_BYTE,
    VDDPA_TRIM_1P35V_BYTE, VDDPA_TRIM_1P45V_BYTE,
    VDDPA_TRIM_1P60V_BYTE
};

enum VDDRF_VDDPA
{
    VDDRF_RES = false,
    VDDPA_RES = true
};

/* ----------------------------------------------------------------------------
 * Function      : unsigned int Sys_RFFE_SetTXPower(int target)
 * ----------------------------------------------------------------------------
 * Description   : Set the TX Power according to the desired target value with
 *                 an accuracy of +/-1 dBm for +6 dBm to -17 dBm. This function
 *                 sets VDDRF, VDDPA, and PA_PWR (RF_REG19) when applicable.
 *
 *                 Note - This function provides RF TX power configurations
 *                        that match the requested levels, without considering
 *                        the potential for increased power consumption due to
 *                        the use of VDDPA.
 *                      - This function uses ADC channel 0 and disables it after use.
 *                      - Ensure this function is called prior to initializing the
 *                        Sleep Mode (or Standby Mode) in applications that use both
 *                        BLE and Sleep (or Standby) functionality so updated
 *                        RF register values can be backed up. See other notes in
 *                        Sys_PowerModes_Sleep_Init(),
 *                        Sys_PowerModes_Sleep_Init_2Mbps() and
 *                        Sys_PowerModes_Standby_Init() functions.
 * Inputs        : target          - Target transmission power in the range
 *                                   from -17 to +6 dBm in 1 dBm increments
 * Outputs       : return value    - ERRNO_NO_ERROR;
 *                                   ERRNO_RFFE_INVALIDSETTING_ERROR: if target
 *                                   is out of the expected range;
 *                                   ERRNO_RFFE_MISSINGSETTING_ERROR: if the
 *                                   device is missing the manufacturing
 *                                   reference trim values in NVR4;
 *                                   ERRNO_RFFE_INSUFFICIENTVCC_ERROR: if the
 *                                   configured VCC target may not be enough to
 *                                   guarantee the expected target TX power.
 *                                   The function might still try to reach the
 *                                   desired target
 *
 * Assumptions   : - The calibrated voltage values exist in device NVR4
 *                 - VCC has been configured to an appropriate level for the
 *                   expected battery level
 * ------------------------------------------------------------------------- */
unsigned int Sys_RFFE_SetTXPower(int target)
{
    int paPwr_trim = 12;
    uint16_t dcdc_expected_trim = 0;
    uint16_t dcdc_actual_trim = 0;
    unsigned int result = 0;
    unsigned int rffe_error = ERRNO_NO_ERROR;
    uint8_t pa_enable = 0;
    uint8_t bank_select_backup;
    uint8_t aout_backup;
    uint32_t VCC_measured = 0;
    unsigned int nvr4_version = 0;
    bool buckIsUsed = ACS_VCC_CTRL->BUCK_ENABLE_ALIAS;

    /* Check version number of NVR4 table */
    Sys_ReadNVR4(MANU_INFO_VERSION, 1, (unsigned int*)&nvr4_version);

    /* Save bank and aout configuration for restoration later */
    bank_select_backup = RF_REG05->BANK_BYTE;

    /* Current VCC trim value for comparison with expected value */
    dcdc_actual_trim = ACS_VCC_CTRL->VTRIM_BYTE;

    /* Initialize PA_PWR trim value for both the memory banks */
    RF_REG05->BANK_BYTE = 0;
    RF_REG19->PA_PWR_BYTE = paPwr_trim;
    RF_REG05->BANK_BYTE = 1;
    RF_REG19->PA_PWR_BYTE = paPwr_trim;
    RF_REG05->BANK_BYTE = bank_select_backup;

    if (target > 6 || target < -17)
    {
        rffe_error = ERRNO_RFFE_INVALIDSETTING_ERROR;
    }
    else if (target < 0)
    {
        result = Sys_Power_VDDRFCalibratedConfig(RFFE_VDDRF_SENSITIVITY_TARGET);

        if (result != ERRNO_NO_ERROR)
        {
            rffe_error = ERRNO_RFFE_MISSINGSETTING_ERROR;
            ACS_VDDRF_CTRL->VTRIM_BYTE = VDDRF_TRIM_1P10V_BYTE;
        }

        /* Linearly interpolate the the trim values for the PA using the
         * theoretical values and set the trim according to target */
        if ((LINEAR_INTERPOLATE_PAPWR(target)) < 0)
        {
            paPwr_trim = ((LINEAR_INTERPOLATE_PAPWR(target)) / 10);

            /* Set the trim value to its max if it's smaller than max */
            if (paPwr_trim < -3)
            {
                paPwr_trim = -3;
            }
            /* Set trim value to an adjusted one for the exceptional case of
             * -20dBm.
             * PA_PWR       Average TX Power (dBm)
             * 0            -17.91
             *-1            -24.45333333
             *-2            -20.88
             */
            else if (paPwr_trim == -1)
            {
                paPwr_trim = -2;
            }
            else if (paPwr_trim == -2)
            {
                paPwr_trim = -1;
            }
        }
        else
        {
            paPwr_trim = (int)(((LINEAR_INTERPOLATE_PAPWR(target)) +
                                (0.5 * 10)) / 10);
        }
        bank_select_backup = RF_REG05->BANK_BYTE;

        /* Set PA_PWR trim value for both the memory banks in case the
         * active memory bank is switched later */
        RF_REG05->BANK_BYTE = 0;
        RF_REG19->PA_PWR_BYTE = paPwr_trim;
        RF_REG05->BANK_BYTE = 1;
        RF_REG19->PA_PWR_BYTE = paPwr_trim;
        RF_REG05->BANK_BYTE = bank_select_backup;
    }
    else if (target >= 0 && target <= 2)
    {
        /* Setup ADC to Measure VDDRF to determine if the VDDRF alone is
         * sufficient or the VDDPA is required for the target transmission
         * value. */
        Sys_ADC_Set_Config(ADC_VBAT_DIV2_NORMAL | ADC_NORMAL | ADC_PRESCALE_200);

        /* Configure ADC channel to measure AOUT */
        Sys_ADC_InputSelectConfig(0, ADC_POS_INPUT_AOUT | ADC_NEG_INPUT_GND);

        /* Save AOUT state for restoration later */
        aout_backup = ACS->AOUT_CTRL;

        /* Output VCC on AOUT */
        ACS->AOUT_CTRL = AOUT_VCC_SENSE;

        /* If target is 2 or 0 set VDDRF using calibrated settings in NVR4 */
        if (target == 2 || target == 0)
        {
            if (target == 0)
            {
                /* VCC threshold trim value to allow VDDRF to be greater than 1.05V */
                result = Sys_GetTrim((uint32_t *)MANU_INFO_DCDC,
                                     RFFE_DCDC_THRESHOLD, &dcdc_expected_trim);

                if (result != ERRNO_NO_ERROR)
                {
                    rffe_error = ERRNO_RFFE_MISSINGSETTING_ERROR;
                    dcdc_expected_trim = VCC_TRIM_1P12V_BYTE;
                }
                else
                {
                    if (nvr4_version >= VCC_VERSION_THRESHOLD)
                    {
                        if (buckIsUsed)
                        {
                            dcdc_expected_trim >>= 8;
                        }
                        dcdc_expected_trim &= 0x1F;
                    }
                }
            }

            else
            {
                /* Expected VCC trim value for a target of 2dBm */
                result = Sys_GetTrim((uint32_t *)MANU_INFO_DCDC,
                                     RFFE_DCDC_2DBM_TARGET, &dcdc_expected_trim);

                if (result != ERRNO_NO_ERROR)
                {
                    rffe_error = ERRNO_RFFE_MISSINGSETTING_ERROR;
                    dcdc_expected_trim = VCC_TRIM_1P25V_BYTE;
                }
                else
                {
                    if (nvr4_version >= VCC_VERSION_THRESHOLD)
                    {
                        if (buckIsUsed)
                        {
                            dcdc_expected_trim >>= 8;
                        }
                        dcdc_expected_trim &= 0x1F;
                    }
                }
            }

            if (dcdc_actual_trim >= dcdc_expected_trim)
            {
                result = Sys_Power_VDDRFCalibratedConfig(targetVoltage[target]);

                if (result != ERRNO_NO_ERROR)
                {
                    rffe_error |= ERRNO_RFFE_MISSINGSETTING_ERROR;
                    ACS_VDDRF_CTRL->VTRIM_BYTE = targetTrim[target];
                }
            }
            else
            {
                rffe_error |= ERRNO_RFFE_INSUFFICIENTVCC_ERROR;

                /* If VCC is not sufficient (VCC < 1.12V), set VDDRF to 1.05V */
                result = Sys_Power_VDDRFCalibratedConfig(RFFE_VDDRF_LOWVCC_TARGET);

                if (result != ERRNO_NO_ERROR)
                {
                    ACS_VDDRF_CTRL->VTRIM_BYTE = VDDRF_TRIM_1P05V_BYTE;
                }
            }
        }

        /* If target is 1, use interpolation to set VDDRF */
        else
        {
            /* Expected VCC trim value for a target of 1dBm */
            result = Sys_GetTrim((uint32_t *)MANU_INFO_DCDC,
                                 RFFE_DCDC_1DBM_TARGET, &dcdc_expected_trim);

            if (result != ERRNO_NO_ERROR)
            {
                rffe_error |= ERRNO_RFFE_MISSINGSETTING_ERROR;
                dcdc_expected_trim = VCC_TRIM_1P20V_BYTE;
            }
            else
                {
                    if (nvr4_version >= VCC_VERSION_THRESHOLD)
                    {
                        if (buckIsUsed)
                        {
                            dcdc_expected_trim >>= 8;
                        }
                        dcdc_expected_trim &= 0x1F;
                    }
                }
            if (dcdc_actual_trim >= dcdc_expected_trim)
            {
                rffe_error |= Sys_RFFE_Interpolate(targetVoltage[target], VDDRF_RES);
            }
            else
            {
                rffe_error |= ERRNO_RFFE_INSUFFICIENTVCC_ERROR;

                /* If VCC is not sufficient (VCC < 1.12V), set VDDRF to 1.05V */
                result = Sys_Power_VDDRFCalibratedConfig(RFFE_VDDRF_LOWVCC_TARGET);

                if (result != ERRNO_NO_ERROR)
                {
                    ACS_VDDRF_CTRL->VTRIM_BYTE = VDDRF_TRIM_1P05V_BYTE;
                }
            }
        }

        /* Measure VCC using the ADC and then convert to *10mV to compare */
        Sys_Delay_ProgramROM(ADC_MEASUREMENT_DELAY);
        VCC_measured = ADC->DATA_TRIM_CH[0];

        /* Convert ADC readings to mV and compare with minimum target */
        VCC_measured = (CONVERT(VCC_measured));

        /* Check VCC to confirm VDDPA doesn't need to be enabled */
        if (dcdc_actual_trim >= dcdc_expected_trim)
        {
            /* Enable VDDPA if VCC is 25 mV below the target voltage */
            if (VCC_measured < ((targetVoltage[target] * 10) + 25))
            {
                if (target == 0)
                {
                    rffe_error |= ERRNO_RFFE_INSUFFICIENTVCC_ERROR;
                }
                else
                {
                    /* In the case of insufficient VDDRF use VDDPA to compensate.
                     * Use linear interpolation to determine the target value of VDDPA. */
                    pa_enable = 1;
                }
            }
        }
        /* If VCC is insufficient, skip VDDRF measurement check and enable
         * VDDPA */
        else
        {
            if (target == 0)
            {
                rffe_error |= ERRNO_RFFE_INSUFFICIENTVCC_ERROR;
            }
            else
            {
                /* In the case of insufficient VDDRF use VDDPA to compensate.
                 * Use linear interpolation to determine the target value of VDDPA. */
                pa_enable = 1;
            }
        }
        Sys_ADC_Set_Config(ADC_VBAT_DIV2_NORMAL | ADC_NORMAL | ADC_DISABLE);
        ACS->AOUT_CTRL = aout_backup;
    }
    else
    {
        pa_enable = 1;
    }

    if (pa_enable == 1)
    {
        /* Target is 3 dBm or higher */
        result = Sys_Power_VDDRFCalibratedConfig(RFFE_VDDRF_SENSITIVITY_TARGET);

        /* Change PA bias from 0x73 to 0xF0 to improve band-edge performance for 3dBm or higher cases */
        RF_REG23->BIAS_0_IQ_RXTX_BYTE = 0xF0;

        if (result != ERRNO_NO_ERROR)
        {
            rffe_error = ERRNO_RFFE_MISSINGSETTING_ERROR;
            ACS_VDDRF_CTRL->VTRIM_BYTE = (VDDRF_TRIM_1P10V_BYTE);
        }

        rffe_error |= Sys_RFFE_Interpolate(targetVoltage[target], VDDPA_RES);

        ACS_VDDA_CP_CTRL->PTRIM_BYTE = VDDA_PTRIM_16MA_BYTE;
        ACS_VDDPA_CTRL->ENABLE_ALIAS = VDDPA_ENABLE_BITBAND;
        ACS_VDDPA_CTRL->VDDPA_SW_CTRL_ALIAS = VDDPA_SW_HIZ_BITBAND;
    }
    else
    {
		RF_REG23->BIAS_0_IQ_RXTX_BYTE = 0x73;
		ACS_VDDA_CP_CTRL->PTRIM_BYTE = VDDA_PTRIM_4MA_BYTE;
        ACS_VDDPA_CTRL->ENABLE_ALIAS = VDDPA_DISABLE_BITBAND;
        ACS_VDDPA_CTRL->VDDPA_SW_CTRL_ALIAS = VDDPA_SW_VDDRF_BITBAND;
    }

    return rffe_error;
}

/* ----------------------------------------------------------------------------
 * IFunction     : unsigned int Sys_RFFE_Interpolate(uint8_t target)
 * ----------------------------------------------------------------------------
 * Description   : Linearly interpolates the required trim value to set VDDPA
 *                 or VDDRF to a target voltage value corresponding to the
 *                 required TX power setting.
 * Inputs        : target          - Target voltage value
 *                 vddrf_vddpa     - Pointer to declare whether to use VDDRF
 *                                   or VDDPA
 * Outputs       : return value    - Interpolated trim value or a status code
 *                 indicating whether the transmission power setting succeeded
 * Assumptions   : The calibrated voltage values exist in device NVR4.
 * ------------------------------------------------------------------------- */
unsigned int Sys_RFFE_Interpolate(uint8_t target, bool vddrf_vddpa)
{
    uint16_t y, y0, y1, x0, x1;
    unsigned int result1 = ERRNO_NO_ERROR;
    unsigned int result2 = ERRNO_NO_ERROR;
    unsigned int result = ERRNO_NO_ERROR;

    /* Use VDDRF calibration values if VDDRF interpolation is required */
    if (vddrf_vddpa == VDDRF_RES)
    {
        /* Known voltage values (*10mV): 107, 120 */
        x0 = targetVoltage[0];
        x1 = targetVoltage[2];

        /* Corresponding trim values for known voltages (*10mV)*/
        result1 = Sys_GetTrim((uint32_t *)MANU_INFO_VDDRF, (uint8_t)x0, &y0);
        result2 = Sys_GetTrim((uint32_t *)MANU_INFO_VDDRF, (uint8_t)x1, &y1);

        if (result1 == ERRNO_GENERAL_FAILURE || result2 == ERRNO_GENERAL_FAILURE)
        {
            y0 = targetTrim[0];
            y1 = targetTrim[2];
            result = ERRNO_RFFE_MISSINGSETTING_ERROR;
        }
    }
    else
    {
        /* Known voltage values (*10mV): 126, 160 */
        x0 = targetVoltage[3];
        x1 = targetVoltage[6];

        /* Corresponding trim values for known voltages (*10mV)*/
        result1 = Sys_GetTrim((uint32_t *)MANU_INFO_VDDPA, (uint8_t)x0, &y0);
        result2 = Sys_GetTrim((uint32_t *)MANU_INFO_VDDPA, (uint8_t)x1, &y1);

        if (result1 == ERRNO_GENERAL_FAILURE || result2 == ERRNO_GENERAL_FAILURE)
        {
            y0 = targetTrim[3];
            y1 = targetTrim[6];

            result = ERRNO_RFFE_MISSINGSETTING_ERROR;
        }
    }

    /* Target voltage value (*10mV) */
    uint8_t x = target;

    /* Interpolation equation to calculate required trim value. The equation
     * is scaled by a factor of 10 to retain 1 decimal accuracy. */
    y = (uint16_t)(((((((x - x0) * (y1 - y0)) * 10) / (x1 - x0)) + (y0 * 10)) + (0.5 * 10))
        / 10);

    if (vddrf_vddpa == VDDRF_RES)
    {
        ACS_VDDRF_CTRL->VTRIM_BYTE = y;
    }
    else
    {
        ACS_VDDPA_CTRL->VTRIM_BYTE = y;
    }

    return (result);
}
