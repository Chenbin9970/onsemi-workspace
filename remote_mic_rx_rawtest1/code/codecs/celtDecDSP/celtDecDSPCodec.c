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

#include "logger.h"
#include "codecs/baseDSP/baseDSPCodec.h"
#include "codecs/baseDSP/baseDSPCodecInternal.h"
#include "codecs/celtDecDSP/celtDecDSPCodec.h"

#include "dsp/loader/loader.h"
#include "dsp/celt_dec/celt_dec_dsp.h"

/* The base locations for each of the memory areas is set in the BCF file
 * for the LPDSP32 project which builds the codec binary. These numbers must
 * match that.
 */
#define BASE_COMMON_PM  0x00000000
#define BASE_INIT_PM    0x00011C00
#define BASE_DECODE_PM  0x00021C00
#define BASE_ENCODE_PM  0x00041C00
#define BASE_UNUSED_PM  0x00081C00

/* Prototypes for the methods we will override from the base */
static void celtDecDSPInitialise(CODEC codec);

static void celtDecDSPPrepareConfigure(CODEC codec);

static void celtDecDSPPrepareEncode(CODEC codec);

static void celtDecDSPPrepareDecode(CODEC codec);

/* ----------------------------------------------------------------------------
 * Function     : initialiseStructure
 * ----------------------------------------------------------------------------
 * Description  : Set up the control block to provide the overridden methods
 *                we require for the CELT encode/decode
 * Inputs       : structure : A pointer to a codec control structure which
 *                is to be initialised
 * Outputs      : The structure object is initialised with default data.
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void initialiseStructure(pCodec structure)
{
    if (codecIsCodec(structure))
    {
        structure->type = CELT_DSP;
        structure->initialise = &celtDecDSPInitialise;
        structure->prepareConfigure = &celtDecDSPPrepareConfigure;
        structure->prepareEncode = &celtDecDSPPrepareEncode;
        structure->prepareDecode = &celtDecDSPPrepareDecode;
    }
}

/* ----------------------------------------------------------------------------
 * Function     : makeCeltDecDSPCodec
 * ----------------------------------------------------------------------------
 * Description  : General constructor, this uses malloc to allocate a block
 *                of memory which it then initialises as above.
 * Inputs       : None
 * Outputs      : An opaque CODEC object, effectively a handle that van be
 *                used in later operations on this codec
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
CODEC makeCeltDecDSPCodec(void)
{
    pCodec celt = getCodec(makeBaseDSPCodec());
    initialiseStructure(celt);
    return celt;
}

/* ----------------------------------------------------------------------------
 * Function     : populateCeltDecDSPCodec
 * ----------------------------------------------------------------------------
 * Description  : As an alternative to allocating memory for a codec, this
 *                method is provided to allow a pre-allocated memory
 *                location to be set up
 * Inputs       : buffer : a pointer to a buffer in which to create and
 *                initialise the codec control block
 *                size : the size in bytes of the buffer
 * Outputs      : NULL if the size of the buffer is not large enough for a
 *                CODEC handle
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
CODEC populateCeltDecDSPCodec(unsigned char *buffer, uint32_t size)
{
    pCodec celt = getCodec(populateBaseDSPCodec(buffer, size));
    initialiseStructure(celt);
    return buffer;
}

/* ----------------------------------------------------------------------------
 * Function     : loadPRAM
 * ----------------------------------------------------------------------------
 * Description  : Load memory areas within the bounds specified by [base,
 *                top)
 * Inputs       : base : The base address of the memory to be selected
 *                top : The top address +1 of the memory to be selected
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void loadPRAM(uint32_t base, uint32_t top)
{
    memoryOverviewEntry *pram = &celt_dec_dsp_Overview.PM;
    for (int i = 0; i < pram->count; i++)
    {
        memoryDescription *description = &pram->entries[i];
        if ((description->vAddress >= base) && (description->vAddress < top))
        {
            loadSinglePRAMEntry(description);
        }
    }
}

/* ----------------------------------------------------------------------------
 * Function     : celtDecDSPInitialise
 * ----------------------------------------------------------------------------
 * Description  : Overridden initialiser to ensure the memory descriptors
 *                are configured and the right sections of the codec have
 *                been loaded to the DSP
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : This relies on the program sections being correctly set up
 *                when building the CELT codec for the DSP.
 * ------------------------------------------------------------------------- */
static void celtDecDSPInitialise(CODEC codec)
{
    pBaseDSPCodec celt __attribute__ ((unused)) = getDSPCodec(codec);

    /* load the PRAM for the common and initialisation stages */
    loadPRAM(BASE_COMMON_PM, BASE_INIT_PM);
    loadPRAM(BASE_INIT_PM, BASE_DECODE_PM);

    /* load the DRAM, this is straightforward */
    loadDSPDRAM(&celt_dec_dsp_Overview.DMH, &celt_dec_dsp_Overview.DML);

    /* Run LPDSP32 */
    resetLoopCache();
    SYSCTRL->DSS_CTRL = DSS_RESET;
}

/* ----------------------------------------------------------------------------
 * Function     : celtDecDSPPrepareConfigure
 * ----------------------------------------------------------------------------
 * Description  : In preparation for a configure operation we need to load
 *                the configure code into PRAM
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void celtDecDSPPrepareConfigure(CODEC codec)
{
    logStart();
    loadPRAM(BASE_INIT_PM, BASE_DECODE_PM);
}

/* ----------------------------------------------------------------------------
 * Function     : celtDecDSPPrepareEncode
 * ----------------------------------------------------------------------------
 * Description  : In preparation for an encode operation we need to load the
 *                encode code into PRAM
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void celtDecDSPPrepareEncode(CODEC codec)
{
    logStart();

    /* Cripple the encode operation by dropping into a loop here until the
     * watchdog kicks */
    while (1)
    {
    	/* Don't do anything here, just wait for the watchdog to kick */
    }
}

/* ----------------------------------------------------------------------------
 * Function     : celtDecDSPPrepareDecode
 * ----------------------------------------------------------------------------
 * Description  : In preparation for a decode operation we need to load the
 *                decode code into PRAM
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void celtDecDSPPrepareDecode(CODEC codec)
{
    logStart();
    loadPRAM(BASE_DECODE_PM, BASE_ENCODE_PM);
}
