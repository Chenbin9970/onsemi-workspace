#include "app.h"
#include "ble_rempro.h"

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

struct rempro_env_tag rempro_env;

void RemproService_Env_Initialize(void)
{
    memset(&rempro_env, 0, sizeof(rempro_env));
    rempro_env.onoff_cccd_value = ATT_CCC_START_NTF;
    rempro_env.role_cccd_value = 0;
    rempro_env.sentSuccess = 1;
}

void RemproService_ServiceAdd(void)
{
    struct gattm_add_svc_req *req = KE_MSG_ALLOC_DYN(GATTM_ADD_SVC_REQ,
                                                     TASK_GATTM, TASK_APP,
                                                     gattm_add_svc_req,
                                                     REMPRO_IDX_NB * sizeof(struct gattm_att_desc));

    uint8_t i;
    const uint8_t svc_uuid[ATT_UUID_128_LEN] = REMPRO_SVC_UUID;

    const struct gattm_att_desc att[REMPRO_IDX_NB] =
    {
        [REMPRO_IDX_ROLE_VALUE_CHAR]     = ATT_DECL_CHAR(),
        [REMPRO_IDX_ROLE_VALUE_VAL]      = ATT_DECL_CHAR_UUID_128(REMPRO_CHARACTERISTIC_ROLE_UUID,
                                                                   PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE)
                                                                   | PERM(WRITE_COMMAND, ENABLE),
                                                                   REMPRO_ROLE_VALUE_MAX_LENGTH),
        [REMPRO_IDX_ROLE_VALU_CCC]      = ATT_DECL_CHAR_CCC(),
        [REMPRO_IDX_ROLE_VALUE_USR_DSCP] = ATT_DECL_CHAR_USER_DESC(REMPRO_USER_DESCRIPTION_MAX_LENGTH),

        [REMPRO_IDX_ONOFF_VALUE_CHAR]    = ATT_DECL_CHAR(),
        [REMPRO_IDX_ONOFF_VALUE_VAL]     = ATT_DECL_CHAR_UUID_128(REMPRO_CHARACTERISTIC_ONOFF_UUID,
                                                                   PERM(RD, ENABLE) | PERM(NTF, ENABLE),
                                                                   REMPRO_ONOFF_VALUE_MAX_LENGTH),
        [REMPRO_IDX_ONOFF_VALU_CCC]     = ATT_DECL_CHAR_CCC(),
    };

    req->svc_desc.start_hdl = 0;
    req->svc_desc.task_id = TASK_APP;
    req->svc_desc.perm = PERM(SVC_UUID_LEN, UUID_128);
    req->svc_desc.nb_att = REMPRO_IDX_NB;

    memcpy(&req->svc_desc.uuid[0], &svc_uuid[0], ATT_UUID_128_LEN);

    for (i = 0; i < REMPRO_IDX_NB; i++)
    {
        memcpy(&req->svc_desc.atts[i], &att[i], sizeof(struct gattm_att_desc));
    }

    ke_msg_send(req);
}

void RemproService_SendNotification(uint8_t conidx, uint8_t attidx,
                                    uint8_t *value, uint8_t length)
{
    struct gattc_send_evt_cmd *cmd;
    uint16_t handle = (attidx + rempro_env.start_hdl + 1);

    cmd = KE_MSG_ALLOC_DYN(GATTC_SEND_EVT_CMD,
                           KE_BUILD_ID(TASK_GATTC, conidx),
                           TASK_APP,
                           gattc_send_evt_cmd,
                           length * sizeof(uint8_t));
    cmd->handle = handle;
    cmd->length = length;
    cmd->operation = GATTC_NOTIFY;
    cmd->seq_num = 0;
    memcpy(cmd->value, value, length);

    ke_msg_send(cmd);
}
