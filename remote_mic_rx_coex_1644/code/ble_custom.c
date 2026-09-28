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
 * ble_custom.c
 * - Bluetooth custom service functions
 * ----------------------------------------------------------------------------
 * $Revision: 1.9 $
 * $Date: 2018/02/27 15:42:17 $
 * ------------------------------------------------------------------------- */

#include "app.h"
#include "ble_rempro.h"
#include "ble_rempro_cmd.h"
#include <printf.h>
#ifdef BS300_ENABLE
#include "bs300_ram_sync.h"
#include "bs300_storage.h"
#endif    /* ifdef BS300_ENABLE */

/* Global variable definition */
struct cs_env_tag cs_env;

/* ----------------------------------------------------------------------------
 * Function      : void CustomService_Env_Initialize(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize custom service environment
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void CustomService_Env_Initialize(void)
{
    /* Reset the application manager environment */
    memset(&cs_env, 0, sizeof(cs_env));
}

/* ----------------------------------------------------------------------------
 * Function      : void CustomService_ServiceAdd(void)
 * ----------------------------------------------------------------------------
 * Description   : Send request to add custom profile into the attribute
 *                 database. Defines the different access functions
 *                 (setter/getter commands to access the different
 * .*                 characteristic attributes).
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void CustomService_ServiceAdd(void)
{
    struct gattm_add_svc_req *req = KE_MSG_ALLOC_DYN(GATTM_ADD_SVC_REQ,
                                                     TASK_GATTM, TASK_APP,
                                                     gattm_add_svc_req,
                                                     CS_IDX_NB * sizeof(struct
                                                                        gattm_att_desc));

    uint8_t i;
    const uint8_t svc_uuid[ATT_UUID_128_LEN]   = CS_SVC_UUID;

    const struct gattm_att_desc att[CS_IDX_NB] =
    {
        /* Attribute Index  = Attribute properties: UUID,
         *                                          Permissions,
         *                                          Max size,
         *                                          Extra permissions */

        [CS_REMPRO_IDX_ROLE_VALUE_CHAR]     = ATT_DECL_CHAR(),
        [CS_REMPRO_IDX_ROLE_VALUE_VAL]      = ATT_DECL_CHAR_UUID_128(CS_REMPRO_CHARACTERISTIC_ROLE_UUID,
                                                                     PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE),
                                                                     CS_REMPRO_ROLE_VALUE_MAX_LENGTH),
        [CS_REMPRO_IDX_ROLE_VALUE_USR_DSCP] = ATT_DECL_CHAR_USER_DESC(CS_REMPRO_USER_DESCRIPTION_MAX_LENGTH),

        [CS_REMPRO_IDX_ONOFF_VALUE_CHAR]     = ATT_DECL_CHAR(),
        [CS_REMPRO_IDX_ONOFF_VALUE_VAL]      = ATT_DECL_CHAR_UUID_128(CS_REMPRO_CHARACTERISTIC_ONOFF_UUID,
                                                                      PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE),
                                                                      CS_REMPRO_ONOFF_VALUE_MAX_LENGTH),
        [CS_REMPRO_IDX_ONOFF_VALUE_USR_DSCP] = ATT_DECL_CHAR_USER_DESC(CS_REMPRO_USER_DESCRIPTION_MAX_LENGTH),

        [CS_REMPRO_IDX_CHNLSIDE_VALUE_CHAR]     = ATT_DECL_CHAR(),
        [CS_REMPRO_IDX_CHNLSIDE_VALUE_VAL]      = ATT_DECL_CHAR_UUID_128(CS_REMPRO_CHARACTERISTIC_CHNLSIDE_UUID,
                                                                         PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE),
                                                                         CS_REMPRO_CHNLSIDE_VALUE_MAX_LENGTH),
        [CS_REMPRO_IDX_CHNLSIDE_VALUE_USR_DSCP] = ATT_DECL_CHAR_USER_DESC(CS_REMPRO_USER_DESCRIPTION_MAX_LENGTH),

        [CS_REMPRO_IDX_MODIDX_VALUE_CHAR]     = ATT_DECL_CHAR(),
        [CS_REMPRO_IDX_MODIDX_VALUE_VAL]      = ATT_DECL_CHAR_UUID_128(CS_REMPRO_CHARACTERISTIC_MODIDX_UUID,
                                                                       PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE),
                                                                       CS_REMPRO_MODIDX_VALUE_MAX_LENGTH),
        [CS_REMPRO_IDX_MODIDX_VALUE_USR_DSCP] = ATT_DECL_CHAR_USER_DESC(CS_REMPRO_USER_DESCRIPTION_MAX_LENGTH),

        [CS_REMPRO_IDX_VOLUME_VALUE_CHAR]     = ATT_DECL_CHAR(),
        [CS_REMPRO_IDX_VOLUME_VALUE_VAL]      = ATT_DECL_CHAR_UUID_128(CS_REMPRO_CHARACTERISTIC_VOLUME_UUID,
                                                                       PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE),
                                                                       CS_REMPRO_VOLUME_VALUE_MAX_LENGTH),
        [CS_REMPRO_IDX_VOLUME_VALUE_USR_DSCP] = ATT_DECL_CHAR_USER_DESC(CS_REMPRO_USER_DESCRIPTION_MAX_LENGTH),

        [CS_REMPRO_IDX_HOPLIST_VALUE_CHAR]     = ATT_DECL_CHAR(),
        [CS_REMPRO_IDX_HOPLIST_VALUE_VAL]      = ATT_DECL_CHAR_UUID_128(CS_REMPRO_CHARACTERISTIC_HOPLIST_UUID,
                                                                        PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE),
                                                                        CS_REMPRO_HOPLIST_VALUE_MAX_LENGTH),
        [CS_REMPRO_IDX_HOPLIST_VALUE_USR_DSCP] = ATT_DECL_CHAR_USER_DESC(CS_REMPRO_USER_DESCRIPTION_MAX_LENGTH),

        [CS_REMPRO_IDX_HOPSIZE_VALUE_CHAR]     = ATT_DECL_CHAR(),
        [CS_REMPRO_IDX_HOPSIZE_VALUE_VAL]      = ATT_DECL_CHAR_UUID_128(CS_REMPRO_CHARACTERISTIC_HOPSIZE_UUID,
                                                                        PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE),
                                                                        CS_REMPRO_HOPSIZE_VALUE_MAX_LENGTH),
        [CS_REMPRO_IDX_HOPSIZE_VALUE_USR_DSCP] = ATT_DECL_CHAR_USER_DESC(CS_REMPRO_USER_DESCRIPTION_MAX_LENGTH),
    };

    /* Fill the add custom service message */
    req->svc_desc.start_hdl = 0;
    req->svc_desc.task_id   = TASK_APP;
    req->svc_desc.perm = PERM(SVC_UUID_LEN, UUID_128);
    req->svc_desc.nb_att    = CS_IDX_NB;

    memcpy(&req->svc_desc.uuid[0], &svc_uuid[0], ATT_UUID_128_LEN);

    for (i = 0; i < CS_IDX_NB; i++)
    {
        memcpy(&req->svc_desc.atts[i], &att[i],
               sizeof(struct gattm_att_desc));
    }

    /* Send the message */
    ke_msg_send(req);
}

/* ----------------------------------------------------------------------------
 * Function      : int GATTM_AddSvcRsp(ke_msg_id_t const msg_id,
 *                                     struct gattm_add_svc_rsp
 *                                     const *param,
 *                                     ke_task_id_t const dest_id,
 *                                     ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle the response from adding a service in the attribute
 *                 database from the GATT manager
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gattm_add_svc_rsp
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int GATTM_AddSvcRsp(ke_msg_id_t const msg_id,
                    struct gattm_add_svc_rsp const *param,
                    ke_task_id_t const dest_id,
                    ke_task_id_t const src_id)
{
    /* 只保留 Rempro Service：每次 TASK_APP 服务添加的 start_hdl 都属 Rempro */
    rempro_env.start_hdl = param->start_hdl;

    /* Add the next requested service  */
    if (!Service_Add())
    {
        /* All services have been added, go to the ready state
         * and start advertising */
        ble_env.state = APPM_READY;
        Advertising_Start();
    }

    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int GATTC_ReadReqInd(ke_msg_id_t const msg_id,
 *                                      struct gattc_read_req_ind
 *                                      const *param,
 *                                      ke_task_id_t const dest_id,
 *                                      ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle received read request indication
 *                 from a GATT Controller
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gattc_read_req_ind
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int GATTC_ReadReqInd(ke_msg_id_t const msg_id,
                     struct gattc_read_req_ind const *param,
                     ke_task_id_t const dest_id,
                     ke_task_id_t const src_id)
{
    uint8_t length = 0;
    uint8_t status = GAP_ERR_NO_ERROR;
    uint16_t attnum;
    uint8_t *valptr = NULL;

    struct gattc_read_cfm *cfm;
    bool is_rempro = (rempro_env.start_hdl != 0 &&
                      param->handle > rempro_env.start_hdl);

    /* Route handle to the correct service（RemoteMic CS 或 Rempro） */
    if (is_rempro)
    {
        attnum = (param->handle - rempro_env.start_hdl - 1);
        switch (attnum)
        {
            case REMPRO_IDX_ROLE_VALUE_VAL:
            {
                length = REMPRO_ROLE_VALUE_MAX_LENGTH;
                valptr = (uint8_t *)&rempro_env.role_value;
            }
            break;

            case REMPRO_IDX_ROLE_VALU_CCC:
            {
                length = 2;
                valptr = (uint8_t *)&rempro_env.role_cccd_value;
            }
            break;

            case REMPRO_IDX_ROLE_VALUE_USR_DSCP:
            {
                length = strlen("ROLE_VALUE");
                valptr = (uint8_t *)"ROLE_VALUE";
            }
            break;

            case REMPRO_IDX_ONOFF_VALUE_VAL:
            {
                length = REMPRO_ONOFF_VALUE_MAX_LENGTH;
                valptr = (uint8_t *)&rempro_env.onoff_value;
            }
            break;

            case REMPRO_IDX_ONOFF_VALU_CCC:
            {
                length = 2;
                valptr = (uint8_t *)&rempro_env.onoff_cccd_value;
            }
            break;

            default:
            {
                status = ATT_ERR_READ_NOT_PERMITTED;
            }
            break;
        }
    }
    else if (param->handle > cs_env.start_hdl)
    {
        attnum = (param->handle - cs_env.start_hdl - 1);
    }
    else
    {
        status = ATT_ERR_INVALID_HANDLE;
    }

    /* If there is no error, send back the requested attribute value */
    if ((status == GAP_ERR_NO_ERROR) && !is_rempro)
    {
        switch (attnum)
        {
            case CS_REMPRO_IDX_ROLE_VALUE_VAL:
            {
                length = CS_REMPRO_ROLE_VALUE_MAX_LENGTH;
                valptr = (uint8_t *)&app_env.rm_param.role;
            }
            break;

            case CS_REMPRO_IDX_ROLE_VALUE_USR_DSCP:
            {
                length = strlen(CS_REMPRO_ROLE_CHARACTERISTIC_NAME);
                valptr = (uint8_t *)CS_REMPRO_ROLE_CHARACTERISTIC_NAME;
            }
            break;

            case CS_REMPRO_IDX_ONOFF_VALUE_VAL:
            {
                length = CS_REMPRO_ONOFF_VALUE_MAX_LENGTH;
                valptr = (uint8_t *)&app_env.RM_on_off;
            }
            break;

            case CS_REMPRO_IDX_ONOFF_VALUE_USR_DSCP:
            {
                length = strlen(CS_REMPRO_ONOFF_CHARACTERISTIC_NAME);
                valptr = (uint8_t *)CS_REMPRO_ONOFF_CHARACTERISTIC_NAME;
            }
            break;

            case CS_REMPRO_IDX_CHNLSIDE_VALUE_VAL:
            {
                length = CS_REMPRO_CHNLSIDE_VALUE_MAX_LENGTH;
                valptr = (uint8_t *)&app_env.rm_param.audioChnl;
            }
            break;

            case CS_REMPRO_IDX_CHNLSIDE_VALUE_USR_DSCP:
            {
                length = strlen(CS_REMPRO_CHNLSIDE_CHARACTERISTIC_NAME);
                valptr = (uint8_t *)CS_REMPRO_CHNLSIDE_CHARACTERISTIC_NAME;
            }
            break;

            case CS_REMPRO_IDX_MODIDX_VALUE_VAL:
            {
                length = CS_REMPRO_MODIDX_VALUE_MAX_LENGTH;
                valptr = (uint8_t *)&app_env.rm_param.mod_idx;
            }
            break;

            case CS_REMPRO_IDX_MODIDX_VALUE_USR_DSCP:
            {
                length = strlen(CS_REMPRO_MODIDX_CHARACTERISTIC_NAME);
                valptr = (uint8_t *)CS_REMPRO_MODIDX_CHARACTERISTIC_NAME;
            }
            break;

            case CS_REMPRO_IDX_VOLUME_VALUE_VAL:
            {
                length = CS_REMPRO_VOLUME_VALUE_MAX_LENGTH;
                valptr = (uint8_t *)&app_env.volume;
            }
            break;

            case CS_REMPRO_IDX_VOLUME_VALUE_USR_DSCP:
            {
                length = strlen(CS_REMPRO_VOLUME_CHARACTERISTIC_NAME);
                valptr = (uint8_t *)CS_REMPRO_VOLUME_CHARACTERISTIC_NAME;
            }
            break;

            case CS_REMPRO_IDX_HOPLIST_VALUE_VAL:
            {
                length = CS_REMPRO_HOPLIST_VALUE_MAX_LENGTH;
                valptr = (uint8_t *)&app_env.rm_param.hopList;
            }
            break;

            case CS_REMPRO_IDX_HOPLIST_VALUE_USR_DSCP:
            {
                length = strlen(CS_REMPRO_HOPLIST_CHARACTERISTIC_NAME);
                valptr = (uint8_t *)CS_REMPRO_HOPLIST_CHARACTERISTIC_NAME;
            }
            break;

            case CS_REMPRO_IDX_HOPSIZE_VALUE_VAL:
            {
                length = CS_REMPRO_HOPSIZE_VALUE_MAX_LENGTH;
                valptr = (uint8_t *)&app_env.rm_param.numChnlInHopList;
            }
            break;

            case CS_REMPRO_IDX_HOPSIZE_VALUE_USR_DSCP:
            {
                length = strlen(CS_REMPRO_HOPSIZE_CHARACTERISTIC_NAME);
                valptr = (uint8_t *)CS_REMPRO_HOPSIZE_CHARACTERISTIC_NAME;
            }
            break;

            default:
            {
                status = ATT_ERR_READ_NOT_PERMITTED;
            }
            break;
        }
    }

    /* Allocate and build message */
    cfm = KE_MSG_ALLOC_DYN(GATTC_READ_CFM,
                           KE_BUILD_ID(TASK_GATTC, ble_env.conidx), TASK_APP,
                           gattc_read_cfm,
                           length);

    if (valptr != NULL)
    {
        memcpy(cfm->value, valptr, length);
    }

    cfm->handle = param->handle;
    cfm->length = length;
    cfm->status = status;

    /* Send the message */
    ke_msg_send(cfm);

    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int GATTC_WriteReqInd(ke_msg_id_t const msg_id,
 *                                       struct gattc_write_req_ind
 *                                       const *param,
 *                                       ke_task_id_t const dest_id,
 *                                       ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle received write request indication
 *                 from a GATT Controller
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gattc_write_req_ind
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int GATTC_WriteReqInd(ke_msg_id_t const msg_id,
                      struct gattc_write_req_ind const *param,
                      ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    struct gattc_write_cfm *cfm = KE_MSG_ALLOC(GATTC_WRITE_CFM,
                                               KE_BUILD_ID(TASK_GATTC,
                                                           ble_env.conidx),
                                               TASK_APP, gattc_write_cfm);

    uint8_t status = GAP_ERR_NO_ERROR;
    uint16_t attnum = 0;
    uint8_t *valptr = NULL;
    uint8_t oldValue_onoff = 0;
    struct rm_callback callback;
    bool is_rempro = (rempro_env.start_hdl != 0 &&
                      param->handle > rempro_env.start_hdl);

    /* Check that offset is not zero */
    if (param->offset)
    {
        status = ATT_ERR_INVALID_OFFSET;
    }

    /* Route handle to the correct service（RemoteMic CS 或 Rempro） */
    if (is_rempro)
    {
        attnum = (param->handle - rempro_env.start_hdl - 1);
        if (status == GAP_ERR_NO_ERROR)
        {
            switch (attnum)
            {
                case REMPRO_IDX_ROLE_VALUE_VAL:
#ifdef CFG_FOTA
                    /* 0xFD = FOTA 触发（不是 HDLC 帧头 0x7E，安全保留） */
                    if (param->length >= 1 && param->value[0] == 0xFD)
                    {
                        PRINTF("[FOTA] trigger via Rempro 0xFD\r\n");
                        Sys_Fota_StartDfu(1);
                        break;   /* 不送入 rempro 重装缓冲 */
                    }
#endif    /* ifdef CFG_FOTA */
#ifdef BS300_ENABLE
                    /* 0xFE = 重启并重新读取 BS300 参数（参照 sleep CS 0xFE：清缓存→重启） */
                    if (param->length >= 1 && param->value[0] == 0xFE)
                    {
                        uint8_t bi;
                        PRINTF("[BS300] 0xFE: clear cache + reset to reload\r\n");
                        for (bi = 0; bi < 4; bi++)
                        {
                            bs300_storage_invalidate(bi);
                        }
                        bs300_settings_invalidate();
                        bs300_reset_to_defaults();
                        /* 重启后 bs300_driver_init() 从芯片重新读取参数 */
                        NVIC_SystemReset();
                        break;   /* 不送入 rempro 重装缓冲 */
                    }
#endif    /* ifdef BS300_ENABLE */
                    /* 手机写入的命令数据直接进 rempro 重装缓冲 */
                    rempro_reasm_append(param->value, param->length);
                    break;
                case REMPRO_IDX_ROLE_VALU_CCC:
                    valptr = (uint8_t *)&rempro_env.role_cccd_value;
                    break;
                case REMPRO_IDX_ONOFF_VALU_CCC:
                    valptr = (uint8_t *)&rempro_env.onoff_cccd_value;
                    break;
                default:
                    status = ATT_ERR_WRITE_NOT_PERMITTED;
                    break;
            }
        }
    }
    else if (param->handle > cs_env.start_hdl)
    {
        attnum = (param->handle - cs_env.start_hdl - 1);
    }
    else
    {
        status = ATT_ERR_INVALID_HANDLE;
    }

    /* If there is no error, save the requested attribute value */
    if ((status == GAP_ERR_NO_ERROR) && !is_rempro)
    {
        switch (attnum)
        {
            case CS_REMPRO_IDX_ROLE_VALUE_VAL:
            {
                valptr = (uint8_t *)&app_env.rm_param.role;
            }
            break;

            case CS_REMPRO_IDX_ONOFF_VALUE_VAL:
            {
                oldValue_onoff = app_env.RM_on_off;
                valptr = (uint8_t *)&app_env.RM_on_off;
            }
            break;

            case CS_REMPRO_IDX_CHNLSIDE_VALUE_VAL:
            {
                valptr = (uint8_t *)&app_env.rm_param.audioChnl;
            }
            break;

            case CS_REMPRO_IDX_MODIDX_VALUE_VAL:
            {
                valptr = (uint8_t *)&app_env.rm_param.mod_idx;
            }
            break;

            case CS_REMPRO_IDX_VOLUME_VALUE_VAL:
            {
                valptr = (uint8_t *)&app_env.volume;
            }
            break;

            case CS_REMPRO_IDX_HOPLIST_VALUE_VAL:
            {
                valptr = (uint8_t *)&app_env.rm_param.hopList;
            }
            break;

            case CS_REMPRO_IDX_HOPSIZE_VALUE_VAL:
            {
                valptr = (uint8_t *)&app_env.rm_param.numChnlInHopList;
            }
            break;

            default:
            {
                status = ATT_ERR_READ_NOT_PERMITTED;
            }
            break;
        }
    }

    if (valptr != NULL)
    {
        memcpy(valptr, param->value, param->length);
    }

    cfm->handle = param->handle;
    cfm->status = status;

    /* Send the message */
    ke_msg_send(cfm);

    if (!is_rempro && (oldValue_onoff != app_env.RM_on_off) &&
        (attnum == CS_REMPRO_IDX_ONOFF_VALUE_VAL))
    {
        if (app_env.RM_on_off)
        {
            callback.trx_event     = RM_Callback_TRX;
            callback.status_update = RM_Callback_StatusUpdate;

            RM_Configure(&app_env.rm_param, callback);
            RF_SwitchToCPMode();
            RM_Enable(1000);
        }
        else
        {
            BBIF_COEX_CTRL->RX_ALIAS = 0;
            BBIF_COEX_CTRL->TX_ALIAS = 0;
            RM_Disable();
            RF_SwitchToBLEMode();
        }
    }

    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int GATTC_CmpEvt(ke_msg_id_t const msg_id,
 *                                 struct gattc_cmp_evt const *param,
 *                                 ke_task_id_t const dest_id,
 *                                 ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle GATT operation completion (Rempro chunked notify
 *                 pacing uses rempro_env.sentSuccess)
 * ------------------------------------------------------------------------- */
int GATTC_CmpEvt(ke_msg_id_t const msg_id,
                 struct gattc_cmp_evt const *param,
                 ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    if (param->operation == GATTC_NOTIFY)
    {
        if (param->status == GAP_ERR_NO_ERROR ||
            param->status == GAP_ERR_DISCONNECTED)
        {
            rempro_env.sentSuccess = 1;
        }
    }
    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : void CustomService_SendNotification(uint8_t conidx,
 *                               uint8_t attidx, uint8_t *value, uint8_t length)
 * ----------------------------------------------------------------------------
 * Description   : Send a notification to the client device
 * Inputs        : - conidx       - connection index
 *                 - attidx       - index to attributes in the service
 *                 - value        - pointer to value
 *                 - length       - length of value
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void CustomService_SendNotification(uint8_t conidx, uint8_t attidx,
                                    uint8_t *value, uint8_t length)
{
    struct gattc_send_evt_cmd *cmd;
    uint16_t handle = (attidx + cs_env.start_hdl + 1);

    /* Prepare a notification message for the specified attribute */
    cmd = KE_MSG_ALLOC_DYN(GATTC_SEND_EVT_CMD,
                           KE_BUILD_ID(TASK_GATTC, conidx), TASK_APP,
                           gattc_send_evt_cmd,
                           length * sizeof(uint8_t));
    cmd->handle    = handle;
    cmd->length    = length;
    cmd->operation = GATTC_NOTIFY;
    cmd->seq_num   = 0;
    memcpy(cmd->value, value, length);

    /* Send the message */
    ke_msg_send(cmd);
}
