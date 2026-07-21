#include "app.h"
#include "ble_rempro.h"
#include "ble_rempro_cmd.h"
#include "bs300_ram_sync.h"
#include "bs300_startup.h"
#include "bs300_storage.h"

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

/* Reassembly + response buffers */
#define REASM_BUF_SIZE  100
static uint8_t reasm_buf[REASM_BUF_SIZE];
static uint8_t reasm_len;
static bool    reasm_pending;
static uint8_t s_device_on = 1;   /* tracks MUTE/ACTIVE state */

void rempro_reasm_append(const uint8_t *data, uint8_t len)
{
    if (reasm_len + len > REASM_BUF_SIZE) {
        reasm_len = 0;   /* overflow — drop and restart */
        reasm_pending = false;
        return;
    }
    memcpy(reasm_buf + reasm_len, data, len);
    reasm_len += len;
    reasm_pending = true;
}

void rempro_reasm_reset(void)
{
    reasm_len = 0;
    reasm_pending = false;
}

#define TX_BUF_SIZE     100
static uint8_t tx_buf[TX_BUF_SIZE];

/* -------- helpers -------- */

static void print_hex(const char *tag, const uint8_t *buf, uint8_t len)
{
    PRINTF("[REMPRO] %s (%uB):", tag, len);
    for (uint8_t i = 0; i < len; i++) PRINTF(" %02X", buf[i]);
    PRINTF("\r\n");
}

static uint8_t hdlc_fcs(const uint8_t *data, uint8_t len)
{
    uint8_t sum = 0;
    while (len--) sum += *data++;
    return sum;
}

/* HDLC byte-stuffing: 7E → 7D 5E,  7D → 7D 5D.
 * Writes to dst, returns stuffed length. */
static uint8_t hdlc_stuff(uint8_t *dst, const uint8_t *src, uint8_t len)
{
    uint8_t w = 0;
    for (uint8_t r = 0; r < len; r++) {
        uint8_t b = src[r];
        if (b == 0x7E)      { dst[w++] = 0x7D; dst[w++] = 0x5E; }
        else if (b == 0x7D) { dst[w++] = 0x7D; dst[w++] = 0x5D; }
        else                { dst[w++] = b; }
    }
    return w;
}

/* Build, stuff, and send an HDLC response in ≤20B chunks. */
static void hdlc_response(uint16_t cmd_id, uint8_t flag,
                          const uint8_t *data, uint8_t data_len)
{
    /* Step 1: build unstuffed frame body */
    uint8_t raw[TX_BUF_SIZE];
    raw[0] = HDLC_SYS_ID;
    raw[1] = (uint8_t)(cmd_id & 0xFF);
    raw[2] = (uint8_t)((cmd_id >> 8) & 0xFF);
    raw[3] = flag;
    uint8_t raw_len = 4;
    if (data_len) {
        memcpy(raw + raw_len, data, data_len);
        raw_len += data_len;
    }
    raw[raw_len] = hdlc_fcs(raw, raw_len);
    raw_len++;

    /* Step 2: stuff body + FCS, wrap with 7E delimiters */
    uint8_t *p = tx_buf;
    *p++ = HDLC_SEPARATOR;
    p += hdlc_stuff(p, raw, raw_len);
    *p++ = HDLC_SEPARATOR;
    uint8_t frame_len = (uint8_t)(p - tx_buf);

    /* Step 3: split into ≤20B chunks and send */
    print_hex("TX frame", tx_buf, frame_len);
    uint8_t offset = 0;
    uint8_t idx = 0;
    while (offset < frame_len) {
        uint8_t chunk = frame_len - offset;
        if (chunk > 20) chunk = 20;
        print_hex("TX chunk", tx_buf + offset, chunk);
        RemproService_SendNotification(ble_env.conidx,
                                       REMPRO_IDX_ONOFF_VALUE_VAL,
                                       tx_buf + offset, chunk);
        offset += chunk;
        idx++;
    }
}

/* HDLC byte-unstuffing: 7D 5E → 7E,  7D 5D → 7D.
 * Processes buf[start .. end-1] in-place, returns new length. */
static uint8_t hdlc_unstuff(uint8_t *buf, uint8_t start, uint8_t end)
{
    uint8_t w = start;
    for (uint8_t r = start; r < end; r++) {
        if (buf[r] == 0x7D && (r + 1) < end) {
            uint8_t next = buf[r + 1];
            if (next == 0x5E)      { buf[w++] = 0x7E; r++; }
            else if (next == 0x5D) { buf[w++] = 0x7D; r++; }
            else                   { buf[w++] = buf[r]; }
        } else {
            buf[w++] = buf[r];
        }
    }
    return w;
}

/* Parse a complete HDLC frame from buf[0..len-1].
 * Returns pointer to data payload, or NULL on failure.
 * On success, sets *cmd_id and *data_len, and *consumed = raw frame length
 * (before unstuffing). */
static const uint8_t *hdlc_parse_frame(const uint8_t *buf, uint8_t len,
                                       uint16_t *cmd_id, uint8_t *data_len,
                                       uint8_t *consumed)
{
    *consumed = 0;
    if (len < 6) return NULL;
    if (buf[0] != HDLC_SEPARATOR) return NULL;

    uint8_t fcs_pos_raw = 0;
    for (uint8_t i = 1; i < len; i++) {
        if (buf[i] == HDLC_SEPARATOR) {
            fcs_pos_raw = i - 1;
            *consumed = i + 1;
            break;
        }
    }
    if (fcs_pos_raw < 4) return NULL;

    /* Unstuff frame body (SYS..FCS, between delimiters).
     * Must copy because the raw data is const (in reasm_buf). */
    uint8_t unstuffed[REASM_BUF_SIZE];
    uint8_t body_len = fcs_pos_raw;
    memcpy(unstuffed, buf + 1, body_len);
    body_len = hdlc_unstuff(unstuffed, 0, body_len);

    if (body_len < 4) return NULL;   /* need at least SY + CMDL + CMDH + FCS */
    if (unstuffed[0] != HDLC_SYS_ID) return NULL;

    *cmd_id = unstuffed[1] | ((uint16_t)unstuffed[2] << 8);

    /* FCS is the last byte of unstuffed body */
    uint8_t fcs_pos = body_len - 1;
    if (fcs_pos < 3) return NULL;

    uint8_t exp = hdlc_fcs(unstuffed, fcs_pos);
    if (unstuffed[fcs_pos] != exp) {
        PRINTF("[REMPRO] FCS err got=%02X exp=%02X\r\n", unstuffed[fcs_pos], exp);
        *consumed = 0;  /* wait for more data — FCS may be split across chunks */
        return NULL;
    }

    *data_len = fcs_pos - 3;
    if (*data_len > 0) {
        /* Copy unstuffed data into reasm_buf (safe: reasm_buf is not const) */
        memcpy((uint8_t *)buf, unstuffed + 3, *data_len);
        return buf;  /* data now sits at start of buf */
    }
    return NULL;
}

/* Build, stuff, and send an HDLC push frame (device→app, SYS_ID=1, no Flag).
 * Same chunking logic as hdlc_response, but SYS_ID=1 and no Flag byte. */
static void hdlc_push(uint16_t cmd_id, const uint8_t *data, uint8_t data_len)
{
    uint8_t raw[TX_BUF_SIZE];
    raw[0] = HDLC_SYS_ID_DEVICE;
    raw[1] = (uint8_t)(cmd_id & 0xFF);
    raw[2] = (uint8_t)((cmd_id >> 8) & 0xFF);
    uint8_t raw_len = 3;
    if (data_len) {
        memcpy(raw + raw_len, data, data_len);
        raw_len += data_len;
    }
    raw[raw_len] = hdlc_fcs(raw, raw_len);
    raw_len++;

    uint8_t *p = tx_buf;
    *p++ = HDLC_SEPARATOR;
    p += hdlc_stuff(p, raw, raw_len);
    *p++ = HDLC_SEPARATOR;
    uint8_t frame_len = (uint8_t)(p - tx_buf);

    print_hex("TX push", tx_buf, frame_len);
    uint8_t offset = 0;
    while (offset < frame_len) {
        uint8_t chunk = frame_len - offset;
        if (chunk > 20) chunk = 20;
        RemproService_SendNotification(ble_env.conidx,
                                       REMPRO_IDX_ONOFF_VALUE_VAL,
                                       tx_buf + offset, chunk);
        offset += chunk;
    }
}

/* Active push: notify app of program switch (CMD=5, SYS_ID=1).
 * Protocol: SYS_ID(1) + CMD_ID(2) + Scene_ID(1) */
void rempro_push_scene_change(uint8_t scene_id)
{
    if (ble_env.state != APPM_CONNECTED) return;
    PRINTF("[REMPRO] push scene=%u\r\n", scene_id);
    hdlc_push(CMD_PUSH_SCENE, &scene_id, 1);
}

/* Active push: notify app of volume change (CMD=4, SYS_ID=1).
 * Protocol: SYS_ID(1) + CMD_ID(2) + Current_Number(1) + Device_Type(1)
 *         + Volume(1) + Volume2(1) */
void rempro_push_volume_change(uint8_t prog, uint8_t volume)
{
    if (ble_env.state != APPM_CONNECTED) return;
    uint8_t d[4];
    d[0] = prog;       /* Current_Number */
    d[1] = 1;          /* Device_Type: 1=left (single device) */
    d[2] = volume;     /* Volume */
    d[3] = volume;     /* Volume2 (same as Volume for single device) */
    PRINTF("[REMPRO] push vol: prog=%u vol=%u\r\n", prog, volume);
    hdlc_push(CMD_PUSH_VOLUME, d, 4);
}

/* Active push: notify app of initial status done (CMD=6, SYS_ID=1).
 * Protocol: SYS_ID(1) + CMD_ID(2) + Device_Type(1) + Initial_Status(1)
 *   Device_Type=1 (left), Initial_Status=2 (初始化完成) */
void rempro_push_initial_status_done(void)
{
    if (ble_env.state != APPM_CONNECTED) return;
    uint8_t d[2];
    d[0] = 1;   /* Device_Type: 1=left (single device) */
    d[1] = 2;   /* Initial_Status: 2=初始化完成 */
    PRINTF("[REMPRO] push initial status done\r\n");
    hdlc_push(CMD_PUSH_INITIAL_STATUS, d, 2);
}

/* Active push: notify app of audiometry exit (CMD=6, SYS_ID=1).
 * Protocol: SYS_ID(1) + CMD_ID(2) + Device_Type(1) + Initial_Status(1)
 *   Device_Type=1 (left), Initial_Status=1 (未初始化) */
void rempro_push_audiometry_exit(void)
{
    if (ble_env.state != APPM_CONNECTED) return;
    uint8_t d[2];
    d[0] = 1;   /* Device_Type: 1=left (single device) */
    d[1] = 1;   /* Initial_Status: 1=未初始化 */
    PRINTF("[REMPRO] push audiometry exit\r\n");
    hdlc_push(CMD_PUSH_INITIAL_STATUS, d, 2);
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

    /* Left side volume — no tone, same path as EQ (re-encode bin_gain → 0x8060B2) */
    if (dev_type == 0 || dev_type == 1) {
        app_env.volume = volume;
        bs300_set_volume_notone_async(volume, NULL);
        /* Flash persist deferred to BLE disconnect — see GAPC_DisconnectInd */
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
    if (bs300_sync_is_busy()) { hdlc_response(CMD_SETDEVICEONOFF, 1, NULL, 0); return; }

    uint8_t dev_type = data[0];
    uint8_t onoff    = data[1];

    if (onoff) {
        bs300_active();
        s_device_on = 1;
    } else {
        bs300_mute();
        s_device_on = 0;
    }

    PRINTF("[REMPRO] SetDeviceOnOff: dev=%u onoff=%u\r\n", dev_type, onoff);
    uint8_t status = 1;
    hdlc_response(CMD_SETDEVICEONOFF, 0, &status, 1);
}

/* ID:5  SetFeedbackOnOff — RAM-only, overrides flash dfbc_enable_mode bit7 */
static void cmd_setfeedbackonoff(const uint8_t *data, uint8_t len)
{
    if (len < 3) { hdlc_response(CMD_SETFEEDBACKONOFF, 1, NULL, 0); return; }
    if (bs300_sync_is_busy()) { hdlc_response(CMD_SETFEEDBACKONOFF, 1, NULL, 0); return; }

    uint8_t dev_type = data[0];
    uint8_t prog     = data[1];
    uint8_t onoff    = data[2];

    if (prog >= 4) { hdlc_response(CMD_SETFEEDBACKONOFF, 1, NULL, 0); return; }

    bs300_set_feedback_onoff(prog, onoff);

    /* Reload struct from flash to get the target program's DFBC mode,
     * then apply feedback override.  Don't change s_dsp_state unless the
     * target program is currently active. */
    bs300_prog_struct_t target;
    const bs300_calib_t *calib = bs300_get_cached_calib();
    bs300_storage_load_program(prog, bs300_work_buf);
    bs300_flash_to_struct(bs300_work_buf, &target);

    if (onoff) {
        uint8_t mode = target.modules.dfbc_enable_mode & 0x0F;
        if (mode == 0) mode = 0x07;
        target.modules.dfbc_enable_mode = 0x80 | mode;
    } else {
        target.modules.dfbc_enable_mode = 0x00;
    }

    /* Send DFBC I2C only if target is the active program */
    if (prog == bs300_get_active_prog()) {
        bs300_prog_struct_t *dsp = bs300_get_dsp_state();
        dsp->modules.dfbc_enable_mode = target.modules.dfbc_enable_mode;

        uint8_t dfbc_data[48];
        if (onoff) {
            bs300_encode_dfbc(&dsp->modules, calib, dfbc_data);
        } else {
            memset(dfbc_data, 0, 48);
        }
        bs300_advanced_write(BS300_CMD_DFBC, dfbc_data);
    }

    PRINTF("[REMPRO] SetFeedbackOnOff: dev=%u prog=%u onoff=%u fb[%u]=%u active=%u\r\n",
           dev_type, prog, onoff, prog, bs300_get_feedback_onoff(prog),
           bs300_get_active_prog());
    uint8_t status = 1;
    hdlc_response(CMD_SETFEEDBACKONOFF, 0, &status, 1);
}

/* ID:33  GetDeviceOnOff */
static void cmd_getdeviceonoff(void)
{
    uint8_t resp[2];
    resp[0] = s_device_on;   /* Left_OnOff */
    resp[1] = s_device_on;   /* Right_OnOff (same as left) */
    PRINTF("[REMPRO] GetDeviceOnOff: L=%u R=%u\r\n", resp[0], resp[1]);
    hdlc_response(CMD_GETDEVICEONOFF, 0, resp, 2);
}

/* ID:34  GetFeedbackOnOff */
static void cmd_getfeedbackonoff(const uint8_t *data, uint8_t len)
{
    uint8_t prog = 0;
    if (data != NULL && len >= 2) {
        prog = data[1];   /* Scene_ID */
        if (prog >= 4) prog = 0;
    }

    uint8_t onoff = bs300_get_feedback_onoff(prog);
    uint8_t resp[2];
    resp[0] = onoff;   /* Left_OnOff */
    resp[1] = onoff;   /* Right_OnOff (same as left) */

    PRINTF("[REMPRO] GetFeedbackOnOff: prog=%u onoff=%u\r\n", prog, onoff);
    hdlc_response(CMD_GETFEEDBACKONOFF, 0, resp, 2);
}

/* ID:4  GetBatteryInfo */
static void cmd_getbatteryinfo(void)
{
    uint8_t resp_data[2];
    resp_data[0] = app_env.batt_lvl;  /* Left_Battery: measured */
    resp_data[1] = 0;                 /* Right_Battery: 0 (single device) */

    PRINTF("[REMPRO] GetBatteryInfo: L=%u R=0\r\n", app_env.batt_lvl);
    hdlc_response(CMD_GETBATTERYINFO, 0, resp_data, 2);
}

/* ID:16  SetCurrentScene */
static void cmd_setcurrentscene(const uint8_t *data, uint8_t len)
{
    if (len < 2) { hdlc_response(CMD_SETCURRENTSCENE, 1, NULL, 0); return; }

    uint8_t dev_type = data[0];
    uint8_t scene_id = data[1];

    PRINTF("[REMPRO] SetCurrentScene: dev=%u active=%u -> %u\r\n",
           dev_type, bs300_get_active_prog(), scene_id);

    if (scene_id < 4) {
        bs300_switch_program_async(scene_id, NULL);
    }

    uint8_t status = 1;
    hdlc_response(CMD_SETCURRENTSCENE, 0, &status, 1);
}

/* ID:15  GetCurrentScene */
static void cmd_getcurrentscene(void)
{
    uint8_t resp_data[12];
    bs300_prog_struct_t *dsp = bs300_get_dsp_state();
    uint8_t prog = bs300_get_active_prog();

    resp_data[0]  = prog;                          /* Left_Scene_ID */
    resp_data[1]  = prog;                          /* Right_Scene_ID (same for single device) */
    resp_data[2]  = dsp->modules.volume_level;     /* Volume_Left */
    resp_data[3]  = dsp->modules.volume_level;     /* Volume_Right (same) */
    resp_data[4]  = bs300_get_prog_denoise(prog);  /* Denoise 0-4 */
    resp_data[5]  = (uint8_t)(int8_t)dsp->modules.eq_low;   /* Left_EQ_Low [-5,5] */
    resp_data[6]  = (uint8_t)(int8_t)dsp->modules.eq_mid;   /* Left_EQ_Mid [-5,5] */
    resp_data[7]  = (uint8_t)(int8_t)dsp->modules.eq_high;  /* Left_EQ_High [-5,5] */
    resp_data[8]  = (uint8_t)(int8_t)dsp->modules.eq_low;   /* Right_EQ_Low (same) */
    resp_data[9]  = (uint8_t)(int8_t)dsp->modules.eq_mid;   /* Right_EQ_Mid (same) */
    resp_data[10] = (uint8_t)(int8_t)dsp->modules.eq_high;  /* Right_EQ_High (same) */
    resp_data[11] = 0;                              /* reserved/padding */

    PRINTF("[REMPRO] GetCurrentScene: prog=%u vol=%u denoise=%u eq=%d/%d/%d\r\n",
           prog, dsp->modules.volume_level, bs300_get_prog_denoise(prog),
           dsp->modules.eq_low, dsp->modules.eq_mid, dsp->modules.eq_high);
    hdlc_response(CMD_GETCURRENTSCENE, 0, resp_data, 12);
}

/* ID:26  GetDeviceConfig */
static void cmd_getdeviceconfig(void)
{
    uint8_t d[32];
    uint8_t pos = 0;

    /* Device_OnOff / Feedback_OnOff: only for Echo402BT, removed */

    d[pos++] = 1; d[pos++] = 0; d[pos++] = 0; d[pos++] = 0; /* Version 1.0.0.0 */
    d[pos++] = 3;  /* Program_Num */

    /* Left side */
    memcpy(d + pos, bdaddr, 6); pos += 6;               /* Address_Left MAC */
    d[pos++] = 101; d[pos++] = 0;                        /* Product_Type = 101 */
    d[pos++] = 1;                                        /* Chip_Type = 1 (BS300) */
    d[pos++] = 2;                                        /* Turn_Number */
    d[pos++] = 16;                                       /* Channel_Number */

    /* Right side (same as left) */
    memcpy(d + pos, bdaddr, 6); pos += 6;               /* Address_Right MAC */
    d[pos++] = 101; d[pos++] = 0;                        /* Product_Type = 101 */
    d[pos++] = 1;                                        /* Chip_Type = 1 (BS300) */
    d[pos++] = 2;                                        /* Turn_Number */
    d[pos++] = 16;                                       /* Channel_Number */

    d[pos++] = 9;  /* Volume_Number */

    PRINTF("[REMPRO] GetDeviceConfig\r\n");
    hdlc_response(CMD_GETDEVICECONFIG, 0, d, pos);
}

/* ID:10  SetEqualizer */
static void cmd_setequalizer(const uint8_t *data, uint8_t len)
{
    if (len < 3) { hdlc_response(CMD_SETEQUALIZER, 1, NULL, 0); return; }

    uint8_t dev_type = data[0];
    uint8_t eq_type  = data[1];
    int8_t  dB       = (int8_t)data[2];  /* raw step [-5,5] */

    if (dB > 5)   dB = 5;
    if (dB < -5)  dB = -5;

    bs300_prog_struct_t *dsp = bs300_get_dsp_state();
    int8_t low  = dsp->modules.eq_low;
    int8_t mid  = dsp->modules.eq_mid;
    int8_t high = dsp->modules.eq_high;

    switch (eq_type) {
    case 0: low  = dB; break;   /* bass  ≤500Hz */
    case 1: mid  = dB; break;   /* mid   500-2000Hz */
    case 2: high = dB; break;   /* treble >2000Hz */
    default: break;
    }

    PRINTF("[REMPRO] SetEqualizer: dev=%u type=%u dB=%d → L=%d M=%d H=%d\r\n",
           dev_type, eq_type, dB, low, mid, high);
    bs300_set_eq_async(low, mid, high, NULL);

    uint8_t status = 1;
    hdlc_response(CMD_SETEQUALIZER, 0, &status, 1);
}

/* Shared buffer for fitting command handlers — 490B, avoids stack alloc */
static bs300_prog_struct_t s_fit_buf;

/* Commit s_fit_buf to flash for the given program.
 * Caller must have already loaded raw→struct into s_fit_buf and modified it.
 * bs300_work_buf still holds the original raw 480B from the load call.
 * sync_dsp: if true and target is the active program, trigger I2C resync
 * to DSP; if false, flash-only (takes effect on next program switch/reboot). */
static int fitting_commit(uint8_t prog_idx, bool sync_dsp)
{
    uint8_t active = bs300_get_active_prog();

    if (bs300_struct_to_flash(&s_fit_buf, bs300_work_buf) < 0) return -1;
    bs300_storage_write_program(prog_idx, bs300_work_buf);

    PRINTF("[FITTING] commit prog=%u active=%u sync=%d\r\n",
           prog_idx, active, sync_dsp);

    if (sync_dsp && prog_idx == active) {
        PRINTF("[FITTING] >>> I2C resync to DSP <<<\r\n");
        bs300_resync_diff_async(&s_fit_buf, NULL);
    } else {
        PRINTF("[FITTING] flash-only, no I2C sync\r\n");
    }
    return 0;
}

/* ID:6  SetGain */
static void cmd_setgain(const uint8_t *data, uint8_t len)
{
    if (len < 4 || ((len - 2) & 1)) {
        hdlc_response(CMD_SETGAIN, 1, NULL, 0); return;
    }
    if (bs300_sync_is_busy()) { hdlc_response(CMD_SETGAIN, 1, NULL, 0); return; }

    uint8_t dev_type = data[0];
    uint8_t prog     = data[1];
    uint8_t pairs    = (len - 2) / 2;
    uint8_t i;

    if (prog >= 4) { hdlc_response(CMD_SETGAIN, 1, NULL, 0); return; }

    PRINTF("[REMPRO] SetGain IN: target_prog=%u active_prog=%u pairs=%u\r\n",
           prog, bs300_get_active_prog(), pairs);

    bs300_print_settings();

    /* Load flash → struct */
    bs300_storage_load_program(prog, bs300_work_buf);
    if (bs300_flash_to_struct(bs300_work_buf, &s_fit_buf) < 0) {
        hdlc_response(CMD_SETGAIN, 1, NULL, 0); return;
    }

    for (i = 0; i < pairs; i++) {
        uint8_t spectrum = data[2 + i * 2];
        int16_t raw_val  = data[3 + i * 2];
        if (spectrum < 32) {
            /* App sends Flash raw: raw = 27 + value_in_MT → value_in_MT = raw - 27 */
            int16_t vmt = raw_val - 27;
            if (vmt > 100) vmt = 100;
            s_fit_buf.wdrc.bin_gain[spectrum] = (int8_t)vmt;
        }
    }

    PRINTF("[REMPRO] SetGain: dev=%u prog=%u pairs=%u → gain[0-3]=%d,%d,%d,%d\r\n",
           dev_type, prog, pairs,
           s_fit_buf.wdrc.bin_gain[0], s_fit_buf.wdrc.bin_gain[1],
           s_fit_buf.wdrc.bin_gain[2], s_fit_buf.wdrc.bin_gain[3]);
    bs300_reset_user_params(prog);
    fitting_commit(prog, false);
    hdlc_response(CMD_SETGAIN, 0, NULL, 0);
}

/* ID:7  SetMPO */
static void cmd_setmpo(const uint8_t *data, uint8_t len)
{
    if (len < 4 || ((len - 2) & 1)) {
        hdlc_response(CMD_SETMPO, 1, NULL, 0); return;
    }
    if (bs300_sync_is_busy()) { hdlc_response(CMD_SETMPO, 1, NULL, 0); return; }

    uint8_t dev_type = data[0];
    uint8_t prog     = data[1];
    uint8_t pairs    = (len - 2) / 2;
    uint8_t i;

    if (prog >= 4) { hdlc_response(CMD_SETMPO, 1, NULL, 0); return; }

    PRINTF("[REMPRO] SetMPO IN: target_prog=%u active_prog=%u pairs=%u\r\n",
           prog, bs300_get_active_prog(), pairs);

    bs300_print_settings();

    bs300_storage_load_program(prog, bs300_work_buf);
    if (bs300_flash_to_struct(bs300_work_buf, &s_fit_buf) < 0) {
        hdlc_response(CMD_SETMPO, 1, NULL, 0); return;
    }

    for (i = 0; i < pairs; i++) {
        uint8_t channel = data[2 + i * 2];
        int16_t raw_val = data[3 + i * 2];
        if (channel < 16) {
            /* App sends Flash raw: raw = value_in_MT - 30 → value_in_MT = raw + 30 */
            s_fit_buf.wdrc.lmt_th_db[channel] = (int8_t)(raw_val + 30);
        }
    }

    PRINTF("[REMPRO] SetMPO: dev=%u prog=%u pairs=%u → lmt_th[0-3]=%d,%d,%d,%d\r\n",
           dev_type, prog, pairs,
           s_fit_buf.wdrc.lmt_th_db[0], s_fit_buf.wdrc.lmt_th_db[1],
           s_fit_buf.wdrc.lmt_th_db[2], s_fit_buf.wdrc.lmt_th_db[3]);
    fitting_commit(prog, false);
    hdlc_response(CMD_SETMPO, 0, NULL, 0);
}

/* ID:8  SetCompressRatio */
static void cmd_setcompressratio(const uint8_t *data, uint8_t len)
{
    if (len < 5 || ((len - 3) & 1)) {
        hdlc_response(CMD_SETCOMPRESSRATIO, 1, NULL, 0); return;
    }
    if (bs300_sync_is_busy()) {
        hdlc_response(CMD_SETCOMPRESSRATIO, 1, NULL, 0); return;
    }

    uint8_t dev_type  = data[0];
    uint8_t prog      = data[1];
    uint8_t turn_num  = data[2];
    uint8_t pairs     = (len - 3) / 2;
    uint8_t i;

    if (prog >= 4) {
        hdlc_response(CMD_SETCOMPRESSRATIO, 1, NULL, 0); return;
    }

    PRINTF("[REMPRO] SetCompressRatio IN: target_prog=%u active_prog=%u pairs=%u\r\n",
           prog, bs300_get_active_prog(), pairs);

    bs300_print_settings();

    bs300_storage_load_program(prog, bs300_work_buf);
    if (bs300_flash_to_struct(bs300_work_buf, &s_fit_buf) < 0) {
        hdlc_response(CMD_SETCOMPRESSRATIO, 1, NULL, 0); return;
    }

    for (i = 0; i < pairs; i++) {
        uint8_t channel = data[3 + i * 2];
        uint8_t step    = data[4 + i * 2];
        if (channel < 16) {
            if (turn_num == 0)
                s_fit_buf.wdrc.kp1_r_idx[channel] = step;
            else
                s_fit_buf.wdrc.kp2_r_idx[channel] = step;
        }
    }

    PRINTF("[REMPRO] SetCompressRatio: dev=%u prog=%u turn=%u pairs=%u → CR0[0-3]=%d,%d,%d,%d CR1[0-3]=%d,%d,%d,%d\r\n",
           dev_type, prog, turn_num, pairs,
           s_fit_buf.wdrc.kp1_r_idx[0], s_fit_buf.wdrc.kp1_r_idx[1],
           s_fit_buf.wdrc.kp1_r_idx[2], s_fit_buf.wdrc.kp1_r_idx[3],
           s_fit_buf.wdrc.kp2_r_idx[0], s_fit_buf.wdrc.kp2_r_idx[1],
           s_fit_buf.wdrc.kp2_r_idx[2], s_fit_buf.wdrc.kp2_r_idx[3]);
    fitting_commit(prog, false);
    hdlc_response(CMD_SETCOMPRESSRATIO, 0, NULL, 0);
}

/* ID:9  SetDenoise — RAM-only, like volume. Does NOT modify program flash. */
static void cmd_setdenoise(const uint8_t *data, uint8_t len)
{
    if (len < 3) {
        hdlc_response(CMD_SETDENOISE, 1, NULL, 0); return;
    }
    if (bs300_sync_is_busy()) {
        hdlc_response(CMD_SETDENOISE, 1, NULL, 0); return;
    }

    uint8_t dev_type = data[0];
    uint8_t prog     = data[1];
    uint8_t level    = data[2];

    if (prog >= 4 || level > 5) {
        hdlc_response(CMD_SETDENOISE, 1, NULL, 0); return;
    }

    PRINTF("[REMPRO] SetDenoise: dev=%u prog=%u level=%u\r\n",
           dev_type, prog, level);
    bs300_set_prog_denoise(prog, level);

    /* If active program, re-sync to apply new ENR max_att to DSP */
    if (prog == bs300_get_active_prog()) {
        bs300_switch_program_async(prog, NULL);
    }
    hdlc_response(CMD_SETDENOISE, 0, NULL, 0);
}

/* ================================================================
 * Audiometry frequency table: Spectrum (CMD 13) → Hz
 * ================================================================ */
static const uint16_t s_audiometry_freq[17] = {
    250, 500, 1000, 1500, 2000, 2500, 3000, 3500,
    4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000
};

/* ID:13  SetPlayVoice — play pure tone at given frequency & dB */
static void cmd_setplayvoice(const uint8_t *data, uint8_t len)
{
    if (len < 3) { hdlc_response(CMD_SETPLAYVOICE, 1, NULL, 0); return; }

    uint8_t spectrum = data[1];
    uint8_t decibel  = data[2];
    uint8_t status   = 1;

    if (spectrum < 17 && decibel >= 20 && decibel <= 100) {
        uint16_t freq_hz = s_audiometry_freq[spectrum];
        const bs300_calib_t *calib = bs300_get_cached_calib();

        /* Protocol flow: Mute → ITG write → Active */
        bs300_mute();
        if (bs300_itg_write(decibel, freq_hz, calib) == 0)
            bs300_active();
        else
            status = 0;
    } else {
        status = 0;
    }

    PRINTF("[REMPRO] SetPlayVoice: spec=%u dB=%u freq=%u\r\n",
           spectrum, decibel,
           (spectrum < 17) ? s_audiometry_freq[spectrum] : 0);
    hdlc_response(CMD_SETPLAYVOICE, 0, &status, 1);
}

/* ID:14  SetStopVoice — stop playing pure tone */
static void cmd_setstopvoice(const uint8_t *data, uint8_t len)
{
    (void)data; (void)len;

    /* Protocol flow: Mute → ITG clear */
    bs300_mute();
    bs300_itg_clear();

    PRINTF("[REMPRO] SetStopVoice\r\n");
    uint8_t status = 1;
    hdlc_response(CMD_SETSTOPVOICE, 0, &status, 1);
}

/* ID:40  SetAudiometryStatus — enter/exit audiometry */
static void cmd_setaudiometrystatus(const uint8_t *data, uint8_t len)
{
    if (len < 2) { hdlc_response(CMD_SETAUDIOMETRYSTATUS, 1, NULL, 0); return; }
    if (bs300_sync_is_busy()) { hdlc_response(CMD_SETAUDIOMETRYSTATUS, 1, NULL, 0); return; }

    uint8_t fitting_status = data[1];

    PRINTF("[REMPRO] SetAudiometryStatus: status=%u\r\n", fitting_status);

    /* Respond immediately — acknowledge receipt */
    {
        uint8_t ack = 1;
        hdlc_response(CMD_SETAUDIOMETRYSTATUS, 0, &ack, 1);
    }

    /* Then do the work; push initial-status-done after enter completes */
    switch (fitting_status) {
    case 0:  /* Enter audiometry */
        if (bs300_audiometry_enter() == 0) {
            bs300_set_audiometry_state(BS300_AUDIOMETRY_TEST);
            /* Notify app after 2s DSP stabilization, non-blocking */
            bs300_schedule_delayed_push(rempro_push_initial_status_done, 200);
        }
        break;
    case 1:  /* Exit audiometry */
        bs300_audiometry_exit();
        bs300_schedule_delayed_push(rempro_push_audiometry_exit, 200);
        break;
    default:
        break;
    }
}

/* ================================================================
 * Main dispatcher
 * ================================================================ */
void rempro_cmd_process(void)
{
    if (!reasm_pending) return;
    reasm_pending = false;

    /* Process as many complete frames as we can */
    while (reasm_len >= 6) {   /* minimum frame: 7E SY CMDL CMDH FCS 7E = 6B */

        /* Skip leading garbage until we find 0x7E */
        if (reasm_buf[0] != HDLC_SEPARATOR) {
            uint8_t skip = 1;
            while (skip < reasm_len && reasm_buf[skip] != HDLC_SEPARATOR)
                skip++;
            PRINTF("[REMPRO] skipping %u leading bytes before 7E\r\n", skip);
            reasm_len -= skip;
            memmove(reasm_buf, reasm_buf + skip, reasm_len);
            if (reasm_len < 7) return;
        }

        uint16_t cmd_id;
        uint8_t data_len = 0, consumed = 0;
        const uint8_t *data = hdlc_parse_frame(reasm_buf, reasm_len,
                                               &cmd_id, &data_len, &consumed);
        if (consumed == 0) {
            /* No closing 7E found — wait for more chunks */
            PRINTF("[REMPRO] waiting: have %u bytes, no end 7E\r\n", reasm_len);
            return;
        }
        /* consumed > 0: frame parsed, data may be NULL if no payload */

        print_hex("RX frame", reasm_buf, consumed);
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
        case CMD_SETFEEDBACKONOFF:
            if (data) cmd_setfeedbackonoff(data, data_len);
            else hdlc_response(CMD_SETFEEDBACKONOFF, 1, NULL, 0);
            break;
        case CMD_GETBATTERYINFO:
            cmd_getbatteryinfo();
            break;
        case CMD_SETCURRENTSCENE:
            if (data) cmd_setcurrentscene(data, data_len);
            else hdlc_response(CMD_SETCURRENTSCENE, 1, NULL, 0);
            break;
        case CMD_SETGAIN:
            if (data) cmd_setgain(data, data_len);
            else hdlc_response(CMD_SETGAIN, 1, NULL, 0);
            break;
        case CMD_SETMPO:
            if (data) cmd_setmpo(data, data_len);
            else hdlc_response(CMD_SETMPO, 1, NULL, 0);
            break;
        case CMD_SETCOMPRESSRATIO:
            if (data) cmd_setcompressratio(data, data_len);
            else hdlc_response(CMD_SETCOMPRESSRATIO, 1, NULL, 0);
            break;
        case CMD_SETDENOISE:
            if (data) cmd_setdenoise(data, data_len);
            else hdlc_response(CMD_SETDENOISE, 1, NULL, 0);
            break;
        case CMD_SETEQUALIZER:
            if (data) cmd_setequalizer(data, data_len);
            else hdlc_response(CMD_SETEQUALIZER, 1, NULL, 0);
            break;
        case CMD_SETPLAYVOICE:
            if (data) cmd_setplayvoice(data, data_len);
            else hdlc_response(CMD_SETPLAYVOICE, 1, NULL, 0);
            break;
        case CMD_SETSTOPVOICE:
            if (data) cmd_setstopvoice(data, data_len);
            else hdlc_response(CMD_SETSTOPVOICE, 1, NULL, 0);
            break;
        case CMD_SETAUDIOMETRYSTATUS:
            if (data) cmd_setaudiometrystatus(data, data_len);
            else hdlc_response(CMD_SETAUDIOMETRYSTATUS, 1, NULL, 0);
            break;
        case CMD_GETCURRENTSCENE:
            cmd_getcurrentscene();
            break;
        case CMD_GETDEVICECONFIG:
            cmd_getdeviceconfig();
            break;
        case CMD_GETDEVICEONOFF:
            cmd_getdeviceonoff();
            break;
        case CMD_GETFEEDBACKONOFF:
            cmd_getfeedbackonoff(data, data_len);
            break;
        default:
            PRINTF("[REMPRO] unknown CMD=%u\r\n", cmd_id);
            break;
        }

        /* Remove processed frame from buffer */
        reasm_len -= consumed;
        if (reasm_len > 0) memmove(reasm_buf, reasm_buf + consumed, reasm_len);
    }
}
