/* -------------------------------------------------------------------------
 * Copyright (c) 2015-2017 Semiconductor Components Industries, LLC (d/b/a ON
 * Semiconductor), All Rights Reserved
 *
 * This code is the property of ON Semiconductor and may not be redistributed
 * in any form without prior written permission from ON Semiconductor.
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 *
 * This is Reusable Code.
 *
 * -------------------------------------------------------------------------
 * rsl10_sys_timers.c
 * - General-purpose system timer hardware support code source file
 * -------------------------------------------------------------------------
 * $Revision: 1.5 $
 * $Date: 2017/07/07 19:02:12 $
 * ------------------------------------------------------------------------- */

#include <rsl10.h>

/* ----------------------------------------------------------------------------
 * Function      : void Sys_Timers_Start(uint32_t cfg)
 * ----------------------------------------------------------------------------
 * Description   : Start the specified general-purpose system timers
 * Inputs        : cfg - Timers to start; use the SELECT_TIMER* settings or
 *                       SELECT_[ALL | NO]_TIMERS to indicate
 *                       which timers to start
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Sys_Timers_Start(uint32_t cfg)
{
    /* Start the timers specified, leaving any unspecified timers in their
     * previous state. To avoid race conditions with a read-modify-write of the
     * TIMER_CTRL_STATUS register, write each of the bits independantly. The
     * stop bit since stop has precedence over start. */
    if ((cfg & SELECT_TIMER0) != 0)
    {
        TIMER_CTRL[0].TIMER_STOP_ALIAS = ~TIMER_STOP_BITBAND;
        TIMER_CTRL[0].TIMER_START_ALIAS = TIMER_START_BITBAND;
    }

    if ((cfg & SELECT_TIMER1) != 0)
    {
        TIMER_CTRL[1].TIMER_STOP_ALIAS = ~TIMER_STOP_BITBAND;
        TIMER_CTRL[1].TIMER_START_ALIAS = TIMER_START_BITBAND;

    }

    if ((cfg & SELECT_TIMER2) != 0)
    {
        TIMER_CTRL[2].TIMER_STOP_ALIAS = ~TIMER_STOP_BITBAND;
        TIMER_CTRL[2].TIMER_START_ALIAS = TIMER_START_BITBAND;
    }

    if ((cfg & SELECT_TIMER3) != 0)
    {
        TIMER_CTRL[3].TIMER_STOP_ALIAS = ~TIMER_STOP_BITBAND;
        TIMER_CTRL[3].TIMER_START_ALIAS = TIMER_START_BITBAND;

    }
}

/* ----------------------------------------------------------------------------
 * Function      : void Sys_Timers_Stop(uint32_t cfg)
 * ----------------------------------------------------------------------------
 * Description   : Stop the specified general-purpose system timers
 * Inputs        : cfg - Timers to stop; use the SELECT_TIMER* settings or
 *                       SELECT_[ALL | NO]_TIMERS to indicate
 *                       which timers to stop
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Sys_Timers_Stop(uint32_t cfg)
{
    /* Stop only the timers specified, leaving any unspecified timers in their
     * previous state. To avoid race conditions with a read-modify-write of the
     * TIMER_CTRL_STATUS register, write each of the bits independantly */

    if ((cfg & SELECT_TIMER0) != 0)
    {
        TIMER_CTRL[0].TIMER_STOP_ALIAS = TIMER_STOP_BITBAND;
    }

    if ((cfg & SELECT_TIMER1) != 0)
    {
        TIMER_CTRL[1].TIMER_STOP_ALIAS = TIMER_STOP_BITBAND;
    }

    if ((cfg & SELECT_TIMER2) != 0)
    {
        TIMER_CTRL[2].TIMER_STOP_ALIAS = TIMER_STOP_BITBAND;
    }

    if ((cfg & SELECT_TIMER3) != 0)
    {
        TIMER_CTRL[3].TIMER_STOP_ALIAS = TIMER_STOP_BITBAND;
    }
}
