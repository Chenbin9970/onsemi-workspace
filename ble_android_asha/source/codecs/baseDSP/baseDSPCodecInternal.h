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

#ifndef BASEDSPCODECINTERNAL_H_
#define BASEDSPCODECINTERNAL_H_

#include <stdint.h>

#include "codecs/base/baseCodec.h"
#include "codecs/codecInternal.h"

/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

typedef struct _baseDSPCodec
{
    volatile struct _codec parent;
    volatile uint32_t dspStatus;
    volatile uint32_t dspError;
    volatile uint32_t dspSignature;
} *pBaseDSPCodec;

pBaseDSPCodec getDSPCodec(CODEC codec);

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* BASEDSPCODECINTERNAL_H_ */
