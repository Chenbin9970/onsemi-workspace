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
 * aesavs.h
 * - Testsuite based on AESAVS test vectors.
 *   implemented:
 *      - variable plain text with a fixed key (VarTxt)
 *      - variable key with a fixed plain text (VarKey)
 * ----------------------------------------------------------------------------
 * $Revision: 1.1 $
 * $Date: 2018/08/29 19:16:29 $
 * ------------------------------------------------------------------------- */
#ifndef AESAVS_H_
#define AESAVS_H_

#include <aes128.h>

/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

#define AESAVS_MAX_TEST_VECTORS        128

/* Reduce the number of test vectors.  */
#define AES128_NB_OF_TEST_VECTORS      AESAVS_MAX_TEST_VECTORS

/* Pointer to the exchange memory */
#define AES128_EM_PTR                  (BB_DRAM0_BASE + 0x200)

/* AES test vectors to pass */
#define AES128_ECB_TEST_VAR_TXT
#define AES128_ECB_TEST_VAR_KEY
#if !defined (AES128_ECB_TEST_VAR_TXT) && !defined (AES128_ECB_TEST_VAR_KEY)
    #warning \
    "One of AES128_ECB_TEST_VAR_TXT or AES128_ECB_TEST_VAR_KEY must be defined. Force AES128_ECB_TEST_VAR_TXT by default"
#define AES128_ECB_TEST_VAR_TXT
#endif    /* if !defined (AES128_ECB_TEST_VAR_TXT) && !defined (AES128_ECB_TEST_VAR_KEY) */

#if !defined (AES128_NB_OF_TEST_VECTORS)
    #warning " AES128_NB_OF_TEST_VECTORS not defined. Force it to AESAVS_MAX_TEST_VECTORS"
#define AES128_NB_OF_TEST_VECTORS        AESAVS_MAX_TEST_VECTORS
#elif defined (AES128_NB_OF_TEST_VECTORS) && (AES128_NB_OF_TEST_VECTORS >  AESAVS_MAX_TEST_VECTORS)
    #error "Nb of AES test vectors greater than  AESAVS_MAX_TEST_VECTORS"
#endif    /* if !defined (AES128_NB_OF_TEST_VECTORS) */

/* ----------------------------------------------------------------------------
 * Typedef declaration
 * ------------------------------------------------------------------------- */
typedef enum aes_process
{
    aes128_ecb_hw_cipher   = 0,
    aes128_ecb_sw_cipher   = 1,
    aes128_ecb_sw_decipher = 2,
    aes128_ecb_init        = 3
} t_aes_process;

typedef struct t_aesavs_vectors
{
    struct
    {
        struct
        {
            struct
            {
#if defined (AES128_ECB_TEST_VAR_TXT)
                uint8_t var_txt[AESAVS_MAX_TEST_VECTORS][AES128_KEY_LENGTH];
#endif    /* if defined (AES128_ECB_TEST_VAR_TXT) */
#if defined (AES128_ECB_TEST_VAR_KEY)
                uint8_t var_key[AESAVS_MAX_TEST_VECTORS][AES128_KEY_LENGTH];
#endif    /* if defined (AES128_ECB_TEST_VAR_KEY) */
            } test;
        } ecb;
    } aes128;
} t_aesavs_vectors;

typedef union t_data
{
    struct
    {
        uint8_t round_key[(AES128_ROUNDS + 1) * AES128_KEY_LENGTH];
        uint8_t plain_text[AES128_KEY_LENGTH];
        uint8_t cipher_text[AES128_KEY_LENGTH];
    } fields;
    uint32_t mem_block[((AES128_ROUNDS + 3) * AES128_KEY_LENGTH) / 4];
} t_data;

/* ----------------------------------------------------------------------------
 * Function Declaration
 * ------------------------------------------------------------------------- */
int32_t AES_test(t_aes_process process);

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* AESAVS_H_ */
