/* ----------------------------------------------------------------------------
 * Copyright (c) 2018 Semiconductor Components Industries, LLC (d/b/a
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
 * app.h
 * - Main application header
 * ------------------------------------------------------------------------- */

#ifndef APP_H
#define APP_H

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
#include <rsl10.h>
#include <rsl10_protocol.h>
#include <ble_gap.h>
#include <ble_gatt.h>
#include <ble_l2c.h>
#include <msg_handler.h>
#include <ble_bass.h>
#include <ble_diss.h>
#include <ble_asha.h>
#include "app_bass.h"
#include "app_trace.h"

/* ----------------------------------------------------------------------------
 * Defines
 * --------------------------------------------------------------------------*/

/* APP Task messages */
enum appm_msg
{
    APPM_DUMMY_MSG = TASK_FIRST_MSG(TASK_ID_APP),
    APP_BATT_LEVEL_LOW,
    APP_7100_HB_TIMER
};

#define APP_IDX_MAX                     BLE_CONNECTION_MAX /* Number of APP Task Instances */

#define APP_BLE_DEV_PARAM_SOURCE        FLASH_PROVIDED_or_DFLT /* or APP_PROVIDED  */

/* If APP_BD_ADDRESS_TYPE == GAPM_CFG_ADDR_PUBLIC and APP_DEVICE_PARAM_SRC == FLASH_PROVIDED_or_DFLT
 * the bluetooth address is loaded from FLASH NVR3. Otherwise, this address is used. */
#define APP_BD_ADDRESS_TYPE              GAPM_CFG_ADDR_PUBLIC /* or GAPM_CFG_ADDR_PRIVATE */
#define APP_BD_ADDRESS                   { 0x94, 0x11, 0x22, 0xff, 0xbb, 0xD5 }
#define APP_NB_PEERS                     1 /* Always 1 */

/* The number of standard profiles and custom services added in this application */
#define APP_NUM_STD_PRF                 2
#define APP_NUM_CUSTOM_SVC              1

#define OUTPUT_POWER_DBM                0  /* RF output power in dBm */
#define RADIO_CLOCK_ACCURACY            20 /* RF Oscillator accuracy in ppm */

/* Charge pump clock prescale value. With SLOWCLK = 2 MHz, CPCLK = 166 kHz */
#define CPCLK_PRESCALE_12               ((uint32_t)(0XBU << \
                                        CLK_DIV_CFG2_CPCLK_PRESCALE_Pos))

/* DIO number that is used for easy re-flashing (recovery mode) */
#define RECOVERY_DIO                    12

/* Timer setting in units of 10ms (kernel timer resolution) */
#define TIMER_SETTING_MS(MS)            (MS / 10)
#define TIMER_SETTING_S(S)              (S * 100)

/* Advertising data is composed by device name and company id */
#define APP_DEVICE_NAME                 "ble_android_asha"

#define APP_DEVICE_NAME_LEN             (sizeof(APP_DEVICE_NAME) - 1)

/* Manufacturer info (ON SEMICONDUCTOR Company ID) */
#define APP_COMPANY_ID                  {0x62, 0x3}
#define APP_COMPANY_ID_LEN              2

#define APP_DEVICE_APPEARANCE           0
#define APP_PREF_SLV_MIN_CON_INTERVAL   8
#define APP_PREF_SLV_MAX_CON_INTERVAL   10
#define APP_PREF_SLV_LATENCY            0
#define APP_PREF_SLV_SUP_TIMEOUT        200

/* ASHA implementation-specific definitions */
#define APP_ASHA_HI_SYNC_ID             {0x62, 0x03, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77}
#define APP_ASHA_DEVICE_CAPABILITIES    (ASHA_CAPABILITIES_MONAURAL | ASHA_CAPABILITIES_SIDE)
#define APP_ASHA_SUPPORTED_CODECS       ASHA_CODEC_ID_G722_16KHZ
#define APP_ASHA_RENDER_DELAY           0 //0ms
#define APP_ASHA_PREPARATION_DELAY      0 //0ms
#define APP_ASHA_FEATURE_MAP            ASHA_FEATURE_MAP_LE_COC_SUPPORTED
#define APP_ASHA_VERSION                0x01

/* Application-provided IRK */
#define APP_IRK                         { 0x01, 0x23, 0x45, 0x68, 0x78, 0x9a, \
                                          0xbc, 0xde, 0x01, 0x23, 0x45, 0x68, \
                                          0x78, 0x9a, 0xbc, 0xde }

/* Application-provided CSRK */
#define APP_CSRK                        { 0x01, 0x23, 0x45, 0x68, 0x78, 0x9a, \
                                          0xbc, 0xde, 0x01, 0x23, 0x45, 0x68, \
                                          0x78, 0x9a, 0xbc, 0xde }

/* Application-provided private key */
#define APP_PRIVATE_KEY                 { 0xEC, 0x89, 0x3C, 0x11, 0xBB, 0x2E, \
                                          0xEB, 0x5C, 0x80, 0x88, 0x63, 0x57, \
                                          0xCC, 0xE2, 0x05, 0x17, 0x20, 0x75, \
                                          0x5A, 0x26, 0x3E, 0x8D, 0xCF, 0x26, \
                                          0x63, 0x1D, 0x26, 0x0B, 0xCE, 0x4D, \
                                          0x9E, 0x07 }

/* Application-provided public key X */
#define APP_PUBLIC_KEY_X                { 0x56, 0x09, 0x79, 0x1D, 0x5A, 0x5F, \
                                          0x4A, 0x5C, 0xFE, 0x89, 0x56, 0xEC, \
                                          0xE6, 0xF7, 0x92, 0x21, 0xAC, 0x93, \
                                          0x99, 0x10, 0x51, 0x82, 0xF4, 0xDD, \
                                          0x84, 0x07, 0x50, 0x99, 0xE7, 0xC2, \
                                          0xF1, 0xC8 }

/* Application-provided public key Y */
#define APP_PUBLIC_KEY_Y                { 0x40, 0x84, 0xB4, 0xA6, 0x08, 0x67, \
                                          0xFD, 0xAC, 0x81, 0x5D, 0xB0, 0x41, \
                                          0x27, 0x75, 0x9B, 0xA7, 0x92, 0x57, \
                                          0x0C, 0x44, 0xB1, 0x57, 0x7C, 0x76, \
                                          0x5B, 0x56, 0xF0, 0xBA, 0x03, 0xF4, \
                                          0xAA, 0x67 }
/* --------------------------------------------------------------------------
 *  Device Information used for Device Information Server Service (DISS)
 * ----------------------------------------------------------------------- */
/* Manufacturer Name Value */
#define APP_DIS_MANUFACTURER_NAME       ("Peripheral_HeartRate")
#define APP_DIS_MANUFACTURER_NAME_LEN   (20)

/* Model Number String Value */
#define APP_DIS_MODEL_NB_STR            ("RW-BLE-1.0")
#define APP_DIS_MODEL_NB_STR_LEN        (10)

/* Serial Number */
#define APP_DIS_SERIAL_NB_STR           ("1.0.0.0-LE")
#define APP_DIS_SERIAL_NB_STR_LEN       (10)

/* Firmware Revision */
#define APP_DIS_FIRM_REV_STR            ("6.1.2")
#define APP_DIS_FIRM_REV_STR_LEN        (5)

/* System ID Value - LSB -> MSB */
#define APP_DIS_SYSTEM_ID               ("\x12\x34\x56\xFF\xFE\x9A\xBC\xDE")
#define APP_DIS_SYSTEM_ID_LEN           (8)

/* Hardware Revision String */
#define APP_DIS_HARD_REV_STR            ("1.0.0")
#define APP_DIS_HARD_REV_STR_LEN        (5)

/* Software Revision String */
#define APP_DIS_SW_REV_STR              ("6.3.0")
#define APP_DIS_SW_REV_STR_LEN          (5)

/* IEEE */
#define APP_DIS_IEEE                    ("\xFF\xEE\xDD\xCC\xBB\xAA")
#define APP_DIS_IEEE_LEN                (6)

/**
 * PNP ID Value - LSB -> MSB
 *      Vendor ID Source : 0x02 (USB Implementers Forum assigned Vendor ID value)
 *      Vendor ID : 0x1057      (ON Semiconductor)
 *      Product ID : 0x0040
 *      Product Version : 0x0300
 */

#define APP_DIS_PNP_ID               ("\x02\x57\x10\x40\x00\x00\x03")
#define APP_DIS_PNP_ID_LEN           (7)
#define APP_DIS_FEATURES             (DIS_ALL_FEAT_SUP)

/* ---------------------------------------------------------------------------
* Function prototype definitions
* --------------------------------------------------------------------------*/
void Device_Initialize(void);

void APP_SetAdvScanData(void);

void APP_SetConnectionCfmParams(uint8_t conidx, struct gapc_connection_cfm* cfm);

void APP_7100_HB_Handler(ke_msg_id_t const msg_id, void const *param,
                         ke_task_id_t const dest_id, ke_task_id_t const src_id);

void APP_ASHA_CallbackHandler(enum ASHA_Operation_t op, void *param);

void APP_GAPM_GATTM_Handler(ke_msg_id_t const msg_id, void const *param,
                            ke_task_id_t const dest_id,
                            ke_task_id_t const src_id);

void APP_GAPC_Handler(ke_msg_id_t const msg_id, void const *param,
                      ke_task_id_t const dest_id, ke_task_id_t const src_id);

void Connection_GetPeerFeat(uint8_t conidx);

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* APP_H */
