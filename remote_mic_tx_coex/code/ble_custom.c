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
 * $Revision: 1.8 $
 * $Date: 2018/03/16 14:22:51 $
 * ------------------------------------------------------------------------- */

#include "app.h"

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
 * Function      : void CustomService_ServiceEnable(uint8_t conidx)
 * ----------------------------------------------------------------------------
 * Description   : Send a command to use service discovery to look for a
 *                 specific service with a known UUID
 * Inputs        : - conidx       - connection index
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void CustomService_ServiceEnable(uint8_t conidx)
{
    struct gattc_disc_cmd *cmd;

    uint8_t svc_uuid[ATT_UUID_128_LEN] = CS_SVC_UUID;

    cmd = KE_MSG_ALLOC_DYN(GATTC_DISC_CMD, KE_BUILD_ID(TASK_GATTC, conidx),
                           TASK_APP, gattc_disc_cmd,
                           16 * sizeof(uint8_t));
    cmd->operation = GATTC_DISC_BY_UUID_SVC;
    cmd->uuid_len  = ATT_UUID_128_LEN;
    cmd->seq_num   = 0x0000;
    cmd->start_hdl = 0x0001;
    cmd->end_hdl   = 0xffff;
    memcpy(cmd->uuid, svc_uuid, ATT_UUID_128_LEN);

    /* Send the message */
    ke_msg_send(cmd);
}

/* ----------------------------------------------------------------------------
 * Function      : int GATTC_DiscCharInd(ke_msg_id_t const msg_id,
 *                                       struct gattc_disc_char_ind
 *                                       const *param,
 *                                       ke_task_id_t const dest_id,
 *                                       ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle discovered characteristic indication message received
 *                 from GATT controller
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gattc_disc_char_ind
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int GATTC_DiscCharInd(ke_msg_id_t const msg_id,
                      struct gattc_disc_char_ind const *param,
                      ke_task_id_t const dest_id,
                      ke_task_id_t const src_id)
{
    uint8_t uuid[CS_IDX_NB][16] = CS_CHARACTERISTICS_LIST;
    uint8_t i;

    /* Attr_hdl is for characteristic handle and pointer_hdl for value  */
    if (param->attr_hdl != 0 && cs_env.disc_attnum < CS_IDX_NB)
    {
        for (i = 0; i < CS_IDX_NB; i++)
        {
            if (param->uuid_len == ATT_UUID_128_LEN &&
                !memcmp(param->uuid, &uuid[i][0], ATT_UUID_128_LEN))
            {
                memcpy(&cs_env.disc_att[cs_env.disc_attnum], param,
                       sizeof(struct discovered_char_att));

                cs_env.disc_attnum++;
                break;
            }
        }

        if (cs_env.disc_attnum == CS_IDX_NB)
        {
            cs_env.state = CS_ALL_ATTS_DISCOVERED;

            /* Enable pending client services to be enable */
            ServiceEnable(ble_env.conidx);
        }
    }
    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int GATTC_CmpEvt(ke_msg_id_t const msg_id,
 *                                  struct gattc_cmp_evt
 *                                  const *param,
 *                                  ke_task_id_t const dest_id,
 *                                  ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle received GATT controller complete event
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gattc_cmp_evt
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int GATTC_CmpEvt(ke_msg_id_t const msg_id, struct gattc_cmp_evt
                 const *param,
                 ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    /* Check application state and status of service and characteristic
     * discovery for custom service and if it is unsuccessful we can disconnect
     * the link although it is possible to go to enable state and let the
     * battery service works */
    if (param->status != GAP_ERR_NO_ERROR)
    {
        if (param->operation == GATTC_DISC_BY_UUID_SVC &&
            param->status == ATT_ERR_ATTRIBUTE_NOT_FOUND &&
            cs_env.state != CS_SERVICE_DISCOVERD
            && ble_env.state != APPM_CONNECTED)
        {
            /* Enable pending client services to be enable */
            ServiceEnable(ble_env.conidx);
        }
        else if (param->operation == GATTC_DISC_ALL_CHAR &&
                 param->status == ATT_ERR_ATTRIBUTE_NOT_FOUND &&
                 cs_env.state == CS_SERVICE_DISCOVERD)
        {
            /* Enable pending client services to be enable */
            ServiceEnable(ble_env.conidx);
        }
    }
    else
    {
        if (param->operation == GATTC_WRITE)
        {
            if (cs_env.state == CS_CONFIGURING)
            {
                /* Mark RM switch pending — timer will handle the switch */
                cs_env.state = CS_PEER_CONFIGURED;
            }
        }
    }

    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int GATTC_DiscSvcInd(ke_msg_id_t const msg_id,
 *                                      struct gattc_disc_svc_ind
 *                                      const *param,
 *                                      ke_task_id_t const dest_id,
 *                                      ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Receive the result of a service discovery
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gattc_disc_svc_ind
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int GATTC_DiscSvcInd(ke_msg_id_t const msg_id,
                     struct gattc_disc_svc_ind const *param,
                     ke_task_id_t const
                     dest_id,
                     ke_task_id_t const src_id)
{
    struct gattc_disc_cmd *cmd;

    /* We accepts only discovered attributes with 128-bit UUID according to the defined
     * characteristics in this custom service */
    if (param->uuid_len == ATT_UUID_128_LEN)
    {
        cs_env.state       = CS_SERVICE_DISCOVERD;

        cs_env.start_hdl   = param->start_hdl;
        cs_env.end_hdl     = param->end_hdl;

        cs_env.disc_attnum = 0;

        /* Allocate and send GATTC discovery command to discover
         * characteristic declarations */
        cmd = KE_MSG_ALLOC_DYN(GATTC_DISC_CMD,
                               KE_BUILD_ID(TASK_GATTC, ble_env.conidx),
                               TASK_APP, gattc_disc_cmd,
                               2 * sizeof(uint8_t));

        cmd->operation = GATTC_DISC_ALL_CHAR;
        cmd->uuid_len  = 2;
        cmd->seq_num   = 0x0000;
        cmd->start_hdl = cs_env.start_hdl;
        cmd->end_hdl   = cs_env.end_hdl;
        cmd->uuid[0]   = 0;
        cmd->uuid[1]   = 0;

        /* Send the message */
        ke_msg_send(cmd);
    }

    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int GATTC_ReadInd(ke_msg_id_t const msg_id,
 *                                   struct gattc_read_ind
 *                                   const *param, ke_task_id_t const dest_id,
 *                                   ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Receive transmitted value from peripheral, assign to
 *                 tx_value
 *
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gattc_read_ind
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int GATTC_ReadInd(ke_msg_id_t const msg_id, struct
                  gattc_read_ind *param,
                  ke_task_id_t const dest_id, ke_task_id_t const
                  src_id)
{
    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int GATTC_EvtInd(ke_msg_id_t const msg_id,
 *                                  struct gattc_read_ind
 *                                  const *param, ke_task_id_t const dest_id,
 *                                  ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Receive transmitted value from peripheral, assign to
 *                 tx_value - contains new value of peer attribute handle
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gattc_read_ind
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------ */
int GATTC_EvtInd(ke_msg_id_t const msg_id, struct
                 gattc_event_ind *param,
                 ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : void CustomSrvice_SendWrite(uint8_t conidx, uint8_t *value,
 *                                                uint16_t handle, uint8_t offset,
 *                                                uint16_t length, uint8_t type)
 * ----------------------------------------------------------------------------
 * Description   : Send a write command or request to the client device
 * Inputs        : - conidx       - Connection index
 *                 - value        - Pointer to value
 *                 - hanlde       - Attribute handle
 *                 - length       - Length of value
 *                 - type         - Type of write message
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void CustomSrvice_SendWrite(uint8_t conidx, uint8_t *value, uint16_t handle,
                            uint8_t offset, uint16_t length, uint8_t type)
{
    struct gattc_write_cmd *cmd = KE_MSG_ALLOC_DYN(GATTC_WRITE_CMD,
                                                   KE_BUILD_ID(TASK_GATTC,
                                                               conidx),
                                                   TASK_APP, gattc_write_cmd,
                                                   length * sizeof(uint8_t));

    if (type == GATTC_WRITE)
    {
        /* Write request that needs a response from peer device */
        cmd->operation    = GATTC_WRITE;
        cmd->auto_execute = 1;
    }
    else if (type == GATTC_WRITE_NO_RESPONSE)
    {
        /* Write command that doesn't need a response from peer device */
        cmd->operation    = GATTC_WRITE_NO_RESPONSE;
        cmd->auto_execute = 0;
    }

    cmd->handle  = handle;
    cmd->seq_num = 0x0000;
    cmd->offset  = offset;
    cmd->cursor  = 0;
    cmd->length  = length;
    memcpy(cmd->value, (uint8_t *)value, length);

    /* Send the message  */
    ke_msg_send(cmd);
}

