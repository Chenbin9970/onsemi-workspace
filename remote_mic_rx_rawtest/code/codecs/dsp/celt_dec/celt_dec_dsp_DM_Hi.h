#ifndef _CELT_DEC_DSP_DM_HI_H_
#define _CELT_DEC_DSP_DM_HI_H_ 1

/* Auto generated file
 */

#include <stdint.h>

#ifndef _MEMORY_DESCRIPTION_
#define _MEMORY_DESCRIPTION_ 1

typedef struct
{
    void        *buffer;
    uint32_t    fileSize;
    uint32_t    memSize;
    uint32_t    vAddress;
} memoryDescription;

#endif

extern memoryDescription  __attribute__ ((section (".dsp"))) celt_dec_dsp_DM_Hi_SegmentList[];

#endif
