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

#include "flashCopier.h"

static uint32_t lastFlashAddress = 0;
static uint32_t lastFlashSize    = 0;
static uint32_t lastDSPAddress   = 0;

/* ----------------------------------------------------------------------------
 * Function     : FLASH_COPY_IRQHandler
 * ----------------------------------------------------------------------------
 * Description  : ISR for the interrupt triggered at the end of a flash copy
 *                operation. This is only used at the moment to provide an
 *                indicator for timing purposes, the flash copy operation is
 *                currently polled for complete.
 * Inputs       : None
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void FLASH_COPY_IRQHandler(void)
{
    /* Do nothing for now */
}

/* ----------------------------------------------------------------------------
 * Function     : copyFromFlashToDSP
 * ----------------------------------------------------------------------------
 * Description  : helper function to initiate a flash copy operation to
 *                either PRAM or DRAM
 * Inputs       : flashAddress : the address in flash from which to copy the
 *                data
 *                flashSize : the number of words to copy
 *                dspAddress : the location to which the data should be
 *                copied
 * Outputs      : None
 * Assumptions  : The copy configuration has to be correctly set prior to
 *                using this method.
 * ------------------------------------------------------------------------- */
static void copyFromFlashToDSP(uint32_t flashAddress, uint32_t flashSize,
                               uint32_t dspAddress)
{
    /* only copy if we have to */
    if ((flashAddress == lastFlashAddress) && (flashSize == lastFlashSize)
        && (dspAddress == lastDSPAddress))
    {
        return;
    }

    if (flashSize > 0)
    {
        FLASH->COPY_SRC_ADDR_PTR = flashAddress;
        FLASH->COPY_DST_ADDR_PTR = dspAddress;
        FLASH->COPY_WORD_CNT     = flashSize;
        FLASH->COPY_CTRL = COPY_START;
        while (FLASH->COPY_CTRL == COPY_BUSY)
        {
        }
    }

    /* remember the state so we don't copy if we don't need to */
    lastFlashAddress = flashAddress;
    lastFlashSize    = flashSize;
    lastDSPAddress   = dspAddress;
}

/* ----------------------------------------------------------------------------
 * Function     : copyPMFromFlashToDSP
 * ----------------------------------------------------------------------------
 * Description  : Method to copy program data from flash to PRAM
 * Inputs       : flashAddress : the address in flash from which to copy the
 *                data
 *                flashSize : the number of bytes to copy
 *                dspAddress : the location to which the data should be
 *                copied
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void copyPMFromFlashToDSP(void *flashAddress, uint32_t flashSize,
                          uint32_t dspAddress)
{
    FLASH->COPY_CFG = COPY_TO_40BIT;
    copyFromFlashToDSP((uint32_t)flashAddress, flashSize / 6,
                       DSP_PRAM0_BASE + ((dspAddress * 2) & 0xFFFF));
}

/* ----------------------------------------------------------------------------
 * Function     : copyDMFromFlashToDSP
 * ----------------------------------------------------------------------------
 * Description  : Method to copy data from flash to DRAM
 * Inputs       : flashAddress : the address in flash from which to copy the
 *                data
 *                flashSize : the number of bytes to copy
 *                dspAddress : the location to which the data should be
 *                copied
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void copyDMFromFlashToDSP(void *flashAddress, uint32_t flashSize,
                          uint32_t dspAddress)
{
    /* The flash address should always be word aligned, but check */
    if (((uint32_t)flashAddress & 3) != 0)
    {
        return;
    }

    /* if the dspAddress is not word aligned we need to skip initial bytes and
     * then manually copy the first 1-3 values until we get to a boundary
     */
    unsigned char *src = flashAddress + (dspAddress & 0x3);
    unsigned char *dst = (unsigned char *)dspAddress;
    while ((uint32_t)dst & 3)
    {
        *dst++ = *src++;
        flashSize--;
    }

    /* use the copier to get the bulk of the information copied */
    FLASH->COPY_CFG = COPY_TO_32BIT;
    copyFromFlashToDSP((uint32_t)src, flashSize >> 2, (uint32_t)dst);

    /* if any spare bytes to copy at the end we need to deal with them */
    src += flashSize & 0xFFFFFFFC;
    dst += flashSize & 0xFFFFFFFC;
    flashSize &= 3;
    while (flashSize)
    {
        *dst++ = *src++;
        flashSize--;
    }
}
