#include "app.h"
#include "ble_rempro.h"
#include "ble_rempro_cmd.h"
#include "bs300_ram_sync.h"
#include "bs300_storage.h"

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

/* Reassembly + response buffers */
#define REASM_BUF_SIZE  100
static uint8_t reasm_buf[REASM_BUF_SIZE];
static uint8_t reasm_len;

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
    if (len < 7) return NULL;
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
    uint8_t resp_data[2];
    resp_data[0] = 100;  /* Left_Battery: hardcoded 100% */
    resp_data[1] = 0;    /* Right_Battery: 0 (single device) */

    PRINTF("[REMPRO] GetBatteryInfo: L=100 R=0\r\n");
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
 * If active program, trigger I2C resync to DSP. */
static int fitting_commit(uint8_t prog_idx)
{
    if (bs300_struct_to_flash(&s_fit_buf, bs300_work_buf) < 0) return -1;
    bs300_storage_write_program(prog_idx, bs300_work_buf);

    if (prog_idx == bs300_get_active_prog()) {
        bs300_resync_diff_async(&s_fit_buf, NULL);
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

    bs300_print_settings();

    /* Load flash → struct */
    bs300_storage_load_program(prog, bs300_work_buf);
    if (bs300_flash_to_struct(bs300_work_buf, &s_fit_buf) < 0) {
        hdlc_response(CMD_SETGAIN, 1, NULL, 0); return;
    }

    for (i = 0; i < pairs; i++) {
        uint8_t spectrum = data[2 + i * 2];
        int16_t decibel  = data[3 + i * 2];
        if (spectrum < 32) {
            if (decibel > 100) decibel = 100;
            s_fit_buf.wdrc.bin_gain[spectrum] = (int8_t)decibel;
        }
    }

    PRINTF("[REMPRO] SetGain: dev=%u prog=%u pairs=%u\r\n", dev_type, prog, pairs);
    bs300_reset_user_params(prog);
    fitting_commit(prog);
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

    bs300_print_settings();

    bs300_storage_load_program(prog, bs300_work_buf);
    if (bs300_flash_to_struct(bs300_work_buf, &s_fit_buf) < 0) {
        hdlc_response(CMD_SETMPO, 1, NULL, 0); return;
    }

    for (i = 0; i < pairs; i++) {
        uint8_t channel = data[2 + i * 2];
        int16_t mpo_val = data[3 + i * 2];
        if (channel < 16) {
            if (mpo_val > 127) mpo_val = 127;
            s_fit_buf.wdrc.lmt_th_db[channel] = (int8_t)mpo_val;
        }
    }

    PRINTF("[REMPRO] SetMPO: dev=%u prog=%u pairs=%u\r\n", dev_type, prog, pairs);
    fitting_commit(prog);
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

    PRINTF("[REMPRO] SetCompressRatio: dev=%u prog=%u turn=%u pairs=%u\r\n",
           dev_type, prog, turn_num, pairs);
    fitting_commit(prog);
    hdlc_response(CMD_SETCOMPRESSRATIO, 0, NULL, 0);
}

/* Denoise level → ENR max_att_db mapping */
static const uint8_t denoise_to_max_att[5] = { 6, 9, 12, 15, 18 };

/* ID:9  SetDenoise */
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
    uint8_t i;

    if (prog >= 4 || level > 4) {
        hdlc_response(CMD_SETDENOISE, 1, NULL, 0); return;
    }

    bs300_print_settings();

    bs300_storage_load_program(prog, bs300_work_buf);
    if (bs300_flash_to_struct(bs300_work_buf, &s_fit_buf) < 0) {
        hdlc_response(CMD_SETDENOISE, 1, NULL, 0); return;
    }

    /* Map level → max_att_db, apply to all 16 channels */
    {
        uint8_t att = denoise_to_max_att[level];
        for (i = 0; i < 16; i++) {
            s_fit_buf.enr.max_att_db[i] = att;
        }
    }

    PRINTF("[REMPRO] SetDenoise: dev=%u prog=%u level=%u → att=%u\r\n",
           dev_type, prog, level, denoise_to_max_att[level]);
    bs300_set_prog_denoise(prog, level);
    fitting_commit(prog);
    hdlc_response(CMD_SETDENOISE, 0, NULL, 0);
}

/* ================================================================
 * Main dispatcher
 * ================================================================ */
void rempro_cmd_process(void)
{
    if (!rempro_env.role_value_changed) return;
    rempro_env.role_value_changed = 0;

    /* Append incoming chunk to reassembly buffer */
    uint8_t chunk_len = rempro_env.role_value_len;
    print_hex("RX chunk", rempro_env.role_value, chunk_len);
    if (reasm_len + chunk_len > REASM_BUF_SIZE) {
        PRINTF("[REMPRO] reasm overflow: %u + %u > %u\r\n",
               reasm_len, chunk_len, REASM_BUF_SIZE);
        reasm_len = 0;   /* reset, drop bad data */
        return;
    }
    memcpy(reasm_buf + reasm_len, rempro_env.role_value, chunk_len);
    reasm_len += chunk_len;

    /* Process as many complete frames as we can */
    while (reasm_len >= 7) {   /* minimum frame: 7E SY CMDL CMDH FCS 7E */

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
        uint8_t data_len, consumed;
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
        case CMD_GETCURRENTSCENE:
            cmd_getcurrentscene();
            break;
        case CMD_GETDEVICECONFIG:
            cmd_getdeviceconfig();
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
