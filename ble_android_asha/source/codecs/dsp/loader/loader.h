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

#ifndef LOADER_H_
#define LOADER_H_

#include <stdint.h>

/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

#define _MEMORY_DESCRIPTION_ 1
#define _MEMORY_OVERVIEW_DESCRIPTION_ 1

typedef struct
{
    void *buffer;
    uint32_t fileSize;
    uint32_t memSize;
    uint32_t vAddress;
} memoryDescription;

typedef struct
{
    memoryDescription *entries;
    uint32_t count;
} memoryOverviewEntry;

typedef struct
{
    memoryOverviewEntry PM;
    memoryOverviewEntry DMH;
    memoryOverviewEntry DML;
} memoryOverview;

/* Normally when loading the LPDSP32 we can just use this routine */
void loadDSPMemory(memoryOverview *overview);

/* provide a hook to explicitly reset the loop cache */
void resetLoopCache(void);

/* For cases where finer control of the loading process is required, we expose
 * the following helper functions
 */
void loadSinglePRAMEntry(memoryDescription *descriptor);

void loadDSPDRAM(memoryOverviewEntry *hi, memoryOverviewEntry *lo);

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* LOADER_H_ */
