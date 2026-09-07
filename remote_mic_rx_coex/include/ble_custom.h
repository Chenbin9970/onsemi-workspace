/* ----------------------------------------------------------------------------
 * Copyright (c) 2015-2017 Semiconductor Components Industries, LLC (d/b/a
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
 * ble_custom.h
 * - Bluetooth custom service header
 * ----------------------------------------------------------------------------
 * $Revision: 1.6 $
 * $Date: 2018/02/27 15:46:06 $
 * ------------------------------------------------------------------------- */

#ifndef BLE_CUSTOM_H
#define BLE_CUSTOM_H

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

/* ----------------------------------------------------------------------------
 * Defines
 * --------------------------------------------------------------------------*/

/* Custom service UUIDs */
#define CS_SVC_UUID                              { 0xaa, 0xbb, 0x0e, 0x6e, 0x01, \
                                                   0x40, 0xca, 0x9e, 0xe5, 0xa9, \
                                                   0xa3, 0x00, 0xb5, 0xf3, 0x93, \
                                                   0xe0 }
#define CS_REMPRO_CHARACTERISTIC_ROLE_UUID       { 0xaa, 0xbb, 0x0e, 0x6e, 0x02, \
                                                   0x40, 0xca, 0x9e, 0xe5, 0xa9, \
                                                   0xa3, 0x00, 0xb5, 0xf3, 0x93, \
                                                   0xe0 }
#define CS_REMPRO_CHARACTERISTIC_ONOFF_UUID      { 0xaa, 0xbb, 0x0e, 0x6e, 0x03, \
                                                   0x40, 0xca, 0x9e, 0xe5, 0xa9, \
                                                   0xa3, 0x00, 0xb5, 0xf3, 0x93, \
                                                   0xe0 }
#define CS_REMPRO_CHARACTERISTIC_CHNLSIDE_UUID   { 0xaa, 0xbb, 0x0e, 0x6e, 0x04, \
                                                   0x40, 0xca, 0x9e, 0xe5, 0xa9, \
                                                   0xa3, 0x00, 0xb5, 0xf3, 0x93, \
                                                   0xe0 }
#define CS_REMPRO_CHARACTERISTIC_MODIDX_UUID     { 0xaa, 0xbb, 0x0e, 0x6e, 0x05, \
                                                   0x40, 0xca, 0x9e, 0xe5, 0xa9, \
                                                   0xa3, 0x00, 0xb5, 0xf3, 0x93, \
                                                   0xe0 }
#define CS_REMPRO_CHARACTERISTIC_VOLUME_UUID     { 0xaa, 0xbb, 0x0e, 0x6e, 0x06, \
                                                   0x40, 0xca, 0x9e, 0xe5, 0xa9, \
                                                   0xa3, 0x00, 0xb5, 0xf3, 0x93, \
                                                   0xe0 }
#define CS_REMPRO_CHARACTERISTIC_HOPLIST_UUID    { 0xaa, 0xbb, 0x0e, 0x6e, 0x07, \
                                                   0x40, 0xca, 0x9e, 0xe5, 0xa9, \
                                                   0xa3, 0x00, 0xb5, 0xf3, 0x93, \
                                                   0xe0 }
#define CS_REMPRO_CHARACTERISTIC_HOPSIZE_UUID    { 0xaa, 0xbb, 0x0e, 0x6e, 0x08, \
                                                   0x40, 0xca, 0x9e, 0xe5, 0xa9, \
                                                   0xa3, 0x00, 0xb5, 0xf3, 0x93, \
                                                   0xe0 }

#define ATT_DECL_CHAR() \
    { ATT_DECL_CHARACTERISTIC_128, PERM(RD, ENABLE), 0, 0 }
#define ATT_DECL_CHAR_UUID_16(uuid, perm, max_length) \
    { uuid, perm, max_length, PERM(RI, ENABLE) | PERM(UUID_LEN, UUID_16) }
#define ATT_DECL_CHAR_UUID_32(uuid, perm, max_length) \
    { uuid, perm, max_length, PERM(RI, ENABLE) | PERM(UUID_LEN, UUID_32) }
#define ATT_DECL_CHAR_UUID_128(uuid, perm, max_length) \
    { uuid, perm, max_length, PERM(RI, ENABLE) | PERM(UUID_LEN, UUID_128) }
#define ATT_DECL_CHAR_CCC()                                                     \
    { ATT_DESC_CLIENT_CHAR_CFG_128, PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE), \
      0, PERM(RI, ENABLE) }
#define ATT_DECL_CHAR_USER_DESC(max_length)                      \
    { ATT_DESC_CHAR_USER_DESC_128, PERM(RD, ENABLE), max_length, \
      PERM(RI, ENABLE) }

enum cs_idx_att
{
    CS_REMPRO_IDX_ROLE_VALUE_CHAR,
    CS_REMPRO_IDX_ROLE_VALUE_VAL,
    CS_REMPRO_IDX_ROLE_VALUE_USR_DSCP,

    CS_REMPRO_IDX_ONOFF_VALUE_CHAR,
    CS_REMPRO_IDX_ONOFF_VALUE_VAL,
    CS_REMPRO_IDX_ONOFF_VALUE_USR_DSCP,

    CS_REMPRO_IDX_CHNLSIDE_VALUE_CHAR,
    CS_REMPRO_IDX_CHNLSIDE_VALUE_VAL,
    CS_REMPRO_IDX_CHNLSIDE_VALUE_USR_DSCP,

    CS_REMPRO_IDX_MODIDX_VALUE_CHAR,
    CS_REMPRO_IDX_MODIDX_VALUE_VAL,
    CS_REMPRO_IDX_MODIDX_VALUE_USR_DSCP,

    CS_REMPRO_IDX_VOLUME_VALUE_CHAR,
    CS_REMPRO_IDX_VOLUME_VALUE_VAL,
    CS_REMPRO_IDX_VOLUME_VALUE_USR_DSCP,

    CS_REMPRO_IDX_HOPLIST_VALUE_CHAR,
    CS_REMPRO_IDX_HOPLIST_VALUE_VAL,
    CS_REMPRO_IDX_HOPLIST_VALUE_USR_DSCP,

    CS_REMPRO_IDX_HOPSIZE_VALUE_CHAR,
    CS_REMPRO_IDX_HOPSIZE_VALUE_VAL,
    CS_REMPRO_IDX_HOPSIZE_VALUE_USR_DSCP,

    /* Max number of characteristics */
    CS_IDX_NB,
};

#define CS_REMPRO_USER_DESCRIPTION_MAX_LENGTH   20

#define CS_REMPRO_ROLE_VALUE_MAX_LENGTH     1
#define CS_REMPRO_ONOFF_VALUE_MAX_LENGTH    1
#define CS_REMPRO_CHNLSIDE_VALUE_MAX_LENGTH 1
#define CS_REMPRO_MODIDX_VALUE_MAX_LENGTH   1
#define CS_REMPRO_VOLUME_VALUE_MAX_LENGTH   1
#define CS_REMPRO_HOPLIST_VALUE_MAX_LENGTH  16
#define CS_REMPRO_HOPSIZE_VALUE_MAX_LENGTH  1

#define CS_REMPRO_ROLE_CHARACTERISTIC_NAME      "ROLE"
#define CS_REMPRO_ONOFF_CHARACTERISTIC_NAME     "ON_OFF"
#define CS_REMPRO_CHNLSIDE_CHARACTERISTIC_NAME  "CHANNEL SIDE"
#define CS_REMPRO_MODIDX_CHARACTERISTIC_NAME    "MOD_INDEX"
#define CS_REMPRO_VOLUME_CHARACTERISTIC_NAME    "VOLUME"
#define CS_REMPRO_HOPLIST_CHARACTERISTIC_NAME   "HOPPING_LIST"
#define CS_REMPRO_HOPSIZE_CHARACTERISTIC_NAME   "HOPPING_LIST_SIZE"

/* List of message handlers that are used by the custom service application manager */
#define CS_MESSAGE_HANDLER_LIST                                     \
    DEFINE_MESSAGE_HANDLER(GATTC_READ_REQ_IND, GATTC_ReadReqInd),   \
    DEFINE_MESSAGE_HANDLER(GATTC_WRITE_REQ_IND, GATTC_WriteReqInd), \
    DEFINE_MESSAGE_HANDLER(GATTM_ADD_SVC_RSP, GATTM_AddSvcRsp)      \


/* Define the available custom service states */
enum cs_state
{
    CS_INIT,
    CS_SERVICE_DISCOVERD,
    CS_ALL_ATTS_DISCOVERED,
    CS_STATE_MAX
};

/* ----------------------------------------------------------------------------
 * Global variables and types
 * --------------------------------------------------------------------------*/

struct cs_env_tag
{
    /* The value of service handle in the database of attributes in the stack */
    uint16_t start_hdl;

    /* The state machine for service discovery, it is not used for server role */
    uint8_t state;
};

extern struct cs_env_tag cs_env;

/* ----------------------------------------------------------------------------
 * Function prototype definitions
 * --------------------------------------------------------------------------*/
extern void CustomService_Env_Initialize(void);

extern void CustomService_ServiceAdd(void);

extern int GATTM_AddSvcRsp(ke_msg_id_t const msgid,
                           struct gattm_add_svc_rsp const *param,
                           ke_task_id_t const dest_id,
                           ke_task_id_t const src_id);

extern int GATTC_ReadReqInd(ke_msg_id_t const msg_id,
                            struct gattc_read_req_ind const *param,
                            ke_task_id_t const dest_id,
                            ke_task_id_t const src_id);

extern int GATTC_WriteReqInd(ke_msg_id_t const msg_id,
                             struct gattc_write_req_ind const *param,
                             ke_task_id_t const dest_id,
                             ke_task_id_t const src_id);

extern void CustomService_SendNotification(uint8_t conidx, uint8_t attidx,
                                           uint8_t *value, uint8_t length);

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* BLE_CUSTOM_H */
