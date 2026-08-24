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

#include "codecs/codec.h" //TODO: fix include path structure
#include "codecs/codecInternal.h"
#include "codecs/base/baseCodec.h"

#define SIGNATURE    (0x000C0DEC)

/* Define function prototypes for methods added to jump table */
static void baseDestructor(CODEC *codec);

static void baseInitialise(CODEC codec);

static KnownCodecs baseGetType(CODEC codec);

static void basePrepareConfigure(CODEC codec);

static void basePrepareEncode(CODEC codec);

static void basePrepareDecode(CODEC codec);

static void baseSetStatusBuffer(CODEC codec, unsigned char *baseAddress,
                                uint32_t size);

static void baseSetInputBuffer(CODEC codec, unsigned char *baseAddress,
                               uint32_t size);

static void baseSetOutputBuffer(CODEC codec, unsigned char *baseAddress,
                                uint32_t size);

static uint32_t baseGetOutputBufferUsed(CODEC codec);

static void baseConfigure(CODEC codec);

static void baseEncode(CODEC codec);

static void baseDecode(CODEC codec);

static void baseNotifyComplete(void);

/* ----------------------------------------------------------------------------
 * Function     : initialiseStructure
 * ----------------------------------------------------------------------------
 * Description  : Initialises a base codec control block. This sets the
 *                function pointers in the jump table to default values
 *                which can be overridden by any classes which want are
 *                derived from this
 * Inputs       : structure : A pointer to a codec control structure which
 *                is to be initialised
 * Outputs      : The structure object is initialised with default data.
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void initialiseStructure(pCodec structure)
{
    if (structure != NULL)
    {
        structure->signature = SIGNATURE;

        structure->type = BASE;

        structure->destructor          = &baseDestructor;
        structure->initialise          = &baseInitialise;
        structure->getType             = &baseGetType;
        structure->prepareConfigure    = &basePrepareConfigure;
        structure->prepareEncode       = &basePrepareEncode;
        structure->prepareDecode       = &basePrepareDecode;
        structure->setStatusBuffer     = &baseSetStatusBuffer;
        structure->setInputBuffer      = &baseSetInputBuffer;
        structure->setOutputBuffer     = &baseSetOutputBuffer;
        structure->getOutputBufferUsed = &baseGetOutputBufferUsed;
        structure->configure           = &baseConfigure;
        structure->encode = &baseEncode;
        structure->decode = &baseDecode;
        structure->notifyComplete      = &baseNotifyComplete;

        structure->endSignature        = SIGNATURE;
    }
}

/* ----------------------------------------------------------------------------
 * Function     : makeBaseCodec
 * ----------------------------------------------------------------------------
 * Description  : General constructor for a base codec, this uses malloc to
 *                allocate a block of memory which it then initialises as
 *                above.
 * Inputs       : None
 * Outputs      : An opaque CODEC object, effectively a handle that van be
 *                used in later operations on this codec
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
CODEC makeBaseCodec(void)
{
    pCodec codec = (pCodec)malloc(sizeof(struct _codec));
    initialiseStructure(codec);
    return (codec);
}

/* ----------------------------------------------------------------------------
 * Function     : populateBaseCodec
 * ----------------------------------------------------------------------------
 * Description  : As an alternative to allocating memory for a codec, this
 *                method is provided to allow a pre-allocated memory
 *                location to be set up as a codec
 * Inputs       : buffer : a pointer to a buffer in which to create and
 *                initialise the codec control block
 *                size : the size in bytes of the buffer
 * Outputs      : NULL if the size of the buffer is not large enough or a
 *                CODEC handle
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
CODEC populateBaseCodec(void *buffer, uint32_t size)
{
    if (sizeof(struct _codec) > size)
    {
        return (NULL);
    }
    initialiseStructure(buffer);
    return (buffer);
}

/* ----------------------------------------------------------------------------
 * Function     : baseDestructor
 * ----------------------------------------------------------------------------
 * Description  : Destructor for a codec, the complement of the
 *                makeBaseCodec method above.
 * Inputs       : codec : a pointer to a CODEC handle, effectively a pointer
 *                to a pointer
 * Outputs      : None
 * Assumptions  : Assumes the codec can be freed, the behaviour is
 *                undetermined if the codec being passed in was not
 *                allocated by the constructor above.
 * ------------------------------------------------------------------------- */
static void baseDestructor(CODEC *codec)
{
    if (codecIsCodec(*codec))
    {
        free(*codec);
        *codec = NULL;
    }
}

/* ----------------------------------------------------------------------------
 * Function     : baseInitialise
 * ----------------------------------------------------------------------------
 * Description  : Default initialise method, does nothing for a base codec.
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void baseInitialise(CODEC codec)
{
    /* the default behaviour is to do no preparation for the codec */
}

/* ----------------------------------------------------------------------------
 * Function     : baseGetType
 * ----------------------------------------------------------------------------
 * Description  : Returns the base type of the codec, this is a field within
 *                the codec control block
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : Returns the base type
 * Assumptions  : This is default behaviour which is not expected to be
 *                over-ridden
 * ------------------------------------------------------------------------- */
static KnownCodecs baseGetType(CODEC codec)
{
    pCodec ptr = getCodec(codec);
    return (ptr->type);
}

/* ----------------------------------------------------------------------------
 * Function     : basePrepareConfigure
 * ----------------------------------------------------------------------------
 * Description  : Prepares a codec for configuration, for the purposes of
 *                the base codec this simply performs some logging to show
 *                the operation is in progress
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void basePrepareConfigure(CODEC codec)
{
    /* Do nothing for now */
}

/* ----------------------------------------------------------------------------
 * Function     : basePrepareEncode
 * ----------------------------------------------------------------------------
 * Description  : Prepares a codec for encode, for the purposes of the base
 *                codec this simply performs some logging to show the
 *                operation is in progress
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void basePrepareEncode(CODEC codec)
{
    /* Do nothing for now */
}

/* ----------------------------------------------------------------------------
 * Function     : basePrepareDecode
 * ----------------------------------------------------------------------------
 * Description  : Prepares a codec for decode, for the purposes of the base
 *                codec this simply performs some logging to show the
 *                operation is in progress
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : None
 * ------------------------------------------------------------------------- */
static void basePrepareDecode(CODEC codec)
{
    /* Do nothing for now */
}

/* ----------------------------------------------------------------------------
 * Function     : baseSetStatusBuffer
 * ----------------------------------------------------------------------------
 * Description  : Sets the status buffer fields in the codec control block
 * Inputs       : codec : An opaque handle to a codec control block
 *                baseAddress : the base address of the status buffer
 *                size : the size of the buffer being used
 * Outputs      : None
 * Assumptions  : This is default behaviour which is not expected to be
 *                over-ridden
 * ------------------------------------------------------------------------- */
static void baseSetStatusBuffer(CODEC codec, unsigned char *baseAddress,
                                uint32_t size)
{
    pCodec ptr = getCodec(codec);
    ptr->statusBufferAddress = (uint32_t)baseAddress;
    ptr->statusBufferSize    = size;
}

/* ----------------------------------------------------------------------------
 * Function     : baseSetInputBuffer
 * ----------------------------------------------------------------------------
 * Description  : Sets the input buffer fields in the codec control block
 * Inputs       : codec : An opaque handle to a codec control block
 *                baseAddress : the base address of the input buffer
 *                size : the size of the buffer being used
 * Outputs      : None
 * Assumptions  : This is default behaviour which is not expected to be
 *                over-ridden
 * ------------------------------------------------------------------------- */
static void baseSetInputBuffer(CODEC codec, unsigned char *baseAddress,
                               uint32_t size)
{
    pCodec ptr = getCodec(codec);
    ptr->inputBufferAddress = (uint32_t)baseAddress;
    ptr->inputBufferSize    = size;
}

/* ----------------------------------------------------------------------------
 * Function     : baseSetOutputBuffer
 * ----------------------------------------------------------------------------
 * Description  : Sets the output buffer fields in the codec control block
 * Inputs       : codec : An opaque handle to a codec control block
 *                baseAddress : the base address of the output buffer
 *                size : the size of the buffer being used
 * Outputs      : None
 * Assumptions  : This is default behaviour which is not expected to be
 *                over-ridden
 * ------------------------------------------------------------------------- */
static void baseSetOutputBuffer(CODEC codec, unsigned char *baseAddress,
                                uint32_t size)
{
    pCodec ptr = getCodec(codec);
    ptr->outputBufferAddress = (uint32_t)baseAddress;
    ptr->outputBufferMaxSize = size;
}

/* ----------------------------------------------------------------------------
 * Function     : baseGetOutputBufferUsed
 * ----------------------------------------------------------------------------
 * Description  : Fetch the size of the output buffer which has been used
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : The size of the used output buffer in bytes
 * Assumptions  : This is default behaviour which is not expected to be
 *                over-ridden
 * ------------------------------------------------------------------------- */
static uint32_t baseGetOutputBufferUsed(CODEC codec)
{
    pCodec ptr = getCodec(codec);
    return (ptr->outputBufferUsedSize);
}

/* ----------------------------------------------------------------------------
 * Function     : baseConfigure
 * ----------------------------------------------------------------------------
 * Description  : Configure the codec, this is an empty implementation as no
 *                work needs to be done for the base codec
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : Each codec implementation should supply its own configure
 *                method
 * ------------------------------------------------------------------------- */
static void baseConfigure(CODEC codec)
{
    pCodec ptr __attribute__ ((unused)) = getCodec(codec);

    /* empty implementation for base class */
}

/* ----------------------------------------------------------------------------
 * Function     : baseEncode
 * ----------------------------------------------------------------------------
 * Description  : perform an encode operation using the codec, this is an
 *                empty implementation as no work needs to be done for the
 *                base codec
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : Each codec implementation should supply its own encode
 *                method
 * ------------------------------------------------------------------------- */
static void baseEncode(CODEC codec)
{
    pCodec ptr __attribute__ ((unused)) = getCodec(codec);

    /* empty implementation for base class */
}

/* ----------------------------------------------------------------------------
 * Function     : baseDecode
 * ----------------------------------------------------------------------------
 * Description  : perform a decode operation using the codec, this is an
 *                empty implementation as no work needs to be done for the
 *                base codec
 * Inputs       : codec : An opaque handle to a codec control block
 * Outputs      : None
 * Assumptions  : Each codec implementation should supply its own decode
 *                method
 * ------------------------------------------------------------------------- */
static void baseDecode(CODEC codec)
{
    pCodec ptr __attribute__ ((unused)) = getCodec(codec);

    /* empty implementation for base class */
}

/* ----------------------------------------------------------------------------
 * Function     : baseNotifyComplete
 * ----------------------------------------------------------------------------
 * Description  : Notify completion of an operation, this is an empty
 *                implementation as no work needs to be done for the base
 *                codec
 * Inputs       : None
 * Outputs      : None
 * Assumptions  : this will be over-ridden by the subclasses
 * ------------------------------------------------------------------------- */
static void baseNotifyComplete(void)
{
    /* empty implementation for base class */
}
