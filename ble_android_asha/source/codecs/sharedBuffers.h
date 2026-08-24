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

#ifndef SHAREDBUFFERS_H_
#define SHAREDBUFFERS_H_

/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

#define CODEC_CONFIGURATION_SIZE    (0x200)
#define CODEC_INPUT_SIZE            (80)
#define CODEC_OUTPUT_SIZE           (320)

typedef struct _sharedMemory
{
    unsigned char configuration[CODEC_CONFIGURATION_SIZE];
    unsigned char input[CODEC_INPUT_SIZE];
    unsigned char output[CODEC_OUTPUT_SIZE];
} *pSharedMemory;

extern struct _sharedMemory Buffer;

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* SHAREDBUFFERS_H_ */
