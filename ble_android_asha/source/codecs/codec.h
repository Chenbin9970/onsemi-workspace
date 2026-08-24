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

#ifndef CODEC_H_
#define CODEC_H_

#include <stdint.h>

/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

/* Define the list of known codecs we currently support. Note that these have
 * been specifically set to require an "int" base type rather than "byte"
 */
typedef enum
{
    BASE = 0x01000000, BASE_DSP, ECHO, ECHO_DSP, G722_DSP, CELT_DSP, G722_PLC_DSP
} KnownCodecs;

/* There are various operations that may be requested, these are defined as
 * bit settings as indicated below
 */
typedef uint8_t EncodeDecode;
#define ENCODE_RESET    0x04
#define DECODE_RESET    0x08
#define ENCODE          0x01
#define DECODE          0x02
#define PACKED          0x10
#define ALT_PACKED      0x20
#define DECODE_PLC      0x40
#define CONFIGURE       0x80

/* The operational parameters define the parameters to be used for the
 * next operation
 */
typedef struct _OperationParameters
{
    EncodeDecode action;    /* the operation as defined above */
    uint8_t frameSize;      /* the frame size to be used */
    uint8_t blockSize;      /* the block size or subframe size */
    uint8_t modeAndChannel;    /* for now mode is lower 4 bits, channel is upper */
    uint32_t sampleRate;    /* sample rate associated with the encode/decode */
} OperationParameters;

/* All access to codecs is via the opaque CODEC type */
typedef void *CODEC;

void codecDestructor(CODEC *codec);

uint32_t codecIsCodec(CODEC codec);

void codecInitialise(CODEC codec);

KnownCodecs codecGetType(CODEC codec);

void codecSetStatusBuffer(CODEC codec, unsigned char *baseAddress,
                          uint32_t size);

void codecSetInputBuffer(CODEC codec, unsigned char *baseAddress,
                         uint32_t size);

void codecSetOutputBuffer(CODEC codec, unsigned char *baseAddress,
                          uint32_t size);

uint32_t codecGetOutputBufferUsed(CODEC codec);

void codecConfigure(CODEC codec);

void codecEncode(CODEC codec);

void codecDecode(CODEC codec);

void codecSetParameters(CODEC codec, OperationParameters *params);

OperationParameters * codecGetParameters(CODEC codec);

void codecSetNotifier(CODEC codec, void (*notifier)(void));

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* CODEC_H_ */
