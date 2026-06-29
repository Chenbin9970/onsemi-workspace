/* ----------------------------------------------------------------------------
 * Copyright (c) 2016-2022 Semiconductor Components Industries, LLC (d/b/a
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
 * rsl10_sys_power.c
 * - Library that provides functions to calibrate the internal power supplies
 * ------------------------------------------------------------------------- */

#include <rsl10.h>

/* ----------------------------------------------------------------------------
 * Function      : unsigned int Sys_Power_BandGapCalibratedConfig(uint8_t target)
 * ----------------------------------------------------------------------------
 * Description   : Set the band-gap voltage trim to the given target based on
 *                 the calibration trim value specified in NVR4.
 * Inputs        : target           - The target band-gap voltage in 10*mV
 * Outputs       : return value     - A code indicating whether an error has
 *                                    occurred.
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
unsigned int Sys_Power_BandGapCalibratedConfig(uint8_t target)
{
    uint16_t trim;
    unsigned int result = ERRNO_NO_ERROR;

    result = Sys_GetTrim((uint32_t *)(MANU_INFO_BANDGAP), target, &trim);

    if (result != ERRNO_GENERAL_FAILURE)
    {
        ACS->BG_CTRL = trim;
        return ERRNO_NO_ERROR;
    }

    return ERRNO_VBG_LOAD_ERROR;
}

/* ----------------------------------------------------------------------------
 * Function      : unsigned int Sys_Power_VDDRFCalibratedConfig(uint8_t target)
 * ----------------------------------------------------------------------------
 * Description   : Set the VDDRF voltage trim to the given target based on
 *                 the calibration trim value specified in NVR4. The VDDRF
 *                 power supply is not enabled.
 * Inputs        : target         - The target VDDRF voltage in 10*mV
 * Outputs       : return value   - A code indicating whether an error has
 *                                  occurred.
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
unsigned int Sys_Power_VDDRFCalibratedConfig(uint8_t target)
{
    uint16_t trim;
    uint8_t target_org;
    unsigned int result = ERRNO_NO_ERROR;

    /* Support 1.05 V by using the 1.07 V trim value minus 2*10 mV */
    target_org = target;
    if (target_org == 105)
    {
        target = 107;
    }
    result = Sys_GetTrim((uint32_t *)MANU_INFO_VDDRF, target, &trim);
    if (target_org == 105)
    {
        trim -= 2;
    }

    if (result != ERRNO_GENERAL_FAILURE)
    {
        ACS_VDDRF_CTRL->VTRIM_BYTE = (uint8_t)trim;
        return ERRNO_NO_ERROR;
    }

    return ERRNO_VDDRF_LOAD_ERROR;
}

/* ----------------------------------------------------------------------------
 * Function      : unsigned int Sys_Power_VDDPACalibratedConfig(uint8_t target)
 * ----------------------------------------------------------------------------
 * Description   : Set the VDDPA voltage trim to the given target based on
 *                 the calibration trim value specified in NVR4. The VDDPA
 *                 power supply is not enabled.
 * Inputs        : target         - The target VDDPA voltage in 10*mV
 * Outputs       : return value   - A code indicating whether an error has
 *                                  occurred.
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
unsigned int Sys_Power_VDDPACalibratedConfig(uint8_t target)
{
    uint16_t trim;
    unsigned int result = ERRNO_NO_ERROR;

    result = Sys_GetTrim((uint32_t *)MANU_INFO_VDDPA, target, &trim);

    if (result != ERRNO_GENERAL_FAILURE)
    {
        ACS_VDDPA_CTRL->VTRIM_BYTE = (uint8_t)trim;
        return ERRNO_NO_ERROR;
    }

    return ERRNO_VDDPA_LOAD_ERROR;
}

/* ----------------------------------------------------------------------------
 * Function      : unsigned int Sys_Power_VDDCCalibratedConfig(uint8_t target)
 * ----------------------------------------------------------------------------
 * Description   : Set the VDDC voltage trim to the given target based on
 *                 the calibration trim value specified in NVR4.
 * Inputs        : target         - The target VDDC voltage in 10*mV
 * Outputs       : return value   - A code indicating whether an error has
 *                                  occurred.
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
unsigned int Sys_Power_VDDCCalibratedConfig(uint8_t target)
{
    uint16_t trim;
    unsigned int result = ERRNO_NO_ERROR;

    result = Sys_GetTrim((uint32_t *)MANU_INFO_VDDC, target, &trim);

    if (result != ERRNO_GENERAL_FAILURE)
    {
        ACS_VDDC_CTRL->VTRIM_BYTE = (uint8_t)trim;
        return ERRNO_NO_ERROR;
    }

    return ERRNO_VDDC_LOAD_ERROR;
}

/* ----------------------------------------------------------------------------
 * Function      : unsigned int Sys_Power_VDDCStandbyCalibratedConfig(
 *                                                              uint8_t target)
 * ----------------------------------------------------------------------------
 * Description   : Set the VDDC standby voltage trim to the given target based
 *                 on the calibration trim value specified in NVR4.
 * Inputs        : target         - The target VDDC standby voltage in 10*mV
 * Outputs       : return value   - A code indicating whether an error has
 *                                  occurred.
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
unsigned int Sys_Power_VDDCStandbyCalibratedConfig(uint8_t target)
{
    uint16_t trim;
    unsigned int result = ERRNO_NO_ERROR;

    result = Sys_GetTrim((uint32_t *)MANU_INFO_VDDC_STANDBY, target, &trim);

    if (result != ERRNO_GENERAL_FAILURE)
    {
        ACS_VDDC_CTRL->STANDBY_VTRIM_BYTE = (uint8_t)trim;
        return ERRNO_NO_ERROR;
    }

    return ERRNO_VDDC_STANDBY_LOAD_ERROR;
}

/* ----------------------------------------------------------------------------
 * Function      : unsigned int Sys_Power_VDDMCalibratedConfig(uint8_t target)
 * ----------------------------------------------------------------------------
 * Description   : Set the VDDM voltage trim to the given target based on
 *                 the calibration trim value specified in NVR4.
 * Inputs        : target         - The target VDDM voltage in 10*mV
 * Outputs       : return value   - A code indicating whether an error has
 *                                  occurred.
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
unsigned int Sys_Power_VDDMCalibratedConfig(uint8_t target)
{
    uint16_t trim;
    unsigned int result = ERRNO_NO_ERROR;

    result = Sys_GetTrim((uint32_t *)MANU_INFO_VDDM, target, &trim);

    if (result != ERRNO_GENERAL_FAILURE)
    {
        ACS_VDDM_CTRL->VTRIM_BYTE = (uint8_t)trim;
        return ERRNO_NO_ERROR;
    }

    return ERRNO_VDDM_LOAD_ERROR;
}

/* ----------------------------------------------------------------------------
 * Function      : unsigned int Sys_Power_VDDMStandbyCalibratedConfig(
 *                                                              uint8_t target)
 * ----------------------------------------------------------------------------
 * Description   : Set the VDDM standby voltage trim to the given target based
 *                 on the calibration trim value specified in NVR4.
 * Inputs        : target         - The target VDDM standby voltage in 10*mV
 * Outputs       : return value   - A code indicating whether an error has
 *                                  occurred.
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
unsigned int Sys_Power_VDDMStandbyCalibratedConfig(uint8_t target)
{
    uint16_t trim;
    unsigned int result = ERRNO_NO_ERROR;

    result = Sys_GetTrim((uint32_t *)MANU_INFO_VDDM_STANDBY, target, &trim);

    if (result != ERRNO_GENERAL_FAILURE)
    {
        ACS_VDDM_CTRL->STANDBY_VTRIM_BYTE = (uint8_t)trim;
        return ERRNO_NO_ERROR;
    }

    return ERRNO_VDDM_STANDBY_LOAD_ERROR;
}

/* ----------------------------------------------------------------------------
 * Function      : unsigned int Sys_Power_DCDCCalibratedConfig(uint8_t target)
 * ----------------------------------------------------------------------------
 * Description   : Set the DC-DC voltage trim to the given target based on
 *                 the calibration trim value specified in NVR4. If an ICH_TRIM
 *                 setting is available in NVR4, also load that trim. The
 *                 DC-DC power supply is not enabled.
 * Inputs        : target         - The target DCDC voltage in 10*mV
 * Outputs       : return value   - A code indicating whether an error has
 *                                  occurred.
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
unsigned int Sys_Power_DCDCCalibratedConfig(uint8_t target)
{
    uint16_t trim;
    uint8_t target_org;
    unsigned int result = ERRNO_NO_ERROR;

    /* Trim ICH_TRIM */
	result = Sys_GetTrim((uint32_t *)MANU_INFO_DCDC_ICH_TRIM, 0, &trim);
	if (result != ERRNO_GENERAL_FAILURE)
	{
		ACS_VCC_CTRL->ICH_TRIM_BYTE = (uint8_t)trim;
	}

	/* Return result to ERRNO_NO_ERROR */
	result = ERRNO_NO_ERROR;

    /* Support 1.10 V by using the 1.12 V trim value minus 2*10 mV */
    target_org = target;
    if (target_org == 110)
    {
        target = 112;
    }
    result = Sys_GetTrim((uint32_t *)MANU_INFO_DCDC, target, &trim);
    if (target_org == 110)
    {
        trim -= 2;
    }

    if (result != ERRNO_GENERAL_FAILURE)
    {
        ACS_VCC_CTRL->VTRIM_BYTE = (uint8_t)trim;
        return ERRNO_NO_ERROR;
    }

    return ERRNO_DCDC_LOAD_ERROR;
}
