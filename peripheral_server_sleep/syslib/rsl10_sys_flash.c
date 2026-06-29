/* ----------------------------------------------------------------------------
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
 * ----------------------------------------------------------------------------
 * rsl10_sys_flash.c
 * - Flash and flash support functions
 * ----------------------------------------------------------------------------
 * $Revision: 1.5 $
 * $Date: 2017/07/05 16:26:49 $
 * ------------------------------------------------------------------------- */

#include <rsl10.h>

/* ----------------------------------------------------------------------------
 * Function      : void Sys_Flash_Copy(uint32_t src_addr, uint32_t dest_addr,
 *                                     uint32_t length, uint32_t cpy_dest)
 * ----------------------------------------------------------------------------
 * Description   : Copy data from the flash memory to a RAM memory instance
 * Inputs        : - src_addr   - Source address in flash to copy data from
 *                 - dest_addr  - Destination address in RAM to copy data to
 *                 - length     - Number of words to copy
 *                 - cpy_dest   - Destination copier is CRC or memories; use
 *                                  COPY_TO_[CRC | MEM]_BITBAND
 * Outputs       : None
 * Assumptions   : - src_addr points to an address in flash memory
 *                 - dest_addr points to an address in RAM memory
 *                 - If dest_addr points to an area in DSP_PRAM memory, the
 *                   copy will write all 40 bits of the PRAM memory
 *                 - The flash copy does not need to be complete before
 *                   returning
 *                 - If CRC is selected as the destination, dest_addr is
 *                   ignored and 32-bit copy mode is selected automatically.
 * ------------------------------------------------------------------------- */
void Sys_Flash_Copy(uint32_t src_addr, uint32_t dest_addr, uint32_t length,
                    uint32_t cpy_dest)
{
    /* Wait for the flash copier to be idle */
    while (FLASH_COPY_CTRL->BUSY_ALIAS != COPY_IDLE_BITBAND);
    FLASH_COPY_CFG->COPY_DEST_ALIAS = cpy_dest;

    /* Configure the flash copier */
    if (cpy_dest == COPY_TO_MEM_BITBAND)
    {
        if ((dest_addr >= DSP_PRAM0_BASE) && (dest_addr < DSP_PRAM3_TOP))
        {
            FLASH_COPY_CFG->COPY_MODE_ALIAS = COPY_TO_40BIT_BITBAND;
        }
        else
        {
            FLASH_COPY_CFG->COPY_MODE_ALIAS = COPY_TO_32BIT_BITBAND;
        }
    }
    else
    {
        FLASH_COPY_CFG->COPY_MODE_ALIAS = COPY_TO_32BIT_BITBAND;
    }
    FLASH_COPY_CFG->MODE_ALIAS = COPY_MODE_BITBAND;

    /* Setup the source, destination, and length of the copy */
    FLASH->COPY_SRC_ADDR_PTR = src_addr;
    FLASH->COPY_DST_ADDR_PTR = dest_addr;
    FLASH->COPY_WORD_CNT = length;

    /* Start the copy */
    FLASH_COPY_CTRL->START_ALIAS = COPY_START_BITBAND;
}

/* ----------------------------------------------------------------------------
 * Function      : uint32_t Sys_Flash_Compare(uint32_t cfg, uint32_t addr,
 *                                            uint32_t length, uint32_t value,
 *                                            uint32_t value_ecc)
 * ----------------------------------------------------------------------------
 * Description   : Compare data in the flash to a pre-specified value
 * Inputs        : - cfg        - Flash comparator configuration; use
 *                                COMP_MODE_[CONSTANT | CHBK]_BYTE,
 *                                COMP_ADDR_[DOWN | UP]_BYTE, and
 *                                COMP_ADDR_STEP_*_BYTE
 *                 - addr       - Base address of the area to verify
 *                 - length     - Number of words to verify
 *                 - value      - Value that the words read from flash will be
 *                                compared against
 *                 - value_ecc  - Value that the error-correction coding
 *                                bits from the extended words read from flash
 *                                will be compared against
 * Outputs       : return value - 0 if comparison succeeded, 1 if the
 *                                comparison failed.
 * Assumptions   : addr points to an address in flash memory
 * ------------------------------------------------------------------------- */
uint32_t Sys_Flash_Compare(uint32_t cfg, uint32_t addr, uint32_t length,
                           uint32_t value, uint32_t value_ecc)
{
    /* Wait for the flash copier to be idle */
    while (FLASH_COPY_CTRL->BUSY_ALIAS != COPY_IDLE_BITBAND);

    /* Configure the flash comparator */
    FLASH_COPY_CFG->COMP_BYTE = cfg;
    FLASH_COPY_CFG->MODE_ALIAS = COMPARATOR_MODE_BITBAND;

    /* Setup the source and length of the comparison, and the data value to
     * compare against */
    FLASH->COPY_SRC_ADDR_PTR = addr;
    FLASH->COPY_WORD_CNT = length;
    FLASH->DATA[0] =  value;
    FLASH->DATA[1] =  value_ecc;

    /* Start the comparison */
    FLASH_COPY_CTRL->START_ALIAS = COPY_START_BITBAND;

    /* Wait for the flash copier to be idle, and return the error code */
    while (FLASH_COPY_CTRL->BUSY_ALIAS != COPY_IDLE_BITBAND);
    return FLASH_COPY_CTRL->ERROR_ALIAS;
}
