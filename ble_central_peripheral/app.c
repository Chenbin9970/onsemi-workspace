/* ----------------------------------------------------------------------------
 * Copyright (c) 2018-2019 Semiconductor Components Industries, LLC (d/b/a
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
#include <printf.h>

int main(void)
{
    /* Configure hardware and initialize BLE stack */
    Device_Initialize();

    /* Debug/trace initialization. In order to enable UART or RTT trace,
     * configure the 'OUTPUT_INTERFACE' macro in printf.h */
    printf_init();
    PRINTF("__ble_central_peripheral has started!\r\n");

    /* Run the following command when erasing flash/bond_list is desirable */
    /* BondList_RemoveAll(); */

    /* Configure application-specific advertising data and scan response  data*/
    APP_SetAdvScanData();

    /* Configure Battery Service Server */
    BASS_Initialize(APP_BAS_NB, APP_BASS_ReadBatteryLevel);
    BASS_NotifyOnBattLevelChange(TIMER_SETTING_S(1));     /* Periodically monitor the battery level. Only notify changes */
    BASS_NotifyOnTimeout(TIMER_SETTING_S(6));             /* Periodically notify the battery level to connected peers */
    APP_BASS_SetBatMonAlarm(BATMON_SUPPLY_THRESHOLD_CFG); /* BATMON alarm configuration */

    /* Configure Battery Service Client */
    BASC_Initialize();
    BASC_RequestBattLevelOnTimeout(TIMER_SETTING_S(5));	  /* Periodically request the battery level of connected peers */

    /* Configure Custom Service Server */
    CUSTOMSS_Initialize();
    CUSTOMSS_NotifyOnTimeout(TIMER_SETTING_S(6)); /* Notify client and fire CUSTOMSS_NTF_TIMEOUT periodically */

    /* Add application message handlers */
    MsgHandler_Add(TASK_ID_GAPM, APP_GAPM_GATTM_Handler);
    MsgHandler_Add(GATTM_ADD_SVC_RSP, APP_GAPM_GATTM_Handler);
    MsgHandler_Add(TASK_ID_GAPC, APP_GAPC_Handler);
    MsgHandler_Add(APP_LED_TIMEOUT, APP_LED_Timeout_Handler);
    MsgHandler_Add(APP_BATT_LEVEL_LOW, APP_BASS_BattLevelLow_Handler);
    MsgHandler_Add(APP_SWITCH_ROLE_TIMEOUT, APP_SwitchRole_Timeout);
    MsgHandler_Add(BASC_BATT_LEVEL_IND, APP_BASC_BattLevelInd_Handler);

    /* Reset the GAP manager. Trigger GAPM_CMP_EVT / GAPM_RESET when finished. See APP_GAPM_GATTM_Handler */
    GAPM_ResetCmd();

    while (1)
    {
        Kernel_Schedule();    /* Dispatch all events in Kernel queue */
        Sys_Watchdog_Refresh();
        SYS_WAIT_FOR_EVENT;    /* Wait for an event before re-executing the scheduler */
    }
}

