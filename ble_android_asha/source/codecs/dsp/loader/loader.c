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

#include <string.h>
#include <rsl10.h>

#include "flashcopier.h"

#include "loader.h"

/* ----------------------------------------------------------------------------
 * Function     : loadSinglePRAMEntry
 * ----------------------------------------------------------------------------
 * Description  : This loads a single PRAM descriptor into the LPDSP32 PRAM
 * Inputs       : descriptor : the descriptor for the memory area to be
 *                copied.
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void loadSinglePRAMEntry(memoryDescription *descriptor)
{
    copyPMFromFlashToDSP(descriptor->buffer, descriptor->memSize,
                         descriptor->vAddress);
}

/* ----------------------------------------------------------------------------
 * Function     : loadDSPPRAM
 * ----------------------------------------------------------------------------
 * Description  : Loads the LPDSP32 PRAM with all the program sections.
 * Inputs       : pram : a memory overview object which provides a list of
 *                memory sections which need to be copied
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void loadDSPPRAM(memoryOverviewEntry *pram)
{
    for (int i = 0; i < pram->count; i++)
    {
        loadSinglePRAMEntry(&pram->entries[i]);
    }
}

/* ----------------------------------------------------------------------------
 * Function     : mapToCM3Space
 * ----------------------------------------------------------------------------
 * Description  : Maps an address from the LPDSP32 address space into CM3
 * Inputs       : vAddress : an address in LPDSP32 memory space which must
 *                be mapped to a CM3 address
 * Outputs      : The CM3 address corresponding to the LPDSP32 address
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static uint32_t mapToCM3Space(uint32_t vAddress)
{
    if (vAddress < 0x00800000)
    {
        /* this is a low (DMA) address, adjust accordingly */
        return (vAddress + DSP_DRAM01_BASE);
    }
    else
    {
        return (vAddress - 0x00800000 + DSP_DRAM4_BASE);
    }
}

/* ----------------------------------------------------------------------------
 * Function     : initialiseDSPDRAM
 * ----------------------------------------------------------------------------
 * Description  : Helper method to copy a single block of DRAM to the
 *                LPDSP32
 * Inputs       : dram : a memory descriptor defining the block of memory to
 *                be copied
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void initialiseDSPDRAM(memoryDescription *dram)
{
    if (dram->fileSize > 0)
    {
        copyDMFromFlashToDSP(dram->buffer, dram->fileSize,
                             mapToCM3Space(dram->vAddress));
    }
}

/* ----------------------------------------------------------------------------
 * Function     : loadDSPDRAM
 * ----------------------------------------------------------------------------
 * Description  : Loads the DRAM associated with a program to the LPDSP32
 * Inputs       : hi : A memory overview object specifying the high memory
 *                area
 *                lo : A memory overview object specifying the low memory
 *                area
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void loadDSPDRAM(memoryOverviewEntry *hi, memoryOverviewEntry *lo)
{
    for (int i = 0; i < lo->count; i++)
    {
        initialiseDSPDRAM(&lo->entries[i]);
    }
    for (int i = 0; i < hi->count; i++)
    {
        initialiseDSPDRAM(&hi->entries[i]);
    }
}

/* ----------------------------------------------------------------------------
 * Function     : resetLoopCache
 * ----------------------------------------------------------------------------
 * Description  : Helper method to reset the loop cache on the LPDSP32
 * Inputs       : None
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void resetLoopCache(void)
{
    SYSCTRL->DSS_LOOP_CACHE_CFG = 0;
    SYSCTRL->DSS_LOOP_CACHE_CFG = 1;
}

/* ----------------------------------------------------------------------------
 * Function     : loadDSPMemory
 * ----------------------------------------------------------------------------
 * Description  : Generic loader that can be used to load simple programs
 *                from flash to the LPDSP32 PRAM and DRAM
 * Inputs       : overview : An overview object that contains the
 *                specifications for all the PRAM and DRAM sections to be
 *                copied.
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void loadDSPMemory(memoryOverview *overview)
{
    loadDSPPRAM(&overview->PM);
    loadDSPDRAM(&overview->DMH, &overview->DML);

    /* Run LPDSP32 */
    resetLoopCache();
    SYSCTRL->DSS_CTRL = DSS_RESET;
}
