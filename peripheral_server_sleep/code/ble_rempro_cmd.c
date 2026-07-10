#include "app.h"
#include "ble_rempro.h"
#include "ble_rempro_cmd.h"
#include "bs300_ram_sync.h"

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
