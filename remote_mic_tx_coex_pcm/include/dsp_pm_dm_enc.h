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
 * ----------------------------------------------------------------------------
 * dsp_pm_dm_enc.h
 * - Header file for DSP codec constants
 * ------------------------------------------------------------------------- */
#ifndef DSP_PM_DM_ENC_H
#define DSP_PM_DM_ENC_H
/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

#define MEM_PM_SIZE  5850
#define MEM_PM_EXTRA  0
#define MEM_PM_LINES  1170

#define MEM_DMA_SIZE  1764
#define MEM_DMA_LINES  441.0
#define MEM_DMB_SIZE  8992
#define MEM_DMB_LINES  2248.0

extern uint8_t LPDSP32_Data_low_DM_enc[];
extern uint8_t LPDSP32_Prog_40bit_PM_enc[];
/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* ifndef DSP_PM_DM_ENC_H */
