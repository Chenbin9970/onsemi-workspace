#include "app.h"
#include "ble_rempro.h"
#include "ble_rempro_cmd.h"
#include "bs300_ram_sync.h"

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

/* HDLC response buffer (max 20 bytes for ONOFF notify) */
static uint8_t resp_buf[20];
static uint8_t resp_len;

/* FCS: bytewise sum of payload */
static uint8_t hdlc_fcs(const uint8_t *data, uint8_t len)
{
    uint8_t sum = 0;
    while (len--) sum += *data++;
    return sum;
}

/* Build and send HDLC response frame */
static void hdlc_response(uint16_t cmd_id, uint8_t flag, const uint8_t *data, uint8_t data_len)
{
    uint8_t *p = resp_buf;
    *p++ = HDLC_SEPARATOR;
    *p++ = HDLC_SYS_ID;
    *p++ = (uint8_t)(cmd_id & 0xFF);
    *p++ = (uint8_t)((cmd_id >> 8) & 0xFF);
    *p++ = flag;
    if (data_len > 0) {
        memcpy(p, data, data_len);
        p += data_len;
    }
    /* FCS covers SYS_ID..data */
    *p = hdlc_fcs(resp_buf + 1, (uint8_t)(p - resp_buf - 1));
    p++;
    *p++ = HDLC_SEPARATOR;
    resp_len = (uint8_t)(p - resp_buf);

    RemproService_SendNotification(ble_env.conidx, REMPRO_IDX_ONOFF_VALUE_VAL,
                                   resp_buf, resp_len);
}

/* Parse HDLC frame from role_value, return data pointer and length, or NULL */
static const uint8_t *hdlc_parse(uint16_t *cmd_id, uint8_t *data_len)
{
    uint8_t *rx = rempro_env.role_value;
    uint8_t len = REMPRO_ROLE_VALUE_MAX_LENGTH;

    if (len < 7) return NULL;                       /* min: 7E SY CMDL CMDH FCS 7E */
    if (rx[0] != HDLC_SEPARATOR) return NULL;

    uint8_t sys_id = rx[1];
    if (sys_id != HDLC_SYS_ID) return NULL;

    *cmd_id = rx[2] | ((uint16_t)rx[3] << 8);

    /* Find closing 0x7E */
    uint8_t fcs_pos = 0;
    for (uint8_t i = 4; i < len; i++) {
        if (rx[i] == HDLC_SEPARATOR) {
            fcs_pos = i - 1;
            break;
        }
    }
    if (fcs_pos == 0 || fcs_pos < 4) return NULL;

    /* Validate FCS */
    uint8_t expected_fcs = hdlc_fcs(rx + 1, fcs_pos - 1);
    if (rx[fcs_pos] != expected_fcs) {
        PRINTF("[REMPRO] FCS mismatch: got=%02X exp=%02X\r\n", rx[fcs_pos], expected_fcs);
        return NULL;
    }

    *data_len = fcs_pos - 4; /* bytes between CMD_ID and FCS */
    return (fcs_pos > 4) ? &rx[4] : NULL;
}

/* ================================================================
 * Command Handlers
 * ================================================================ */

/* ID:2  SetVolume */
static void cmd_setvolume(const uint8_t *data, uint8_t len)
{
    if (len < 3) { hdlc_response(CMD_SETVOLUME, 1, NULL, 0); return; }

    uint8_t dev_type  = data[0];
    uint8_t volume    = data[1];
    uint8_t volume2   = data[2];

    if (volume > 9) volume = 9;
    if (volume2 > 9) volume2 = 9;

    /* Left side volume */
    if (dev_type == 0 || dev_type == 1) {
        app_env.volume = volume;
        bs300_set_volume_async(volume, NULL);
    }
    /* Right side not supported on this device — ignore */

    PRINTF("[REMPRO] SetVolume: dev=%u vol=%u vol2=%u\r\n", dev_type, volume, volume2);

    uint8_t status = 1;
    hdlc_response(CMD_SETVOLUME, 0, &status, 1);
}

/* ID:3  SetDeviceOnOff */
static void cmd_setdeviceonoff(const uint8_t *data, uint8_t len)
{
    if (len < 2) { hdlc_response(CMD_SETDEVICEONOFF, 1, NULL, 0); return; }

    uint8_t dev_type = data[0];
    uint8_t onoff    = data[1];

    PRINTF("[REMPRO] SetDeviceOnOff: dev=%u onoff=%u\r\n", dev_type, onoff);

    /* TODO: implement actual on/off control */
    uint8_t status = 1;
    hdlc_response(CMD_SETDEVICEONOFF, 0, &status, 1);
}

/* ID:4  GetBatteryInfo */
static void cmd_getbatteryinfo(void)
{
    uint8_t batt_lvl = app_env.batt_lvl;
    if (batt_lvl > 100) batt_lvl = 100;

    uint8_t resp_data[2];
    resp_data[0] = batt_lvl;  /* Left_Battery */
    resp_data[1] = batt_lvl;  /* Right_Battery (same for single device) */

    PRINTF("[REMPRO] GetBatteryInfo: %u%%\r\n", batt_lvl);
    hdlc_response(CMD_GETBATTERYINFO, 0, resp_data, 2);
}

/* ID:16  SetCurrentScene */
static void cmd_setcurrentscene(const uint8_t *data, uint8_t len)
{
    if (len < 2) { hdlc_response(CMD_SETCURRENTSCENE, 1, NULL, 0); return; }

    uint8_t dev_type = data[0];
    uint8_t scene_id = data[1];

    if (scene_id < 4) {
        bs300_switch_program_async(scene_id, NULL);
    }

    PRINTF("[REMPRO] SetCurrentScene: dev=%u scene=%u\r\n", dev_type, scene_id);

    uint8_t status = 1;
    hdlc_response(CMD_SETCURRENTSCENE, 0, &status, 1);
}

/* ID:26  GetDeviceConfig */
static void cmd_getdeviceconfig(void)
{
    /* Build minimal response: product=1 (K24BE), chip=1 (BS300) */
    uint8_t resp_data[10];
    resp_data[0] = 0;              /* Device_OnOff (placeholder) */
    resp_data[1] = 0;              /* Feedback_OnOff (placeholder) */
    resp_data[2] = 1; resp_data[3] = 0; resp_data[4] = 0; resp_data[5] = 0; /* Version 1.0.0.0 */
    resp_data[6] = 4;              /* Program_Num */
    /* MAC (6B, skipped for brevity) */
    resp_data[7] = 1; resp_data[8] = 0; /* Product_Type = 1 (K24BE) */
    resp_data[9] = 1;              /* Chip_Type = 1 (BS300) */

    PRINTF("[REMPRO] GetDeviceConfig\r\n");
    hdlc_response(CMD_GETDEVICECONFIG, 0, resp_data, 10);
}

/* ================================================================
 * Main dispatcher
 * ================================================================ */
void rempro_cmd_process(void)
{
    if (!rempro_env.role_value_changed) return;
    rempro_env.role_value_changed = 0;

    uint16_t cmd_id;
    uint8_t data_len;
    const uint8_t *data = hdlc_parse(&cmd_id, &data_len);
    if (data == NULL && cmd_id == 0) return; /* bad frame, silently drop */

    PRINTF("[REMPRO] CMD=%u len=%u\r\n", cmd_id, data_len);

    switch (cmd_id) {
    case CMD_SETVOLUME:
        if (data) cmd_setvolume(data, data_len);
        else hdlc_response(CMD_SETVOLUME, 1, NULL, 0);
        break;
    case CMD_SETDEVICEONOFF:
        if (data) cmd_setdeviceonoff(data, data_len);
        else hdlc_response(CMD_SETDEVICEONOFF, 1, NULL, 0);
        break;
    case CMD_GETBATTERYINFO:
        cmd_getbatteryinfo();
        break;
    case CMD_SETCURRENTSCENE:
        if (data) cmd_setcurrentscene(data, data_len);
        else hdlc_response(CMD_SETCURRENTSCENE, 1, NULL, 0);
        break;
    case CMD_GETDEVICECONFIG:
        cmd_getdeviceconfig();
        break;
    default:
        PRINTF("[REMPRO] unknown CMD=%u\r\n", cmd_id);
        break;
    }
}
