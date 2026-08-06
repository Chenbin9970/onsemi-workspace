/* ----------------------------------------------------------------------------
 * Copyright (c) 2018 Semiconductor Components Industries, LLC (d/b/a
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
 * app_basc.c
 * - BASC Application-specific source file
 * ------------------------------------------------------------------------- */
#include <app.h>
#include <app_basc.h>
#include <printf.h>

/* ----------------------------------------------------------------------------
 * Function      : void APP_BASC_BattLevelInd_Handler(
 *                                     ke_msg_id_t const msg_id,
                                       void const *param,
                                       ke_task_id_t const dest_id,
                                       ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Battery level indication handler. Print peer device battery
 *                 level over UART.
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameter
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void APP_BASC_BattLevelInd_Handler(ke_msg_id_t const msg_id,
                                   void const *param,
                                   ke_task_id_t const dest_id,
                                   ke_task_id_t const src_id)
{
    PRINTF("__Battery level\n(peer=%d): %d%%\n\r", (uint8_t)KE_IDX_GET(src_id),
           BASC_GetLastBatteryLevel((uint8_t)KE_IDX_GET(src_id), 0));
}

