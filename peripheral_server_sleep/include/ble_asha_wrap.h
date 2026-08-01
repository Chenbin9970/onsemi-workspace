/* ----------------------------------------------------------------------------
 * ble_asha_wrap.h — ASHA-specific GATT dispatch prototypes
 * ------------------------------------------------------------------------- */

#ifndef BLE_ASHA_WRAP_H
#define BLE_ASHA_WRAP_H

#include <stdbool.h>
#include <rsl10_ble.h>

/* ASHA service registration */
void ASHA_ServiceAdd(void);

/* GATTM_ADD_SVC_RSP handler for ASHA service */
bool ASHA_GATTM_AddSvcRspHandler(const struct gattm_add_svc_rsp *param);
uint8_t ASHA_GATTM_GetAddedCount(void);

/* GATT read/write dispatch for ASHA handles */
bool ASHA_GATTC_ReadReqHandler(uint8_t conidx, const struct gattc_read_req_ind *param);
bool ASHA_GATTC_WriteReqHandler(uint8_t conidx, const struct gattc_write_req_ind *param);

#endif /* BLE_ASHA_WRAP_H */
