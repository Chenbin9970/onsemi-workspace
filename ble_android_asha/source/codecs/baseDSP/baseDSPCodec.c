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

#include <malloc.h>
#include <stdint.h>

#include <rsl10.h>

#include "codecs/baseDSP/baseDSPCodec.h"
#include "codecs/baseDSP/baseDSPCodecInternal.h"

/* A defined signature for a DSP compliant codec, this is selected specifically
 * to be different from the signature on the base codec. */
#define SIGNATURE    (0x00C0DEC0)

/* Define function prototypes for the features we will override from base */
static void baseDSPConfigure(CODEC codec);

static void baseDSPEncode(CODEC codec);

static void baseDSPDecode(CODEC codec);

/* ----------------------------------------------------------------------------
 * Function     : initialiseStructure
 * ----------------------------------------------------------------------------
 * Description  : Initialises a base DSP codec control block. This sets the
 *                function pointers in the jump table for any methods we
 *                wish to override.
 * Inputs       : structure : A pointer to a codec control structure which
 *                is to be initialised
 * Outputs      : The structure object is initialised with default data.
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void initialiseStructure(pBaseDSPCodec structure)
{
    if (codecIsCodec(structure))
    {
        structure->parent.configure = &baseDSPConfigure;
        structure->parent.encode    = &baseDSPEncode;
        structure->parent.decode    = &baseDSPDecode;
        structure->parent.type  = BASE_DSP;
        structure->dspStatus    = 0;
        structure->dspError     = 0;
        structure->dspSignature = SIGNATURE;
    }
}

/* ----------------------------------------------------------------------------
 * Function     : makeBaseDSPCodec
 * ----------------------------------------------------------------------------
 * Description  : General constructor for a base DSP codec, this uses malloc
 *                to allocate a block of memory which it then initialises as
 *                above.
 * Inputs       : None
 * Outputs      : An opaque CODEC object, effectively a handle that can be
 *                used in later operations on this codec
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
CODEC makeBaseDSPCodec(void)
{
    pCodec base    = getCodec(makeBaseCodec());
    pBaseDSPCodec dspBase = (pBaseDSPCodec)realloc(base,
                                                   sizeof(struct _baseDSPCodec));
    initialiseStructure(dspBase);
    return (dspBase);
}

/* ----------------------------------------------------------------------------
 * Function     : populateBaseDSPCodec
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
CODEC populateBaseDSPCodec(unsigned char *buffer, uint32_t size)
{
    if (sizeof(struct _baseDSPCodec) > size)
    {
        return (NULL);
    }
    pBaseDSPCodec dspBase = (pBaseDSPCodec)getCodec(
        populateBaseCodec(buffer, size));
    initialiseStructure(dspBase);
    return (dspBase);
}

/* ----------------------------------------------------------------------------
 * Function     : getDSPCodec
 * ----------------------------------------------------------------------------
 * Description  : Helper function to retrieve the DSP Codec control block
 *                from the given CODEC handle. This performs simple
 *                verification that the control block has not been corrupted
 *                and a fatal message will be logged if it has.
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : This will return a pointer to a DSP Codec control block
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
pBaseDSPCodec getDSPCodec(CODEC codec)
{
    if (codecIsCodec(codec))
    {
        pBaseDSPCodec dsp = (pBaseDSPCodec)codec;
        if (dsp->dspSignature == SIGNATURE)
        {
            return (dsp);
        }
    }

    /* not a real codec, for now we'll treat this as a fatal code error */
    return (NULL);
}

/* ----------------------------------------------------------------------------
 * Function     : dspHandshake
 * ----------------------------------------------------------------------------
 * Description  : When communicating with the LPDSP32 co-processor we need
 *                to have a mechanism for getting it in synchronisation with
 *                the ARM, this method provides a simple handshake protocol
 *                to assist with this. This method is expected to be used at
 *                the start of a program and will communicate with the
 *                LPDSP32 via shared memory and expects the LPDSP32 to use
 *                the corresponding routine in the wrapper main function.
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : This will hang if the handshake protocol can't be
 *                completed
 * ------------------------------------------------------------------------- */
void dspHandshake(CODEC codec)
{
    pBaseDSPCodec config = getDSPCodec(codec);
    if (config->dspStatus != 0)
    {
        config->dspStatus = 0;
    }
    while (config->dspStatus != 1);
    config->dspStatus = 2;

    /* Strictly speaking the handshake is over when the dspStatus hits 0x03
     * however as the DSP will be free running at this stage it is also valid
     * for it to be 0x34
     */
    while ((config->dspStatus != 3) && (config->dspStatus != 0x34));
}

/* ----------------------------------------------------------------------------
 * Function     : baseDSPConfigure
 * ----------------------------------------------------------------------------
 * Description  : Configure the codec, this is the default implementation
 *                when minimal configuration is required on the DSP. All
 *                this does is indicate to the LPDSP32 program that it is
 *                time to configure.
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : This implementation may be enough if no specific
 *                configuration is required however each codec implementor
 *                should consider if this is needed
 * ------------------------------------------------------------------------- */
static void baseDSPConfigure(CODEC codec)
{
    pBaseDSPCodec object __attribute__ ((unused)) = getDSPCodec(codec);

    /* Tell the DSP there is data available to deal with. */
    SYSCTRL_DSS_CMD->DSS_CMD_1_ALIAS = DSS_CMD_1_BITBAND;
}

/* ----------------------------------------------------------------------------
 * Function     : baseDSPEncode
 * ----------------------------------------------------------------------------
 * Description  : Base DSP encode, this is the default implementation when
 *                minimal configuration is required on the DSP. All this
 *                does is indicate to the LPDSP32 program that it is time to
 *                do something.
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : This implementation may be enough if no specific handling
 *                is required however each codec implementor should consider
 *                if this is needed
 * ------------------------------------------------------------------------- */
static void baseDSPEncode(CODEC codec)
{
    pBaseDSPCodec object __attribute__ ((unused)) = getDSPCodec(codec);

    /* Tell the DSP there is data available to deal with. */
    SYSCTRL_DSS_CMD->DSS_CMD_1_ALIAS = DSS_CMD_1_BITBAND;
}

/* ----------------------------------------------------------------------------
 * Function     : baseDSPDecode
 * ----------------------------------------------------------------------------
 * Description  : Base DSP decode, this is the default implementation when
 *                minimal configuration is required on the DSP. All this
 *                does is indicate to the LPDSP32 program that it is time to
 *                do something.
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : This implementation may be enough if no specific handling
 *                is required however each codec implementor should consider
 *                if this is needed
 * ------------------------------------------------------------------------- */
static void baseDSPDecode(CODEC codec)
{
    pBaseDSPCodec object __attribute__ ((unused)) = getDSPCodec(codec);

    /* Tell the DSP there is data available to deal with. */
    SYSCTRL_DSS_CMD->DSS_CMD_1_ALIAS = DSS_CMD_1_BITBAND;
}
