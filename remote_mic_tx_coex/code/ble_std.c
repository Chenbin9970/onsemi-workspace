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
 * ble_std.c
 * - Bluetooth standard functions and message handlers
 * ----------------------------------------------------------------------------
 * $Revision: 1.10 $
 * $Date: 2019/12/27 18:50:38 $
 * ------------------------------------------------------------------------- */

#include "app.h"
#include <printf.h>

/* Bluetooth Environment Structure */
struct ble_env_tag ble_env;

/* List of functions used to create the database */
const appm_add_svc_func_t appm_add_svc_func_list[] = {
    SERVICE_ADD_FUNCTION_LIST, NULL
};

/* List of functions used to enable client services */
const appm_enable_svc_func_t appm_enable_svc_func_list[] = {
    SERVICE_ENABLE_FUNCTION_LIST, NULL
};

/* Bluetooth Device Address */
uint8_t bdaddr[BDADDR_LENGTH];
uint8_t bdaddr_type;

static struct gapm_set_dev_config_cmd *gapmConfigCmd;

/* ----------------------------------------------------------------------------
 * Standard Functions
 * ------------------------------------------------------------------------- */
/* ----------------------------------------------------------------------------
 * Function      : void BLE_Initialize(void)
 * ----------------------------------------------------------------------------
 * Description   : Initialize the BLE baseband and application manager
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void BLE_Initialize(void)
{
    struct gapm_reset_cmd *cmd;
    uint8_t *ptr = (uint8_t *)DEVICE_INFO_BLUETOOTH_ADDR;
    uint8_t tmp[2][BDADDR_LENGTH] = { { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
                                      { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
    uint8_t default_addr[BDADDR_LENGTH] = PRIVATE_BDADDR;

    /* Seed the random number generator */
    srand(1);

    /* Initialize the kernel and Bluetooth stack */
    Kernel_Init(0);
    BLE_InitNoTL(0);

    /* Enable the Bluetooth related interrupts needed */
    NVIC_EnableIRQ(BLE_EVENT_IRQn);
    NVIC_EnableIRQ(BLE_RX_IRQn);
    NVIC_EnableIRQ(BLE_CRYPT_IRQn);
    NVIC_EnableIRQ(BLE_ERROR_IRQn);
    NVIC_EnableIRQ(BLE_SW_IRQn);
    NVIC_EnableIRQ(BLE_GROSSTGTIM_IRQn);
    NVIC_EnableIRQ(BLE_FINETGTIM_IRQn);
    NVIC_EnableIRQ(BLE_CSCNT_IRQn);
    NVIC_EnableIRQ(BLE_SLP_IRQn);

    NVIC_SetPriority(BLE_EVENT_IRQn, 1);
    NVIC_SetPriority(BLE_RX_IRQn, 1);
    NVIC_SetPriority(BLE_CRYPT_IRQn, 1);
    NVIC_SetPriority(BLE_ERROR_IRQn, 1);
    NVIC_SetPriority(BLE_SW_IRQn, 1);
    NVIC_SetPriority(BLE_GROSSTGTIM_IRQn, 1);
    NVIC_SetPriority(BLE_FINETGTIM_IRQn, 1);
    NVIC_SetPriority(BLE_CSCNT_IRQn, 1);
    NVIC_SetPriority(BLE_SLP_IRQn, 1);

    /* Reset the Bluetooth environment */
    memset(&ble_env, 0, sizeof(ble_env));

    /* Initialize task state */
    ble_env.state = APPM_INIT;

    /* Use the device's public address if an address is available at
     * DEVICE_INFO_BLUETOOTH_ADDR (located in NVR3). If this address is
     * not defined (all ones) use a pre-defined private address for this
     * application */
    if (memcmp(ptr, &tmp[0][0], BDADDR_LENGTH) == 0 ||
        memcmp(ptr, &tmp[1][0], BDADDR_LENGTH) == 0)
    {
        memcpy(bdaddr, default_addr, sizeof(uint8_t) * BDADDR_LENGTH);
        bdaddr_type = GAPM_CFG_ADDR_PRIVATE;
    }
    else
    {
        memcpy(bdaddr, ptr, sizeof(uint8_t) * BDADDR_LENGTH);
        bdaddr_type = GAPM_CFG_ADDR_PUBLIC;
    }

    /* Initialize GAPM configuration command to initialize the stack */
    gapmConfigCmd =
        malloc(sizeof(struct gapm_set_dev_config_cmd));
    gapmConfigCmd->operation      = GAPM_SET_DEV_CONFIG;
    gapmConfigCmd->role = GAP_ROLE_CENTRAL;
    memcpy(gapmConfigCmd->addr.addr, bdaddr, BDADDR_LENGTH);
    gapmConfigCmd->addr_type      = bdaddr_type;
    gapmConfigCmd->renew_dur      = RENEW_DUR;
    memset(&gapmConfigCmd->irk.key[0], 0, KEY_LEN);
    gapmConfigCmd->pairing_mode   = GAPM_PAIRING_DISABLE;
    gapmConfigCmd->gap_start_hdl  = 0;
    gapmConfigCmd->gatt_start_hdl = 0;
    gapmConfigCmd->max_mtu = MTU_MAX;
    gapmConfigCmd->max_mps = MPS_MAX;
    gapmConfigCmd->att_and_ext_cfg = ATT_CFG;
    gapmConfigCmd->sugg_max_tx_octets = TX_OCT_MAX;
    gapmConfigCmd->sugg_max_tx_time   = TX_TIME_MAX;
    gapmConfigCmd->tx_pref_rates = GAP_RATE_ANY;
    gapmConfigCmd->rx_pref_rates = GAP_RATE_ANY;
    gapmConfigCmd->max_nb_lecb   = 0x0;
    gapmConfigCmd->audio_cfg     = 0;

    /* Reset the stack */
    cmd = KE_MSG_ALLOC(GAPM_RESET_CMD, TASK_GAPM, TASK_APP, gapm_reset_cmd);
    cmd->operation = GAPM_RESET;
    ke_msg_send(cmd);
}

/* ----------------------------------------------------------------------------
 * Function      : bool Service_Add(void)
 * ----------------------------------------------------------------------------
 * Description   : Add the next service in the service list,
 *                 calling the appropriate add service function
 * Inputs        : None
 * Outputs       : return value - Indicates if any service has not yet been
 *                                added
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
bool Service_Add(void)
{
    /* Check if another should be added in the database */
    if (appm_add_svc_func_list[ble_env.next_svc] != NULL)
    {
        /* Call the function used to add the required service */
        appm_add_svc_func_list[ble_env.next_svc] ();

        /* Select the next service to add */
        ble_env.next_svc++;
        return (true);
    }

    return (false);
}

/* ----------------------------------------------------------------------------
 * Standard Message Handlers
 * ------------------------------------------------------------------------- */
/* ----------------------------------------------------------------------------
 * Function      : int GAPM_ProfileAddedInd(ke_msg_id_t const msg_id,
 *                                          struct gapm_profile_added_ind
 *                                          const *param,
 *                                          ke_task_id_t const dest_id,
 *                                          ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle the received result of adding a profile to the
 *                 attribute database
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gapm_profile_added_ind
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int GAPM_ProfileAddedInd(ke_msg_id_t const msg_id,
                         struct gapm_profile_added_ind const *param,
                         ke_task_id_t const dest_id,
                         ke_task_id_t const src_id)
{
    /* If the application is creating its attribute database, continue to add
     * services; otherwise do nothing. */
    if (ble_env.state == APPM_CREATE_DB)
    {
        PRINTF("__GAPM_PROFILE_ADDED_IND\n");
        /* Add the next requested service */
        if (!Service_Add())
        {
            /* If there are no more services to add, go to the ready state */
            ble_env.state = APPM_READY;

            /* No more service to add, send start connection */
            Connection_SendStartCmd();
        }
    }

    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int GAPM_CmpEvt(ke_msg_id_t const msg_id,
 *                                 struct gapm_cmp_evt
 *                                 const *param,
 *                                 ke_task_id_t const dest_id,
 *                                 ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle the reception of a GAPM complete event
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gapm_cmp_evt
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int GAPM_CmpEvt(ke_msg_id_t const msg_id, struct gapm_cmp_evt
                const *param,
                ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    struct gapm_set_dev_config_cmd *cmd;

    switch (param->operation)
    {
        /* A reset has occurred, configure the device and
         * start a kernel timer for the application */
        case (GAPM_RESET):
        {
            if (param->status == GAP_ERR_NO_ERROR)
            {
                /* Set the device configuration */
                cmd = KE_MSG_ALLOC(GAPM_SET_DEV_CONFIG_CMD, TASK_GAPM, TASK_APP,
                                   gapm_set_dev_config_cmd);
                memcpy(cmd, gapmConfigCmd, sizeof(struct
                                                  gapm_set_dev_config_cmd));
                free(gapmConfigCmd);

                /* Send message */
                ke_msg_send(cmd);

                /* Start a timer to be used as a periodic tick timer for
                 * application */
                ke_timer_set(APP_TEST_TIMER, TASK_APP, TIMER_200MS_SETTING);
            }
        }
        break;

        /* Device configuration updated */
        case (GAPM_SET_DEV_CONFIG):
        {
            /* Start creating the GATT database */
            ble_env.state = APPM_CREATE_DB;

            /* Add the first required service in the database */
            if (!Service_Add())
            {
                /* Go to the ready state */
                ble_env.state = APPM_READY;

                /* Send start connection command since there are no services to
                 * add to the attribute database */
                Connection_SendStartCmd();
            }
        }
        break;

        default:
        {
            /* No action required for other operations */
        }
        break;
    }

    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int GAPC_ConnectionReqInd(ke_msg_idd_t const msg_id,
 *                                           struct gapc_connection_req_ind
 *                                           const *param,
 *                                           ke_task_id_t const dest_id,
 *                                           ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle connection indication message received from GAP
 *                 controller
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gapc_connection_req_ind
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int GAPC_ConnectionReqInd(ke_msg_id_t const msg_id,
                          struct gapc_connection_req_ind const *param,
                          ke_task_id_t const dest_id,
                          ke_task_id_t const src_id)
{
    struct gapc_connection_cfm *cfm;

    ble_env.conidx = KE_IDX_GET(src_id);

    PRINTF("__GAPC_CONNECTION_REQ_IND\n");
    /* Check if the received connection handle was valid */
    if (ble_env.conidx != GAP_INVALID_CONIDX)
    {
        PRINTF("__APPM_CONNECTED\n");
        ble_env.state  = APPM_CONNECTED;

        /* Retrieve the connection info from the parameters */
        ble_env.conidx = param->conhdl;

        /* Save the connection parameters */
        ble_env.con_interval = param->con_interval;
        ble_env.con_latency  = param->con_latency;
        ble_env.time_out     = param->sup_to;

        /* Send connection confirmation */
        cfm = KE_MSG_ALLOC(GAPC_CONNECTION_CFM,
                           KE_BUILD_ID(TASK_GAPC, ble_env.conidx), TASK_APP,
                           gapc_connection_cfm);

        cfm->pairing_lvl = GAP_AUTH_REQ_NO_MITM_NO_BOND;

        cfm->svc_changed_ind_enable = 0;

        /* Send the message */
        ke_msg_send(cfm);

        /* Start enabling client services */
        BLE_SetServiceState(true, ble_env.conidx);
    }
    else
    {
        Connection_SendStartCmd();
    }

    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int GAPC_CmpEvt(ke_msg_id_t const msg_id,
 *                                 struct gapc_cmp_evt
 *                                 const *param,
 *                                 ke_task_id_t const dest_id,
 *                                 ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle received GAPC complete event
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gapc_cmp_evt
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int GAPC_CmpEvt(ke_msg_id_t const msg_id, struct gapc_cmp_evt
                const *param,
                ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    /* No operations in this application use this event */
    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int GAPC_DisconnectInd(ke_msg_id_t const msg_id,
 *                                        struct gapc_disconnect_ind
 *                                        const *param,
 *                                        ke_task_id_t const dest_id,
 *                                        ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle disconnect indication message from GAP controller
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gapc_disconnect_ind
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int GAPC_DisconnectInd(ke_msg_id_t const msg_id,
                       struct gapc_disconnect_ind const *param,
                       ke_task_id_t const dest_id, ke_task_id_t const src_id)
{
    PRINTF("__GAPC_DISCONNECT_IND\n");
    /* Go to the ready state */
    ble_env.state = APPM_READY;

    BLE_SetServiceState(false, ble_env.conidx);

    /* When the link is lost, it sends connection start command again to put
     * it in the state that accepts advertisements from peer device
     * and can connect to it */
    Connection_SendStartCmd();

    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int GAPC_ParamUpdatedInd(ke_msg_id_t const msg_id,
 *                                          struct gapc_param_updated_ind
 *                                          const *param,
 *                                          ke_task_id_t const dest_id,
 *                                          ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   : Handle message parameter updated indication received from GAP
 *                 controller
 * Inputs        : - msg_id     - Kernel message ID number
 *                 - param      - Message parameters in format of
 *                                struct gapc_param_updated_ind
 *                 - dest_id    - Destination task ID number
 *                 - src_id     - Source task ID number
 * Outputs       : return value - Indicate if the message was consumed;
 *                                compare with KE_MSG_CONSUMED
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
int GAPC_ParamUpdatedInd(ke_msg_id_t const msg_id,
                         struct gapc_param_updated_ind const *param,
                         ke_task_id_t const dest_id,
                         ke_task_id_t const src_id)
{
    ble_env.updated_con_interval = param->con_interval;
    ble_env.updated_latency = param->con_latency;
    ble_env.updated_suo_to  = param->sup_to;

    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : int GAPC_ParamUpdateReqInd(ke_msg_id_t const msg_id,
 *                         struct gapc_param_update_req_ind const *param,
 *                         ke_task_id_t const dest_id,
 *                         ke_task_id_t const src_id)
 * ----------------------------------------------------------------------------
 * Description   :
 * ------------------------------------------------------------------------- */
int GAPC_ParamUpdateReqInd(ke_msg_id_t const msg_id,
                           struct gapc_param_update_req_ind
                           const *param,
                           ke_task_id_t const dest_id,
                           ke_task_id_t const src_id)
{
    struct gapc_param_update_cfm *cfm;

    cfm = KE_MSG_ALLOC(GAPC_PARAM_UPDATE_CFM,
                       KE_BUILD_ID(TASK_GAPC, ble_env.conidx),
                       TASK_APP,
                       gapc_param_update_cfm);
    cfm->accept     = 1;
    cfm->ce_len_min = ((param->intv_min * 2));
    cfm->ce_len_max = ((param->intv_max * 2));

    /* Send message */
    ke_msg_send(cfm);
    return (KE_MSG_CONSUMED);
}

/* ----------------------------------------------------------------------------
 * Function      : void Connection_SendStartCmd(void)
 * ----------------------------------------------------------------------------
 * Description   : Send a command to establish a connection with peer device
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : Application knows the peer Bluetooth device address,
 *                 so it uses direct connection method
 * ------------------------------------------------------------------------- */
void Connection_SendStartCmd(void)
{
    uint8_t peerAddress0[BD_ADDR_LEN] = DIRECT_PEER_BD_ADDRESS;
    struct gapm_start_connection_cmd *cmd;

    PRINTF("__START CONNECTION\n");
    /* Prepare the GAPM_START_CONNECTION_CMD message  */
    cmd = KE_MSG_ALLOC_DYN(GAPM_START_CONNECTION_CMD, TASK_GAPM, TASK_APP,
                           gapm_start_connection_cmd,
                           (sizeof(struct gap_bdaddr)));

    cmd->op.code       = GAPM_CONNECTION_DIRECT;
    cmd->op.addr_src   = GAPM_STATIC_ADDR;
    cmd->op.state      = 0;

    /* Set scan interval to 62.5ms and scan window to 50% of the interval */
    cmd->scan_interval = SCAN_INERVAL;
    cmd->scan_window   = SCAN_WINDOW;

    /* Set the connection interval to 7.5ms */
    cmd->con_intv_min  = CON_INTERVAL_MIN;
    cmd->con_intv_max  = CON_INTERVAL_MAX;
    cmd->con_latency   = CON_SLAVE_LATENCY;

    /* Set supervisory timeout to 3s */
    cmd->superv_to     = CON_SUP_TIMEOUT;

    cmd->nb_peers      = 1;

    /* Address Type: Private Address */
    cmd->peers[0].addr_type = DIRECT_PEER_BD_ADDRESS_TYPE;

    memcpy(&cmd->peers[0].addr.addr[0], &peerAddress0[0], BD_ADDR_LEN);

    /* Send the message */
    ke_msg_send(cmd);

    ble_env.state = APPM_CONNECTING;
}

/* ----------------------------------------------------------------------------
 * Function      : void BLE_SetServiceState(bool enable, uint8_t conidx)
 * ----------------------------------------------------------------------------
 * Description   : Set Bluetooth application environment state to enabled
 * Inputs        : - enable    - Indicates that enable request should be sent
 *                               for all services/profiles or their status
 *                               should be set to disabled
 *                 - conidx    - Connection index
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
void BLE_SetServiceState(bool enable, uint8_t conidx)
{
    /* All standard services should be send enable request to the stack,
     * for custom services, application should decide if it would want
     * to do a specific action
     * all services should be disabled once the link is lost
     */
    if (enable == true)
    {
        /* To enable standard Bluetooth services, an enable request should be
         * sent to the stack (for related profile) and at receiving the
         * response the enable flag can be set. For custom service it is
         * application implementation dependent. Here, it starts service
         * discovery, and if the service UUID and characteristics UUID are
         * discovered, then it goes to an state that is equivalent to the
         * enable flag of standard profiles
         */
        ble_env.next_svc_enable = 0;
        ServiceEnable(conidx);
    }
    else
    {
        basc_support_env.enable = false;
        cs_env.state = CS_INIT;
    }
}

/* ----------------------------------------------------------------------------
 * Function      : bool ServiceEnable(uint8_t conidx)
 * ----------------------------------------------------------------------------
 * Description   : Enable the next service in the service list,
 *                 calling the appropriate enable service function
 * Inputs        : - conidx     - Connection index
 * Outputs       : return value - Indicates if any service has not yet been
 *                                added
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
bool ServiceEnable(uint8_t conidx)
{
    /* Check if another should be added in the database */
    if (appm_enable_svc_func_list[ble_env.next_svc_enable] != NULL)
    {
        /* Call the function used to enable the required service */
        appm_enable_svc_func_list[ble_env.next_svc_enable] (conidx);

        /* Select the next service to enable */
        ble_env.next_svc_enable++;
        return (true);
    }

    return (false);
}
