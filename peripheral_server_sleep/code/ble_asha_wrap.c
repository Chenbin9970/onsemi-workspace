/* ----------------------------------------------------------------------------
 * ble_asha_wrap.c
 * - ASHA-specific GATT dispatch helpers.
 *   BLE Abstraction source files (ble_gap.c, ble_gatt.c, ble_l2c.c,
 *   msg_handler.c) are now provided by the SDK via RTE.
 *   This file contains only the ASHA-specific additions.
 * ------------------------------------------------------------------------- */

#include <rsl10.h>
#include <rsl10_ke.h>
#include <rsl10_ble.h>
#include <ble_gap.h>
#include <ble_gatt.h>
#include <ble_l2c.h>
#include <ble_asha.h>
#include <malloc.h>
#include <string.h>
#include <stdbool.h>
#include <msg_handler.h>
#include "ble_asha_wrap.h"

/* Global linked-list head for MsgHandler dispatcher */
MsgHandler_t *msgHandlerHead = NULL;

/* ----------------------------------------------------------------------------
 * Local GATT state for ASHA service dispatch.
 * (SDK's ble_gatt.c has its own static gatt_env — we keep a parallel copy
 *  of start_hdl and att_db_len for ASHA handle routing.)
 * ------------------------------------------------------------------------- */
static const struct att_db_desc *asha_attdb;
static uint16_t asha_attdb_len;
static uint16_t asha_start_hdl;
static uint8_t  asha_svc_added;

/* ----------------------------------------------------------------------------
 * ASHA_ServiceAdd — plug into SERVICE_ADD_FUNCTION_LIST
 * ------------------------------------------------------------------------- */
extern const struct att_db_desc asha_att_db[];

/* =========================================================================
 * Thin wrappers — replace ble_gap.c / ble_gatt.c / ble_l2c.c / msg_handler.c
 * ========================================================================= */

/* --- GAPM_LepsmRegisterCmd --- */
void GAPM_LepsmRegisterCmd(uint16_t le_psm, uint16_t app_task, uint8_t sec_lvl)
{
    struct gapm_lepsm_register_cmd *cmd = KE_MSG_ALLOC(GAPM_LEPSM_REGISTER_CMD,
        TASK_GAPM, TASK_APP, gapm_lepsm_register_cmd);
    cmd->app_task  = TASK_APP;
    cmd->le_psm    = le_psm;
    cmd->operation = GAPM_LEPSM_REG;
    cmd->sec_lvl   = sec_lvl;
    ke_msg_send(cmd);
}

/* --- L2CC_LecbConnectCfm --- */
void L2CC_LecbConnectCfm(uint8_t conidx, const struct l2cc_lecb_connect_cfm *param)
{
    struct l2cc_lecb_connect_cfm *cfm = KE_MSG_ALLOC(L2CC_LECB_CONNECT_CFM,
        KE_BUILD_ID(TASK_L2CC, conidx), TASK_APP, l2cc_lecb_connect_cfm);
    memcpy(cfm, param, sizeof(struct l2cc_lecb_connect_cfm));
    ke_msg_send(cfm);
}

/* --- L2CC_LecbAddCmd --- */
void L2CC_LecbAddCmd(uint8_t conidx, uint16_t local_cid, uint16_t credit)
{
    struct l2cc_lecb_add_cmd *cmd = KE_MSG_ALLOC(L2CC_LECB_ADD_CMD,
        KE_BUILD_ID(TASK_L2CC, conidx), TASK_APP, l2cc_lecb_add_cmd);
    cmd->operation  = L2CC_LECB_CREDIT_ADD;
    cmd->local_cid  = local_cid;
    cmd->credit     = credit;
    ke_msg_send(cmd);
}

/* --- GATTC_SendEvtCmd --- */
void GATTC_SendEvtCmd(uint8_t conidx, uint8_t operation, uint16_t seq_num,
                      uint16_t handle, uint16_t length, uint8_t *value)
{
    struct gattc_send_evt_cmd *cmd = KE_MSG_ALLOC_DYN(GATTC_SEND_EVT_CMD,
        KE_BUILD_ID(TASK_GATTC, conidx), TASK_APP, gattc_send_evt_cmd,
        length * sizeof(uint8_t));
    cmd->handle    = handle;
    cmd->length    = length;
    cmd->operation = operation;
    cmd->seq_num   = seq_num;
    memcpy(cmd->value, value, length);
    ke_msg_send(cmd);
}

/* --- GATTM_AddAttributeDatabase --- */
void GATTM_AddAttributeDatabase(const struct att_db_desc *att_db, uint16_t att_db_len)
{
    uint8_t cs_nb_att = 0;
    uint8_t att_idx   = 1;

    while (att_idx < att_db_len)
    {
        uint8_t nb_att;
        for (nb_att = 0; att_idx < att_db_len && !att_db[att_idx].is_service; nb_att++, att_idx++);

        struct gattm_add_svc_req *req = KE_MSG_ALLOC_DYN(GATTM_ADD_SVC_REQ,
            TASK_GATTM, TASK_APP, gattm_add_svc_req,
            nb_att * sizeof(struct gattm_att_desc));
        req->svc_desc.start_hdl = 0;
        req->svc_desc.task_id   = TASK_APP;
        req->svc_desc.perm      = att_db[cs_nb_att].att.perm;
        req->svc_desc.nb_att    = nb_att;
        memcpy(&req->svc_desc.uuid[0], &att_db[cs_nb_att++].att.uuid, ATT_UUID_128_LEN);
        for (uint8_t i = 0; i < nb_att; i++)
            memcpy(&req->svc_desc.atts[i], &att_db[cs_nb_att++].att, sizeof(struct gattm_att_desc));
        ke_msg_send(req);
        att_idx++;
    }
}

/* --- GATTM_GetHandle --- */
uint16_t GATTM_GetHandle(uint16_t attidx)
{
    if (asha_start_hdl && attidx < asha_attdb_len)
        return asha_start_hdl + attidx;
    return 0;
}

/* --- MsgHandler_Add --- */
bool MsgHandler_Add(ke_msg_id_t const msg_id,
    void (*callback)(ke_msg_id_t const, void const *,
                     ke_task_id_t const, ke_task_id_t const))
{
    MsgHandler_t *node = (MsgHandler_t *)malloc(sizeof(MsgHandler_t));
    if (node == NULL) return false;
    node->msg_id   = msg_id;
    node->callback = callback;
    node->next     = msgHandlerHead;
    msgHandlerHead = node;
    return true;
}

/* --- MsgHandler_Notify (linked-list dispatcher for ASHA events) --- */
int MsgHandler_Notify(ke_msg_id_t const msg_id, void *param,
                      ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    MsgHandler_t *tmp = msgHandlerHead;
    uint8_t task_id = KE_IDX_GET(msg_id);

    while (tmp)
    {
        if ((tmp->msg_id == msg_id) || (tmp->msg_id == task_id))
            tmp->callback(msg_id, param, dest_id, src_id);
        tmp = tmp->next;
    }
    return 0;
}

/* =========================================================================
 * ASHA-specific GATT dispatch helpers
 * ========================================================================= */

void ASHA_ServiceAdd(void)
{
    GATTM_AddAttributeDatabase(asha_att_db, ASHA_MAX_IDX);
    asha_attdb     = asha_att_db;
    asha_attdb_len = ASHA_MAX_IDX;
}

/* ----------------------------------------------------------------------------
 * ASHA_GATTM_AddSvcRspHandler — capture start_hdl from GATTM_ADD_SVC_RSP
 * ------------------------------------------------------------------------- */
bool ASHA_GATTM_AddSvcRspHandler(const struct gattm_add_svc_rsp *param)
{
    if (asha_attdb == NULL)
        return false;

    if (param->status == GAP_ERR_NO_ERROR && asha_start_hdl == 0)
        asha_start_hdl = param->start_hdl;

    asha_svc_added++;
    return true;
}

/* ----------------------------------------------------------------------------
 * ASHA_GATTM_GetAddedCount
 * ------------------------------------------------------------------------- */
uint8_t ASHA_GATTM_GetAddedCount(void)
{
    return asha_svc_added;
}

/* ----------------------------------------------------------------------------
 * ASHA_GATTC_ReadReqHandler — dispatch ASHA GATT read requests
 * ------------------------------------------------------------------------- */
bool ASHA_GATTC_ReadReqHandler(uint8_t conidx, const struct gattc_read_req_ind *param)
{
    if (asha_start_hdl == 0 || asha_attdb == NULL)
        return false;

    uint16_t attnum = param->handle - asha_start_hdl;
    if (attnum >= asha_attdb_len)
        return false;

    const struct att_db_desc *att = &asha_attdb[attnum];
    if (!(att->att.perm & (PERM(RD, ENABLE) | PERM(NTF, ENABLE))))
        return false;

    if (att->callback != NULL)
    {
        att->callback(conidx, attnum, param->handle,
                      NULL, att->data, att->length ? att->length : ATT_UUID_128_LEN,
                      GATTC_READ_REQ_IND);
    }
    return true;
}

/* ----------------------------------------------------------------------------
 * ASHA_GATTC_WriteReqHandler — dispatch ASHA GATT write requests
 * ------------------------------------------------------------------------- */
bool ASHA_GATTC_WriteReqHandler(uint8_t conidx, const struct gattc_write_req_ind *param)
{
    if (asha_start_hdl == 0 || asha_attdb == NULL)
        return false;

    uint16_t attnum = param->handle - asha_start_hdl;
    if (attnum >= asha_attdb_len)
        return false;

    const struct att_db_desc *att = &asha_attdb[attnum];
    if (!(att->att.perm & (PERM(WRITE_REQ, ENABLE) | PERM(WRITE_COMMAND, ENABLE))))
        return false;

    if (att->callback != NULL)
    {
        att->callback(conidx, attnum, param->handle,
                      att->data, param->value,
                      MIN(param->length, att->length),
                      GATTC_WRITE_REQ_IND);
    }
    else
    {
        memcpy(att->data, param->value, MIN(param->length, att->length));
    }
    return true;
}
