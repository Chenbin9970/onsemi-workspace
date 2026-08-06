/* ----------------------------------------------------------------------------
 * Copyright (c) 2015-2017 Semiconductor Components Industries, LLC (d/b/a
 * ON Semiconductor), All Rights Reserved
 *
 * Copyright (C) RivieraWaves 2009-2016
 *
 * This module is derived in part from example code provided by RivieraWaves
 * and as such the underlying code is the property of RivieraWaves [a member
 * of the CEVA, Inc. group of companies], together with additional code which
 * is the property of ON Semiconductor. The code (in whole or any part) may not
 * be redistributed in any form without prior written permission from
 * ON Semiconductor.
 *
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 *
 * This is Reusable Code.
 *
 * ----------------------------------------------------------------------------
 * ble_basc.h
 * - Bluetooth battery client header
 * ----------------------------------------------------------------------------
 * $Revision: 1.1 $
 * $Date: 2019/06/03 15:11:13 $
 * ------------------------------------------------------------------------- */

#ifndef BLE_BASC_H
#define BLE_BASC_H

/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

/* ----------------------------------------------------------------------------
 * Include files
 * --------------------------------------------------------------------------*/
#include <basc.h>

/* ----------------------------------------------------------------------------
 * Global variables and types
 * --------------------------------------------------------------------------*/
typedef struct
{
    struct gapm_profile_added_ind profile_added_ind;
    uint32_t battLevelReqTimeout;
    uint8_t bas_nb[BLE_CONNECTION_MAX];
    bool enabled[BLE_CONNECTION_MAX];
    uint8_t batt_lvl[BLE_CONNECTION_MAX][BASC_NB_BAS_INSTANCES_MAX];
    uint8_t ntf_cfg[BLE_CONNECTION_MAX][BASC_NB_BAS_INSTANCES_MAX];
    uint8_t req_ntf_cfg[BLE_CONNECTION_MAX][BASC_NB_BAS_INSTANCES_MAX];
    struct prf_char_pres_fmt char_pres_format[BLE_CONNECTION_MAX][BASC_NB_BAS_INSTANCES_MAX];
    struct bas_content bas[BLE_CONNECTION_MAX][BASC_NB_BAS_INSTANCES_MAX];
} BASC_Env_t;

enum basc_app_msg_id
{
    BASC_BATT_LEVEL_REQ_TIMEOUT = TASK_FIRST_MSG(TASK_ID_BASC) + 50
};

/* ----------------------------------------------------------------------------
 * Function prototype definitions
 * --------------------------------------------------------------------------*/
void BASC_Initialize(void);

void BASC_EnableReq(uint8_t conidx);

void BASC_ReadInfoReq(uint8_t conidx, uint8_t bas_nb, uint8_t info);

void BASC_BattLevelNtfCfgReq(uint8_t conidx, uint8_t bas_nb, uint8_t ntf_cfg);

void BASC_RequestBattLevelOnTimeout(uint32_t timeout_ms);

uint8_t BASC_GetLastBatteryLevel(uint8_t conidx, uint8_t bas_nb);

const BASC_Env_t* BASC_GetEnv(void);

void BASC_MsgHandler(ke_msg_id_t const msg_id, void const *param,
                     ke_task_id_t const dest_id, ke_task_id_t const src_id);

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* BLE_BASC_H */
