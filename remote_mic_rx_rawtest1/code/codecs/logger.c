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
 * ------------------------------------------------------------------------- */

#include <rsl10.h>
#include <rsl10_reg.h>
#include <rsl10_hw.h>

#include "logger.h"

#define LONG_PULSE    (3)
#define SHORT_PULSE    (1)

#define MASK    (0x00000FFF)
#define SELECT    (0x00008000)
#define OTHER    (0x00007FFF)
#define INFO    (0x00001000)
#define ERROR    (0x0000E000)
#define FATAL    (0x0000F000)

#define GPIO_TIME   (5)
#define GPIO_DATA   (6)
#define GPIO_CLOCK    (7)

static int logIsHigh = 0;

/* ----------------------------------------------------------------------------
 * Function     : logInitialise
 * ----------------------------------------------------------------------------
 * Description  : Initialise the logging system, this uses various GPIO pins
 *                to provide a wide range of log features.
 * Inputs       : None
 * Outputs      : None
 * Assumptions  : Assumes the GPIOs are available for use
 * ------------------------------------------------------------------------- */
void logInitialise(void)
{
    Sys_DIO_Config(GPIO_TIME, DIO_WEAK_PULL_DOWN);
    Sys_DIO_Config(GPIO_DATA, DIO_WEAK_PULL_DOWN);
    Sys_DIO_Config(GPIO_CLOCK, DIO_WEAK_PULL_DOWN);
    Sys_DIO_Set_Direction(DIO5_OUTPUT | DIO6_OUTPUT | DIO7_OUTPUT);

    Sys_GPIO_Set_Low(GPIO_TIME);
    Sys_GPIO_Set_Low(GPIO_DATA);
    Sys_GPIO_Set_Low(GPIO_CLOCK);
}

/* ----------------------------------------------------------------------------
 * Function     : flash
 * ----------------------------------------------------------------------------
 * Description  : Flashes the data line and clock line
 * Inputs       : on : indicator for the data line, True implies set high
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void flash(bool on)
{
    if (on)
    {
        Sys_GPIO_Set_High(GPIO_DATA);
    }
    Sys_GPIO_Set_High(GPIO_CLOCK);
    Sys_GPIO_Set_Low(GPIO_CLOCK);
    for (int j = 0; j < LONG_PULSE; j++);
    Sys_GPIO_Set_Low(GPIO_DATA);
    for (int j = 0; j < SHORT_PULSE; j++);
}

/* ----------------------------------------------------------------------------
 * Function     : flashStatus
 * ----------------------------------------------------------------------------
 * Description  : Flashes a sixteen bit status signified by the given value.
 * Inputs       : value : the 16 bit value which should be shown on the data
 *                line.
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void flashStatus(uint16_t value)
{
    for (int bit = 0; bit < 16; bit++)
    {
        flash((value & SELECT) != 0);
        value = (value & OTHER) << 1;
    }
}

/* ----------------------------------------------------------------------------
 * Function     : logStart
 * ----------------------------------------------------------------------------
 * Description  : Signifies the start of a logging event, this sets the TIME
 *                GPIO high
 * Inputs       : None
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void logStart(void)
{
    Sys_GPIO_Set_High(GPIO_TIME);
    logIsHigh = 1;
}

/* ----------------------------------------------------------------------------
 * Function     : logEnd
 * ----------------------------------------------------------------------------
 * Description  : Signifies the end of a logging event, this sets the TIME
 *                GPIO low
 * Inputs       : None
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void logEnd(void)
{
    Sys_GPIO_Set_Low(GPIO_TIME);
    logIsHigh = 0;
}

/* ----------------------------------------------------------------------------
 * Function     : logToggle
 * ----------------------------------------------------------------------------
 * Description  : toggles the TIME GPIO low
 * Inputs       : None
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void logToggle(void)
{
    if (logIsHigh)
    {
        Sys_GPIO_Set_Low(GPIO_TIME);
        Sys_GPIO_Set_High(GPIO_TIME);
    }
    else
    {
        Sys_GPIO_Set_High(GPIO_TIME);
        Sys_GPIO_Set_Low(GPIO_TIME);
    }
}

/* ----------------------------------------------------------------------------
 * Function     : logInfo
 * ----------------------------------------------------------------------------
 * Description  : Logs a 12 bit information message
 * Inputs       : status : the status event to log, this only uses the
 *                bottom 12 bits
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void logInfo(uint16_t status)
{
    flashStatus(INFO | (status & MASK));
}

/* ----------------------------------------------------------------------------
 * Function     : logError
 * ----------------------------------------------------------------------------
 * Description  : Logs a 12 bit error message
 * Inputs       : status : the status event to log, this only uses the
 *                bottom 12 bits
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void logError(uint16_t status)
{
    flashStatus(ERROR | (status & MASK));
}

/* ----------------------------------------------------------------------------
 * Function     : logFatal
 * ----------------------------------------------------------------------------
 * Description  : Logs a 12 bit fatal error message and causes the program
 *                to enter an infinite loop until the watchdog resets.
 * Inputs       : status : the status event to log, this only uses the
 *                bottom 12 bits
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void logFatal(uint16_t status)
{
    while (1)
    {
        flashStatus(FATAL | (status & MASK));
        for (int j = 0; j < LONG_PULSE * 3; j++);
    }
}
