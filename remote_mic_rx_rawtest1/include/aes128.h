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
 * ----------------------------------------------------------------------------
 * aes128.h
 * - AES128 algorithm header file.
 * ----------------------------------------------------------------------------
 * $Revision: 1.1 $
 * $Date: 2018/08/29 19:16:29 $
 * ------------------------------------------------------------------------- */
#ifndef AES128_H
#define AES128_H
#include <rsl10.h>

/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

/* ----------------------------------------------------------------------------
 * define and enum declaration
 * ------------------------------------------------------------------------- */

/* The number of rounds in AES Cipher. */
#define AES128_ROUNDS                      10

/* Key length in bytes [128 bit] */
#define AES128_KEY_LENGTH                  16

/* ----------------------------------------------------------------------------
 * typedef declaration
 * ------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------------
 * Forward declaration
 * ------------------------------------------------------------------------- */
void aes128_hw_cipher(uint8_t *const key, const uint32_t em, uint8_t *block);

void aes128_sw_cipher(uint8_t *const round_key, uint8_t *block);

void aes128_sw_decipher(uint8_t *const round_key, uint8_t *block);

void aes128_key_expand(uint8_t *round_key);

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* AES128_H */
