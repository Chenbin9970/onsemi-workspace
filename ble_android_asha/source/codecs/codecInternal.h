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

#ifndef BASECODECINTERNAL_H_
#define BASECODECINTERNAL_H_

#include <stdint.h>

/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

typedef struct _codec
{
    uint32_t signature;

    KnownCodecs type;
    OperationParameters operation;

    uint32_t statusBufferAddress;
    uint32_t statusBufferSize;

    uint32_t inputBufferAddress;
    uint32_t inputBufferSize;

    uint32_t outputBufferAddress;
    uint32_t outputBufferMaxSize;
    uint32_t outputBufferUsedSize;

    /* provide interface to codec specific implementation */
    void (*destructor)(CODEC *codec);
    void (*initialise)(CODEC codec);

    KnownCodecs (*getType)(CODEC codec);

    void (*prepareConfigure)(CODEC codec);
    void (*prepareEncode)(CODEC codec);
    void (*prepareDecode)(CODEC codec);

    void (*setStatusBuffer)(CODEC codec, unsigned char *baseAddress,
                            uint32_t size);
    void (*setInputBuffer)(CODEC codec, unsigned char *baseAddress,
                           uint32_t size);
    void (*setOutputBuffer)(CODEC codec, unsigned char *baseAddress,
                            uint32_t size);
    uint32_t (*getOutputBufferUsed)(CODEC codec);

    void (*configure)(CODEC codec);
    void (*encode)(CODEC codec);
    void (*decode)(CODEC codec);

    void (*notifyComplete)(void);

    uint32_t endSignature;
} *pCodec;

pCodec getCodec(CODEC codec);

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* BASECODECINTERNAL_H_ */
