#ifndef _CELT_DEC_DSP_H_
#define _CELT_DEC_DSP_H_ 1

/* Auto generated file
 */

#include <stdint.h>

#include "celt_dec_dsp_PM.h"
#include "celt_dec_dsp_DM_Hi.h"
#include "celt_dec_dsp_DM_Lo.h"

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

extern memoryOverview __attribute__ ((section (".dsp"))) celt_dec_dsp_Overview;

#endif
