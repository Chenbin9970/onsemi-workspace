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
 * This is Reusable Code.
 *
 * ----------------------------------------------------------------------------
 * app.c
 * - Main application file
 * ------------------------------------------------------------------------- */

#include <app.h>

extern const struct ReadOnlyProperties_t ashaReadOnlyProperties;

extern const struct DISS_DeviceInfo_t deviceInfo;

/* AudioControlPoint characteristic status is START/STOP */
extern bool is_asha_start_stop;

/* ----------------------------------------------------------------------------
 * Function      : void BASS_Setup(void)
 * ----------------------------------------------------------------------------
 * Description   : Configure the Battery Service Server
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
static void BASS_Setup(void)
{
    BASS_Initialize(APP_BAS_NB, APP_BASS_ReadBatteryLevel);
    BASS_NotifyOnBattLevelChange(TIMER_SETTING_S(1));     /* Periodically monitor the battery level. Only notify changes */
    APP_BASS_SetBatMonAlarm(BATMON_SUPPLY_THRESHOLD_CFG); /* BATMON alarm configuration */
}


/* ----------------------------------------------------------------------------
 * Function      : void DISS_Setup(void)
 * ----------------------------------------------------------------------------
 * Description   : Configure the Device Information Service Server
 * Inputs        : None
 * Outputs       : None
 * Assumptions   : None
 * ------------------------------------------------------------------------- */
/**
 * @brief Configure the Device Information Service Server
 */
static void DISS_Setup(void)
{
    DISS_Initialize(APP_DIS_FEATURES, (const struct DISS_DeviceInfo_t*) &deviceInfo);
}

int main(void)
{
    /* Configure hardware and initialize BLE stack */
    Device_Initialize();

    TRACE_INIT();

    PRINTF("\r\n==============================================\n");
#if ASHA_CAPABILITIES_SIDE==ASHA_CAPABILITIES_SIDE_LEFT
    PRINTF("\r\n===== Initializing ble_android_asha LEFT =====\n");
#else
    PRINTF("\r\n===== Initializing ble_android_asha RIGHT ====\n");
#endif /* ASHA_CAPABILITIES_SIDE */
    PRINTF("\r\n==============================================\r\n");

    /* Run the following command when erasing flash/bond_list is desirable */
    //BondList_RemoveAll();

    /* Configure application-specific advertising data and scan response  data*/
    APP_SetAdvScanData();

    /* Configure Battery Service Server */
    BASS_Setup();

    /* Configure Device Information Service Server */
    DISS_Setup();

    /* Initialize Android Audio Streaming Hearing Aid service */
    ASHA_Initialize(&ashaReadOnlyProperties, APP_ASHA_CallbackHandler);

    /* Add application message handlers */
    MsgHandler_Add(TASK_ID_GAPM, APP_GAPM_GATTM_Handler);
    MsgHandler_Add(GATTM_ADD_SVC_RSP, APP_GAPM_GATTM_Handler);
    MsgHandler_Add(TASK_ID_GAPC, APP_GAPC_Handler);
    MsgHandler_Add(APP_BATT_LEVEL_LOW, APP_BASS_BattLevelLow_Handler);
    MsgHandler_Add(APP_7100_HB_TIMER, APP_7100_HB_Handler);

    /* Reset the GAP manager. Trigger GAPM_CMP_EVT / GAPM_RESET when finished. See APP_GAPM_GATTM_Handler */
    GAPM_ResetCmd();

    while (1)
    {
        /* If AudioControlPoint characteristic
        * is set to START or STOP,
        * send a notification to that effect
        */
        if(is_asha_start_stop == true)
        {
            is_asha_start_stop = false;
            PRINTF("\r\n===== Notify Start/Stop received========= \r\n");
            ASHA_NotifyStatusPoint(ASHA_GetEnv()->conidx, ASHA_GetEnv()->audioStatusPoint);
        }

        Kernel_Schedule();    /* Dispatch all events in Kernel queue */
        Sys_Watchdog_Refresh();
        SYS_WAIT_FOR_EVENT;    /* Wait for an event before re-executing the scheduler */
    }
}

