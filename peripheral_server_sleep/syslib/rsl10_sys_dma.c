/* ----------------------------------------------------------------------------
 * Copyright (c) 2015-2017 Semiconductor Components Industries, LLC (d/b/a ON
 * Semiconductor), All Rights Reserved
 *
 * This code is the property of ON Semiconductor and may not be redistributed
 * in any form without prior written permission from ON Semiconductor.
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 *
 * This is Reusable Code.
 *
 * ----------------------------------------------------------------------------
 * rsl10_sys_dma.c
 * - System library Direct Memory Access (DMA) peripheral support
 * ----------------------------------------------------------------------------
 * $Revision: 1.6 $
 * $Date: 2017/07/07 21:49:10 $
 * ------------------------------------------------------------------------- */

#include <rsl10.h>

/* ----------------------------------------------------------------------------
 * Function      : void Sys_DMA_ChannelConfig(uint32_t num,
 *                                            uint32_t cfg,
 *                                            uint32_t transferLength,
 *                                            uint32_t counterInt,
 *                                            uint32_t srcAddr,
 *                                            uint32_t destAddr)
 * ----------------------------------------------------------------------------
 * Description   : Configure the DMA channels for a data transfer
 * Inputs        : - num            - DMA channel number
 *                 - cfg         - Configuration of the DMA transfer
 *                                    behavior; use
 *                                    DMA_DEST_ADDR_STEP_SIZE_*,
 *                                    DMA_SRC_ADDR_STEP_SIZE_*,
 *                                    DMA_DEST_ADDR_[POS | NEG],
 *                                    DMA_SRC_ADDR_[POS | NEG],
 *                                    DMA_[LITTLE | BIG]_ENDIAN,
 *                                    DMA_DISABLE_INT_[DISABLE | ENABLE],
 *                                    DMA_ERROR_INT_[DISABLE | ENABLE],
 *                                    DMA_COMPLETE_INT_[DISABLE | ENABLE],
 *                                    DMA_COUNTER_INT_[DISABLE | ENABLE],
 *                                    DMA_START_INT_[DISABLE | ENABLE],
 *                                    DMA_DEST_WORD_SIZE_*,
 *                                    DMA_SRC_WORD_SIZE_*,
 *                                    DMA_DEST_[I2C | SPI0 | SPI1 | PCM |
 *                                    UART | ASRC],
 *                                    DMA_SRC_[I2C | SPI0 | SPI1 | PCM | UART |
 *                                    ASRC],
 *                                    DMA_PRIORITY_*,
 *                                    DMA_TRANSFER_[P | M]_TO_[P | M]
 *                                    DMA_DEST_ADDR_[STATIC | INC],
 *                                    DMA_SRC_ADDR_[STATIC | INC],
 *                                    DMA_ADDR_[CIRC | LIN],
 *                                    DMA_[DISABLE | ENABLE]
 *                 - transferLength - Configuration of the DMA transfer length
 *                 - counterInt     - Configuration of when the counter
 *                                    interrupt will occur during the transfer
 *                 - srcAddr        - Base source address for the DMA transfer
 *                 - destAddr       - Base destination address for the DMA
 *                                    transfer
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void Sys_DMA_ChannelConfig(uint32_t num, uint32_t cfg,
                           uint32_t transferLength, uint32_t counterInt,
                           uint32_t srcAddr, uint32_t destAddr)
{
    /* Disable the DMA channel to ensure that it is safe to update the DMA
     * registers */
    DMA->CTRL0[num] = DMA_DISABLE;

    /* Setup the base addresses for the source and destination */
    DMA->SRC_BASE_ADDR[num] = srcAddr;
    DMA->DEST_BASE_ADDR[num] = destAddr;

    /* Setup the transfer length and transfer counter interrupt setting */
    DMA->CTRL1[num] = (((transferLength << DMA_CTRL1_TRANSFER_LENGTH_Pos) &
                                        DMA_CTRL1_TRANSFER_LENGTH_Mask)     |
                        ((counterInt << DMA_CTRL1_COUNTER_INT_VALUE_Pos) &
                                        DMA_CTRL1_COUNTER_INT_VALUE_Mask));

    /* Configure the DMA channel */
    DMA->CTRL0[num] = (cfg & (DMA_CTRL0_DEST_ADDR_STEP_SIZE_Mask         |
                              DMA_CTRL0_SRC_ADDR_STEP_SIZE_Mask          |
                             (1U << DMA_CTRL0_DEST_ADDR_STEP_MODE_Pos)   |
                             (1U << DMA_CTRL0_SRC_ADDR_STEP_MODE_Pos)    |
                             (1U << DMA_CTRL0_BYTE_ORDER_Pos)            |
                             (1U << DMA_CTRL0_DISABLE_INT_ENABLE_Pos)    |
                             (1U << DMA_CTRL0_ERROR_INT_ENABLE_Pos)      |
                             (1U << DMA_CTRL0_COMPLETE_INT_ENABLE_Pos)   |
                             (1U << DMA_CTRL0_COUNTER_INT_ENABLE_Pos)    |
                             (1U << DMA_CTRL0_START_INT_ENABLE_Pos)      |
                              DMA_CTRL0_DEST_WORD_SIZE_Mask              |
                              DMA_CTRL0_SRC_WORD_SIZE_Mask               |
                              DMA_CTRL0_DEST_SELECT_Mask                 |
                              DMA_CTRL0_SRC_SELECT_Mask                  |
                              DMA_CTRL0_CHANNEL_PRIORITY_Mask            |
                              DMA_CTRL0_TRANSFER_TYPE_Mask               |
                             (1U << DMA_CTRL0_DEST_ADDR_INC_Pos)         |
                             (1U << DMA_CTRL0_SRC_ADDR_INC_Pos)          |
                             (1U << DMA_CTRL0_ADDR_MODE_Pos)             |
                             (1U << DMA_CTRL0_ENABLE_Pos)));
}
