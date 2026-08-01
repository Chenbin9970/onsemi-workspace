/* ----------------------------------------------------------------------------
 * asha_app.h
 * ------------------------------------------------------------------------- */

#ifndef ASHA_APP_H
#define ASHA_APP_H

#include <stdbool.h>
#include <stdint.h>
#include <ble_asha.h>
#include "asha_audio.h"

extern bool asha_active;

void ASHA_App_Init(void);
void ASHA_App_Deinit(void);
void ASHA_App_Process(void);
void APP_ASHA_CallbackHandler(uint8_t op_code, void *param);
void APP_Audio_Transfer(uint8_t *data, uint16_t length, uint8_t seq_num);
void APP_Audio_Start(void);
void APP_Audio_Disconnect(void);
void Volume_Set(int8_t vol);
void APP_ResetPrevSeqNumber(void);

#endif /* ASHA_APP_H */
