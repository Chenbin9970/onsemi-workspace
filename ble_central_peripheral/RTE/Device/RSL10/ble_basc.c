/* ----------------------------------------------------------------------------
 * Copyright (c) 2018 Semiconductor Components Industries, LLC (d/b/a
 * ON Semiconductor), All Rights Reserved
 *
 * Copyright (C) RivieraWaves 2009-2018
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
 *  This is Reusable Code.
 *
 * ----------------------------------------------------------------------------
 * ble_basc.c
 * - Bluetooth battery service client functions
 * ----------------------------------------------------------------------------
 * $Revision: 1.1 $
 * $Date: 2019/06/03 15:11:15 $
 * ------------------------------------------------------------------------- */
#include <rsl10.h>
#include <ble_gap.h>
#include <msg_handler.h>
#include <ble_basc.h>

/* Global variable definition */
static BASC_Env_t basc_env;

/* ----------------------------------------------------------------------------
 * Function      : void BASC_Initialize()
 * ----------------------------------------------------------------------------
 * Description   : Initialize battery service client service environment and
 *                 configure message handlers
 * Inputs        : readBattLevelCallback  - Pointer to user application
 *                     callback function that returns the battery value
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void BASC_Initialize(void)
{
    /* Initialize environment variable */
    memset(&basc_env, 0, sizeof(BASC_Env_t));
    for (unsigned int i = 0; i < BLE_CONNECTION_MAX; i++)
    {
        basc_env.req_ntf_cfg[i][0] = ATT_CCC_START_NTF;
        basc_env.req_ntf_cfg[i][1] = ATT_CCC_START_NTF;
    }

    basc_env.profile_added_ind.prf_task_id = TASK_ID_INVALID;
    basc_env.battLevelReqTimeout = 0;

    /* Add BASC message handlers. Medium priority means handlers are called after GAP, GATT handlers */
    MsgHandler_Add(TASK_ID_BASC,            BASC_MsgHandler);
    MsgHandler_Add(GAPM_CMP_EVT,            BASC_MsgHandler);
    MsgHandler_Add(GAPC_CONNECTION_REQ_IND, BASC_MsgHandler);
    MsgHandler_Add(GAPC_DISCONNECT_IND,     BASC_MsgHandler);
    MsgHandler_Add(GAPM_PROFILE_ADDED_IND,  BASC_MsgHandler);
}

/* ----------------------------------------------------------------------------
 * Function      : void BASC_ServiceEnable_Client(uint8_t conidx)
 * ----------------------------------------------------------------------------
 * Description   : Send request for enabling battery service to battery client
 * Inputs        : conidx - Connection index
 * Outputs       : None
 * Assumptions   : conidx is valid (between 0 and GAP_MAX_CONNECTIONS-1)
 * ------------------------------------------------------------------------- */
void BASC_EnableReq(uint8_t conidx)
{
    struct basc_enable_req *req;

    /* Allocate the GATT notification message  */
    req = KE_MSG_ALLOC(BASC_ENABLE_REQ,
                       prf_src_task_get(&(PRF_ENV_GET(BASC, basc)->prf_env),
                                        conidx), TASK_APP, basc_enable_req);

    /* Set con_type to profile discovery */
    req->con_type = PRF_CON_DISCOVERY;

    /* Should be filled according to the last discovery */
    memcpy(&req->bas, &basc_env.bas[conidx],
           sizeof(struct bas_content));
    req->bas_nb = basc_env.bas_nb[conidx];

    /* Send the message  */
    ke_msg_send(req);
}

/* ----------------------------------------------------------------------------
 * Function      : void Batt_SendReadInfoReq(uint8_t conidx,
 *                                           uint8_t bas_nb,
 *                                           uint8_t info)
 * ----------------------------------------------------------------------------
 * Description   : Send a request to read a battery service info from peer
 * Inputs        : - conidx     - Connection index,
 *                 - bas_nb     - Battery instance number
 *                 - info       - Info type
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void BASC_ReadInfoReq(uint8_t conidx, uint8_t bas_nb, uint8_t info)
{
    struct basc_read_info_req *req;

    req = KE_MSG_ALLOC(BASC_READ_INFO_REQ,
                       prf_src_task_get(&(PRF_ENV_GET(BASC, basc)->prf_env),
                       conidx), TASK_APP, basc_read_info_req);

    /* Fill in the parameter structure  */
    req->bas_nb = bas_nb;

    /* BASC_BATT_LVL_VAL, BASC_NTF_CFG, BASC_BATT_LVL_PRES_FORMAT */
    req->info   = info;

    /* Send the message */
    ke_msg_send(req);
}

/* ----------------------------------------------------------------------------
 * Function      : void BASC_SendLevelNtfCfgReq(uint8_t conidx,
 *                                              uint8_t bas_nb,
 *                                              uint8_t ntf_cfg)
 * ----------------------------------------------------------------------------
 * Description   : Send a request to change battery level notification
 *                 configuration in peer device
 * Inputs        : - conidx     - Connection index,
 *                 - bas_nb     - Battery instance number
 *                 - ntf_cfg    - The value of notification config
 *
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void BASC_BattLevelNtfCfgReq(uint8_t conidx, uint8_t bas_nb, uint8_t ntf_cfg)
{
    struct basc_batt_level_ntf_cfg_req *req;

    req = KE_MSG_ALLOC(BASC_BATT_LEVEL_NTF_CFG_REQ,
                       prf_src_task_get(&(PRF_ENV_GET(BASC, basc)->prf_env), conidx),
                       TASK_APP, basc_batt_level_ntf_cfg_req);

    /* Save the value to be confirmed when notification response is
     * received */
    basc_env.req_ntf_cfg[conidx][bas_nb] = ntf_cfg;

    /* Fill in the parameter structure */
    req->bas_nb = bas_nb;
    req->ntf_cfg = ntf_cfg;

    /* Send the message  */
    ke_msg_send(req);
}

/* ----------------------------------------------------------------------------
 * Function      : void BASC_RequestBattLevelOnTimeout(uint32_t timeout)
 * ----------------------------------------------------------------------------
 * Description   : Configure a kernel timer to periodically send battery level
 *                 requests.
 * Inputs        : - timeout     - Delay between requests in units of 10ms. If
 *                                 timeout == 0, disable periodic requests.
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void BASC_RequestBattLevelOnTimeout(uint32_t timeout)
{
    basc_env.battLevelReqTimeout = timeout;
    for(uint8_t i = 0; i < BLE_CONNECTION_MAX; i++)
    {
        if(basc_env.enabled[i] && basc_env.battLevelReqTimeout)
        {
            ke_timer_set(BASC_BATT_LEVEL_REQ_TIMEOUT, KE_BUILD_ID(TASK_APP, i),
                         basc_env.battLevelReqTimeout);
        }
    }
}

/* ----------------------------------------------------------------------------
 * Function      : void BASC_GetLastBatteryLevel(uint8_t conidx,
 *                                               uint8_t bas_nb)
 * ----------------------------------------------------------------------------
 * Description   : Return the last battery level value received from
 *                 peripheral. This function doesn't send an update request
 *                 over the air. Use BASC_ReadInfoReq for that instead.
 * Inputs        : conidx    - Connection index [0, BLE_CONNECTION_MAX-1]
 *                 bas_nb    - Battery instance [0, 1]
 * Outputs       : A constant pointer to BASC_Env_t
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
uint8_t BASC_GetLastBatteryLevel(uint8_t conidx, uint8_t bas_nb)
{
    return basc_env.batt_lvl[conidx][bas_nb];
}


/* ----------------------------------------------------------------------------
 * Function      : void BASS_GetEnv(void)
 * ----------------------------------------------------------------------------
 * Description   : Return a reference to the internal environment structure.
 * Inputs        : None
 * Outputs       : A constant pointer to BASC_Env_t
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
const BASC_Env_t* BASC_GetEnv(void)
{
    return &basc_env;
}

/* ----------------------------------------------------------------------------
 * Function      : void BASC_MsgHandler(ke_msg_id_t const msg_id,
 *                                      void const *param,
 *                                      ke_task_id_t const dest_id,
 *                                      ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle all events related to the battery service client
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameter
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void BASC_MsgHandler(ke_msg_id_t const msg_id, void const *param,
                     ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    uint8_t conidx = KE_IDX_GET(src_id);
    switch(msg_id)
    {
        case GAPM_CMP_EVT:
        {
            if(((struct gapm_cmp_evt*)param)->operation == GAPM_SET_DEV_CONFIG)
            {
                /* Add BASC profile task to the stack. A GAPM_PROFILE_ADDED_IND event
                 * is generated when finished. */
                GAPM_ProfileTaskAddCmd(PERM(SVC_AUTH, DISABLE), TASK_ID_BASC,
                                       TASK_APP, 0, NULL, 0);
            }
        }
        break;

        case GAPM_PROFILE_ADDED_IND:
        {
            const struct gapm_profile_added_ind *p = param;
            if(p->prf_task_id == TASK_ID_BASC)
            {
                basc_env.profile_added_ind.prf_task_id = p->prf_task_id;
                basc_env.profile_added_ind.prf_task_nb = p->prf_task_nb;
            }
        }
        break;

        case GAPC_CONNECTION_REQ_IND:
        {
            /* If profile has been added */
            if(basc_env.profile_added_ind.prf_task_id != TASK_ID_INVALID)
            {
                BASC_EnableReq(conidx);
            }
        }
        break;

        case GAPC_DISCONNECT_IND:
        {
            basc_env.enabled[conidx] = false;

        }
        break;

        case BASC_ENABLE_RSP:
        {
            struct basc_enable_rsp const *bas_param = param;

            if (bas_param->status == GAP_ERR_NO_ERROR)
            {
                memcpy(&basc_env.bas[conidx], bas_param->bas,
                       sizeof(struct bas_content));

                basc_env.enabled[conidx] = true;

                for(uint8_t i = 0; i < basc_env.bas_nb[conidx]; i++)
                {
                    BASC_BattLevelNtfCfgReq(conidx, i, basc_env.req_ntf_cfg[conidx][i]);
                }

                if(basc_env.battLevelReqTimeout) /* Start periodic battery level request */
                {
                    ke_timer_set(BASC_BATT_LEVEL_REQ_TIMEOUT, KE_BUILD_ID(TASK_APP, conidx),
                                 basc_env.battLevelReqTimeout);
                }
            }
            break;
        }

        case BASC_READ_INFO_RSP:
        {
            struct basc_read_info_rsp const *bas_param = param;

            if (bas_param->status == GAP_ERR_NO_ERROR)
            {
                if (bas_param->info == BASC_BATT_LVL_VAL)
                {
                    basc_env.batt_lvl[conidx][bas_param->bas_nb] =
                            bas_param->data.batt_level;
                }
                else if (bas_param->info == BASC_NTF_CFG)
                {
                    basc_env.ntf_cfg[conidx][bas_param->bas_nb] =
                            bas_param->data.ntf_cfg;
                }
                else if (bas_param->info == BASC_BATT_LVL_PRES_FORMAT)
                {
                    basc_env.char_pres_format[conidx][bas_param->bas_nb] =
                            bas_param->data.char_pres_format;
                }
            }
            break;
        }

        case BASC_BATT_LEVEL_IND:
        {
            struct basc_batt_level_ind const *bas_param = param;
            basc_env.batt_lvl[conidx][bas_param->bas_nb] = bas_param->batt_level;
            break;
        }

        case BASC_BATT_LEVEL_NTF_CFG_RSP:
        {
            struct basc_batt_level_ntf_cfg_rsp const *bas_param = param;
            if (bas_param->status == GAP_ERR_NO_ERROR)
            {
                basc_env.ntf_cfg[conidx][bas_param->bas_nb] =
                    basc_env.req_ntf_cfg[conidx][bas_param->bas_nb];
            }
            break;
        }

        case BASC_BATT_LEVEL_REQ_TIMEOUT:
        {
            if(basc_env.enabled[conidx] && basc_env.battLevelReqTimeout)
            {
                for(uint8_t i = 0; i < basc_env.bas_nb[conidx]; i++)
                {
                    BASC_ReadInfoReq(conidx, i, BASC_BATT_LVL_VAL);
                }
                /* Restart timer */
                ke_timer_set(BASC_BATT_LEVEL_REQ_TIMEOUT, KE_BUILD_ID(TASK_APP, conidx),
                             basc_env.battLevelReqTimeout);
            }
            break;
        }

        default:
        {
            /* Do not handle other BASC events */
            break;
        }
    }
}
