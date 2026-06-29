/* ----------------------------------------------------------------------------
 * Copyright (c) 2016-2017 Semiconductor Components Industries, LLC (d/b/a
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
 * rsl10_sys_clocks.c
 * - Library that provides functions to calibarte and manipulate the clocks
 *   registers
 * ----------------------------------------------------------------------------
 * $Revision: 1.15 $
 * $Date: 2017/08/17 15:32:34 $
 * ------------------------------------------------------------------------- */

#include "rsl10.h"

/* ----------------------------------------------------------------------------
 * Function      : unsigned int Sys_Clocks_Osc32kCalibratedConfig(uint16_t target)
 * ----------------------------------------------------------------------------
 * Description   : Set the standby clock frequency to the given target based on
 *                 a calibration trim value specified in NVR4. The 32k
 *                 oscillator is not enabled. This function will only load the
 *                 trim register, the user is responsible for enabling the
 *                 oscillator if desired.
 * Inputs        : target         - The target 32k oscillator frequency in Hz
 * Outputs       : return value   - A code indicating whether an error has
 *                                  occurred.
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
unsigned int Sys_Clocks_Osc32kCalibratedConfig(uint16_t target)
{
    uint16_t trim;
    unsigned int result = ERRNO_NO_ERROR;

    result = Sys_GetTrim((uint32_t *)MANU_INFO_OSC_32K, target, &trim);

    if (result != ERRNO_GENERAL_FAILURE)
    {
        ACS_RCOSC_CTRL->FTRIM_32K_BYTE = (ACS_RCOSC_CTRL->FTRIM_32K_BYTE &
                                         ~ACS_RCOSC_CTRL_FTRIM_32K_Mask) |
                                         (trim << ACS_RCOSC_CTRL_FTRIM_32K_Pos);
        return ERRNO_NO_ERROR;
    }

    return ERRNO_RCOSC_32k_LOAD_ERROR;
}

/* ----------------------------------------------------------------------------
 * Function      : unsigned int Sys_Clocks_OscRCCalibratedConfig(uint16_t target)
 * ----------------------------------------------------------------------------
 * Description   : Set the start oscillator frequency to the given target based
 *                 on a calibration trim value specified in NVR4. This function
 *                 only loads the trim register and multiplier bit if necessary.
 * Inputs        : target         - The target start oscillator frequency in kHz
 * Outputs       : return value   - A code indicating whether an error has
 *                                  occurred.
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
unsigned int Sys_Clocks_OscRCCalibratedConfig(uint16_t target)
{
    uint16_t trim;
    uint16_t *rcosc_ftrim;
    unsigned int result = ERRNO_NO_ERROR;

    result = Sys_GetTrim((uint32_t *)(MANU_INFO_OSC_RC), target, &trim);

    if (result != ERRNO_GENERAL_FAILURE)
    {
        rcosc_ftrim = (uint16_t *)(ACS_RCOSC_CTRL);
        *rcosc_ftrim = ((uint16_t)(*rcosc_ftrim) & ~ACS_RCOSC_CTRL_FTRIM_START_Mask) |
                       (trim << ACS_RCOSC_CTRL_FTRIM_START_Pos);

        ACS_RCOSC_CTRL->CLOCK_MULT_ALIAS = RC_START_OSC_3MHZ_BITBAND;
        ACS_RCOSC_CTRL->FTRIM_FLAG_ALIAS = RC_OSC_CALIBRATED_BITBAND;

        return ERRNO_NO_ERROR;
    }

    result = Sys_GetTrim((uint32_t *)(MANU_INFO_OSC_RC_MULT), target, &trim);

    if (result != ERRNO_GENERAL_FAILURE)
    {
        rcosc_ftrim = (uint16_t *)(ACS_RCOSC_CTRL);
        *rcosc_ftrim = ((uint16_t)(*rcosc_ftrim) & ~ACS_RCOSC_CTRL_FTRIM_START_Mask) |
                       (trim << ACS_RCOSC_CTRL_FTRIM_START_Pos);

        ACS_RCOSC_CTRL->CLOCK_MULT_ALIAS = RC_START_OSC_12MHZ_BITBAND;
        ACS_RCOSC_CTRL->FTRIM_FLAG_ALIAS = RC_OSC_CALIBRATED_BITBAND;

        return ERRNO_NO_ERROR;
    }

    return ERRNO_RC_LOAD_ERROR;
}
