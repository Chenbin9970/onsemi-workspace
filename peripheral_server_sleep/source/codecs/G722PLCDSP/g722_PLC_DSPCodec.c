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

#include "codecs/baseDSP/baseDSPCodec.h"
#include "codecs/baseDSP/baseDSPCodecInternal.h"

#include "dsp/loader/loader.h"
#include "dsp/g722_plc/g722_plc_dsp.h"
#include "g722_PLC_DSPCodec.h"

/* Define function prototypes for the features we will override from the base */
static void g722PLCDSPInitialise(CODEC codec);

/* ----------------------------------------------------------------------------
 * Function     : initialiseStructure
 * ----------------------------------------------------------------------------
 * Description  : Initialises an echo codec control block. This sets the
 *                function pointers in the jump table for any methods we
 *                wish to override.
 * Inputs       : structure : A pointer to a codec control structure which
 *                is to be initialised
 * Outputs      : The structure object is initialised with default data.
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void initialiseStructure(pCodec structure)
{
    if (codecIsCodec(structure))
    {
        structure->type = G722_PLC_DSP;
        structure->initialise = &g722PLCDSPInitialise;
    }
}

/* ----------------------------------------------------------------------------
 * Function     : makeG722PLCDSPCodec
 * ----------------------------------------------------------------------------
 * Description  : General constructor for G722 PLC codec, this uses malloc to
 *                allocate a block of memory which it then initialises as
 *                above.
 * Inputs       : None
 * Outputs      : An opaque CODEC object, effectively a handle that van be
 *                used in later operations on this codec
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
CODEC makeG722PLCDSPCodec(void)
{
    pCodec g722 = getCodec(makeBaseDSPCodec());
    initialiseStructure(g722);
    return g722;
}

/* ----------------------------------------------------------------------------
 * Function     : populateG722PLCDSPCodec
 * ----------------------------------------------------------------------------
 * Description  : As an alternative to allocating memory for a codec, this
 *                method is provided to allow a pre-allocated memory
 *                location to be set up
 * Inputs       : buffer : a pointer to a buffer in which to create and
 *                initialise the codec control block
 *                size : the size in bytes of the buffer
 * Outputs      : NULL if the size of the buffer is not large enough or a
 *                CODEC handle
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
CODEC populateG722PLCDSPCodec(unsigned char *buffer, uint32_t size)
{
    pCodec g722 = getCodec(populateBaseDSPCodec(buffer, size));
    initialiseStructure(g722);
    return buffer;
}

/* ----------------------------------------------------------------------------
 * Function     : g722PLCDSPInitialise
 * ----------------------------------------------------------------------------
 * Description  : The initialisation of the DSP based G722 codec must load
 *                the LPDSP32 program into memory, once this is done
 *                everything else is correctly configured by the base
 *                implementations.
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void g722PLCDSPInitialise(CODEC codec)
{
    pBaseDSPCodec object  __attribute__ ((unused)) = getDSPCodec(codec);
    SYSCTRL->DSS_CTRL = DSS_LPDSP32_PAUSE;
    loadDSPMemory(&g722_plc_dsp_Overview);
    SYSCTRL->DSS_CTRL = DSS_LPDSP32_RESUME;
}
