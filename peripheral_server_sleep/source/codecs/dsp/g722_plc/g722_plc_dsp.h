#ifndef _G722_PLC_DSP_H_
#define _G722_PLC_DSP_H_ 1

/* Auto generated file
 */

#include <stdint.h>

#include "g722_plc_dsp_PM.h"
#include "g722_plc_dsp_DM_Hi.h"
#include "g722_plc_dsp_DM_Lo.h"

#ifndef _MEMORY_OVERVIEW_DESCRIPTION_
#define _MEMORY_OVERVIEW_DESCRIPTION_ 1

typedef struct
{
    memoryDescription   *entries;
    uint32_t            count;
} memoryOverviewEntry;

typedef struct
{
    memoryOverviewEntry    PM;
    memoryOverviewEntry    DMH;
    memoryOverviewEntry    DML;
} memoryOverview;

#endif

extern memoryOverview __attribute__ ((section (".dsp"))) g722_plc_dsp_Overview;

#endif
