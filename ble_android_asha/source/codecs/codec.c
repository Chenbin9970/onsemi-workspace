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
#include <stdbool.h>
#include <string.h>

#include "codecs/codec.h"
#include "codecs/codecInternal.h"

#define SIGNATURE    (0x000C0DEC)

/* ----------------------------------------------------------------------------
 * Function     : getCodec
 * ----------------------------------------------------------------------------
 * Description  : Retrieve the codec control block for a given handle. This
 *                also performs some simple checking to ensure the block has
 *                not been corrupted.
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : Returns a pointer to a codec control block that can be
 *                used in the other methods
 * Assumptions  : This is the primary way for methods to access the codec
 *                control block
 * ------------------------------------------------------------------------- */
pCodec getCodec(CODEC codec)
{
    if (codecIsCodec(codec))
    {
        return ((pCodec)codec);
    }

    /* not a real codec, for now we'll treat this as a fatal code error */
    return (NULL);
}

/* ----------------------------------------------------------------------------
 * Function     : codecIsCodec
 * ----------------------------------------------------------------------------
 * Description  : Helper method to establish if a given handle actually
 *                represents a valid codec control block
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : True if the provided codec is valid, false otherwise
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
uint32_t codecIsCodec(CODEC codec)
{
    if (codec == NULL)
    {
        return (false);
    }

    pCodec object = (pCodec)codec;
    if (object->signature != SIGNATURE)
    {
        return (false);
    }

    return (object->endSignature == SIGNATURE);
}

/* ----------------------------------------------------------------------------
 * Function     : codecDestructor
 * ----------------------------------------------------------------------------
 * Description  : Delegate method to safely call the subclass destructor
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void codecDestructor(CODEC *codec)
{
    pCodec object = getCodec(*codec);
    return (object->destructor(*codec));
}

/* ----------------------------------------------------------------------------
 * Function     : codecInitialise
 * ----------------------------------------------------------------------------
 * Description  : Delegate method to safely call the subclass initialiser
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void codecInitialise(CODEC codec)
{
    pCodec object = getCodec(codec);
    object->initialise(codec);
}

/* ----------------------------------------------------------------------------
 * Function     : codecGetType
 * ----------------------------------------------------------------------------
 * Description  : Delegate method to safely call the subclass getType
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : the registered type of the codec
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
KnownCodecs codecGetType(CODEC codec)
{
    pCodec object = getCodec(codec);
    return (object->getType(codec));
}

/* ----------------------------------------------------------------------------
 * Function     : codecSetStatusBuffer
 * ----------------------------------------------------------------------------
 * Description  : Delegate method to safely set the status buffer
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void codecSetStatusBuffer(CODEC codec, unsigned char *baseAddress,
                          uint32_t size)
{
    pCodec object = getCodec(codec);
    object->setStatusBuffer(codec, baseAddress, size);
}

/* ----------------------------------------------------------------------------
 * Function     : codecSetInputBuffer
 * ----------------------------------------------------------------------------
 * Description  : Delegate method to safely set the input buffer
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void codecSetInputBuffer(CODEC codec, unsigned char *baseAddress, uint32_t size)
{
    pCodec object = getCodec(codec);
    object->setInputBuffer(codec, baseAddress, size);
}

/* ----------------------------------------------------------------------------
 * Function     : codecSetOutputBuffer
 * ----------------------------------------------------------------------------
 * Description  : Delegate method to safely set the output buffer
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void codecSetOutputBuffer(CODEC codec, unsigned char *baseAddress,
                          uint32_t size)
{
    pCodec object = getCodec(codec);
    object->setOutputBuffer(codec, baseAddress, size);
}

/* ----------------------------------------------------------------------------
 * Function     : codecGetOutputBufferUsed
 * ----------------------------------------------------------------------------
 * Description  : Delegate method to safely get the number of used bytes
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : The number of bytes used in the output buffer
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
uint32_t codecGetOutputBufferUsed(CODEC codec)
{
    pCodec object = getCodec(codec);
    return (object->getOutputBufferUsed(codec));
}

/* ----------------------------------------------------------------------------
 * Function     : codecEncode
 * ----------------------------------------------------------------------------
 * Description  : Delegate method to safely prepare and invoke the encode
 *                method
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void codecEncode(CODEC codec)
{
    pCodec object = getCodec(codec);
    object->prepareEncode(codec);
    object->encode(codec);
}

/* ----------------------------------------------------------------------------
 * Function     : codecDecode
 * ----------------------------------------------------------------------------
 * Description  : Delegate method to safely prepare and invoke the decode
 *                method
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void codecDecode(CODEC codec)
{
    pCodec object = getCodec(codec);
    object->prepareDecode(codec);
    object->decode(codec);
}

/* ----------------------------------------------------------------------------
 * Function     : codecConfigure
 * ----------------------------------------------------------------------------
 * Description  : Delegate method to safely prepare and invoke the configure
 *                method
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void codecConfigure(CODEC codec)
{
    pCodec object = getCodec(codec);
    object->prepareConfigure(codec);
    object->configure(codec);
}

/* ----------------------------------------------------------------------------
 * Function     : codecSetNotifier
 * ----------------------------------------------------------------------------
 * Description  : Delegate method to safely set the notifier method for the
 *                codec, this notifier is invoked on completion of an
 *                operation.
 * Inputs       : codec : An opaque handle to a codec control block
 *                notifier : a function pointer that refers to the notifier
 *                the codec requires
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void codecSetNotifier(CODEC codec, void (*notifier)(void))
{
    pCodec object = getCodec(codec);
    object->notifyComplete = notifier;
}

/* ----------------------------------------------------------------------------
 * Function     : codecSetParameters
 * ----------------------------------------------------------------------------
 * Description  : Delegate method to safely set the operational parameters
 * Inputs       : codec : An opaque handle to a codec control block
 *                params : the operational parameters block that should be
 *                set up
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
void codecSetParameters(CODEC codec, OperationParameters *params)
{
    pCodec object = getCodec(codec);
    memcpy(&object->operation, params, sizeof(OperationParameters));
}

/* ----------------------------------------------------------------------------
 * Function     : codecGetParameters
 * ----------------------------------------------------------------------------
 * Description  : Delegate method to safely get the operational parameters
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : returns a pointer to the operational parameters block
 *                associated with this codec
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
OperationParameters * codecGetParameters(CODEC codec)
{
    pCodec object = getCodec(codec);
    return (&object->operation);
}
