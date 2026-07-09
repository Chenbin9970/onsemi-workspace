/**
 * BS300 Protocol Building Blocks + Startup Sequence
 *
 * Phase 1 (bs300_startup):  MUTE → KEY_LOCK → VERIFY_COMM
 * Phase 2 (init):           Calibration read → Global Profile read
 * Phase 3 (program_read):   READ_START → read 10 packets
 */

#include "bs300_startup.h"
#include "bs300_hal.h"
#include "app.h"
#include <string.h>

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

/* ============================================================
 * Internal Constants — frame payload lengths (addr excluded,
 * bs300_i2c_write sends addr via I2C hardware START).
 * ============================================================ */

#define SIMPLE_CMD_LEN      5     /* Len(0x00) + Cmd[3] + Chk */
#define ADV_WRITE_LEN       53    /* Len(0x10) + Cmd[3] + Data[48] + Chk */
#define READ_REQ_LEN        2     /* Len(0x80|0x90) + Chk */
#define STATUS_RESP_LEN     4     /* Cmd[3] + Chk */
#define DATA_RESP_LEN       52    /* Cmd[3] + Data[48] + Chk */


/* Default security code for VERIFY_COMM (little-endian) */
static const uint8_t security_code[3] = { 0x01, 0x29, 0x58 };

/* ============================================================
 * Checksum
 * ============================================================ */

static uint8_t calc_chk(const uint8_t *payload, uint8_t len)
{
    uint16_t sum = 0;
    for (uint8_t i = 0; i < len; i++) sum += payload[i];
    return (uint8_t)(0xFF - (sum & 0xFF));
}

/* ============================================================
 * I2C Frame Builders
 * ============================================================ */

/* Simple Command frame: Len(0x00) Cmd_L Cmd_M Cmd_H Chk
 * 5 bytes. Addr sent by I2C hardware START. */
static void build_simple_cmd(uint32_t cmd_word, uint8_t *frame)
{
    uint8_t payload[4];
    payload[0] = 0x00;
    payload[1] = (uint8_t)(cmd_word & 0xFF);
    payload[2] = (uint8_t)((cmd_word >> 8) & 0xFF);
    payload[3] = (uint8_t)((cmd_word >> 16) & 0xFF);

    frame[0] = payload[0];
    frame[1] = payload[1];
    frame[2] = payload[2];
    frame[3] = payload[3];
    frame[4] = calc_chk(payload, 4);
}

/* Read Request frame: Len(0x80|0x90) Chk
 * 2 bytes. len_data: 0x00 for status (→ read 4B), 0x10 for data (→ read 52B) */
static void build_read_req(uint8_t len_data, uint8_t *frame)
{
    uint8_t len_byte = 0x80 | len_data;
    frame[0] = len_byte;
    frame[1] = calc_chk(&len_byte, 1);
}

/* Advanced Write frame: Len(0x10) Cmd_L Cmd_M Cmd_H Data[48] Chk
 * 52 bytes. */
static void build_adv_write(uint32_t cmd_word, const uint8_t *data,
                             uint8_t *frame)
{
    uint8_t payload[52];
    payload[0] = 0x10;
    payload[1] = (uint8_t)(cmd_word & 0xFF);
    payload[2] = (uint8_t)((cmd_word >> 8) & 0xFF);
    payload[3] = (uint8_t)((cmd_word >> 16) & 0xFF);
    memcpy(payload + 4, data, 48);

    memcpy(frame, payload, 52);
    frame[52] = calc_chk(payload, 52);
}

/* ============================================================
 * FURPROC Polling
 * ============================================================ */

/* Single poll: send Read Request(0), read 4B status.
 * Returns true if FURPROC=0 and checksum valid.
 * Returns false if I2C error, checksum mismatch, or FURPROC still set. */
static bool poll_once(uint32_t expected_cmd_h)
{
    uint8_t frame[READ_REQ_LEN];
    uint8_t resp[STATUS_RESP_LEN];

    build_read_req(0x00, frame);
    if (!bs300_i2c_write(BS300_I2C_ADDR, frame, READ_REQ_LEN))
        return false;
    bs300_delay_ms(1);
    if (!bs300_i2c_read(BS300_I2C_ADDR, resp, STATUS_RESP_LEN))
        return false;

    /* Verify checksum */
    if (calc_chk(resp, 3) != resp[3]) return false;

    /* FURPROC=0 → ready. Also check Cmd_H matches (masking bit23). */
    uint32_t cmd = resp[0] | ((uint32_t)resp[1] << 8) | ((uint32_t)resp[2] << 16);
    if ((cmd >> 23) & 1) return false;            /* FURPROC=1 → still busy */
    if (resp[2] != (expected_cmd_h & 0x7F)) return false;  /* Cmd_H mismatch */

    return true;
}

/* ============================================================
 * Internal: send Simple Cmd + wait_ms + poll, with retry
 * ============================================================ */

static bool _send_simple_cmd(uint32_t cmd_word, uint32_t wait_ms)
{
    uint8_t frame[SIMPLE_CMD_LEN + 1];
    build_simple_cmd(cmd_word, frame);
    uint8_t cmd_h = (uint8_t)((cmd_word >> 16) & 0xFF);

    PRINTF("[BS300] I2C TX[%u] CMD=0x%06lX:", SIMPLE_CMD_LEN, cmd_word);
    for (uint8_t i = 0; i < SIMPLE_CMD_LEN; i++)
        PRINTF(" %02X", frame[i]);
    PRINTF("\r\n");

    for (uint8_t retry = 0; retry < 10; retry++) {
        if (retry) bs300_delay_ms(25);
        if (!bs300_i2c_write(BS300_I2C_ADDR, frame, SIMPLE_CMD_LEN))
            continue;
        bs300_delay_ms(wait_ms);
        if (poll_once(cmd_h)) return true;
    }
    return false;
}

/* ============================================================
 * Protocol Building Blocks (public)
 * ============================================================ */

bool bs300_send_simple_cmd(uint32_t cmd_word)
{
    return _send_simple_cmd(cmd_word, 85);
}

bool bs300_advanced_write(uint32_t cmd_word, const uint8_t *data)
{
    uint8_t frame[ADV_WRITE_LEN + 1];
    build_adv_write(cmd_word, data, frame);
    uint8_t cmd_h = (uint8_t)((cmd_word >> 16) & 0xFF);

    PRINTF("[BS300] I2C TX[%u] CMD=0x%06lX:", ADV_WRITE_LEN, cmd_word);
    for (uint8_t i = 0; i < ADV_WRITE_LEN; i++)
        PRINTF(" %02X", frame[i]);
    PRINTF("\r\n");

    for (uint8_t retry = 0; retry < 10; retry++) {
        if (retry) bs300_delay_ms(25);

        if (!bs300_i2c_write(BS300_I2C_ADDR, frame, ADV_WRITE_LEN))
            continue;
        bs300_delay_ms(60);  /* Adv Write → 60ms wait */
        if (poll_once(cmd_h)) return true;
    }
    return false;
}

bool bs300_read_packet(uint32_t prepare_cmd, uint8_t *data_out)
{
    uint8_t frame[READ_REQ_LEN];
    uint8_t resp[DATA_RESP_LEN];

    /* Step 1: Send prepare command (60ms wait for read-type commands) */
    if (!_send_simple_cmd(prepare_cmd, 60))
        return false;

    /* Step 2: Wait 60ms after ready */
    bs300_delay_ms(60);

    /* Step 3: Read Request (len=0x10) → I2C Read 52 bytes */
    build_read_req(0x10, frame);

    PRINTF("[BS300] I2C TX[%u] READ_REQ:", READ_REQ_LEN);
    for (uint8_t i = 0; i < READ_REQ_LEN; i++)
        PRINTF(" %02X", frame[i]);
    PRINTF("\r\n");

    if (!bs300_i2c_write(BS300_I2C_ADDR, frame, READ_REQ_LEN))
        return false;
    bs300_delay_ms(1);    /* BS300 needs 1ms after Read Request before responding */
    if (!bs300_i2c_read(BS300_I2C_ADDR, resp, DATA_RESP_LEN))
        return false;

    PRINTF("[BS300] I2C RX[%u]:", DATA_RESP_LEN);
    for (uint8_t i = 0; i < DATA_RESP_LEN; i++)
        PRINTF(" %02X", resp[i]);
    PRINTF("\r\n");

    /* Step 4: Verify checksum over Cmd[3] + Data[48] */
    uint8_t chk_buf[51];
    memcpy(chk_buf, resp, 3);
    memcpy(chk_buf + 3, resp + 3, 48);
    if (calc_chk(chk_buf, 51) != resp[51])
        return false;

    /* Step 5: Extract 48-byte payload */
    if (data_out)
        memcpy(data_out, resp + 3, 48);

    return true;
}

/* ============================================================
 * High-level Sequences
 * ============================================================ */

bool bs300_startup(void)
{
    uint8_t verify_data[48];

    /* ① MUTE — stop DSP, enter configurable state */
    if (!bs300_send_simple_cmd(0x800000))
        return false;
    bs300_delay_ms(2);

    /* ② KEY_LOCK — lock physical keys */
    if (!bs300_send_simple_cmd(0x801020))
        return false;
    bs300_delay_ms(2);

    /* ③ VERIFY_COMM — unlock Flash access with security code */
    memset(verify_data, 0, sizeof(verify_data));
    memcpy(verify_data, security_code, 3);
    if (!bs300_advanced_write(0x800030, verify_data))
        return false;

    return true;
}

bool bs300_read_calibration(uint8_t *calib_out)
{
    if (!bs300_read_packet(0x800051, calib_out + 0 * 48))
        return false;
    bs300_delay_ms(2);

    if (!bs300_read_packet(0x801051, calib_out + 1 * 48))
        return false;
    bs300_delay_ms(2);

    if (!bs300_read_packet(0x802051, calib_out + 2 * 48))
        return false;

    return true;
}

bool bs300_read_global_profile(uint8_t *profile_out)
{
    bs300_delay_ms(10);
    return bs300_read_packet(0x800071, profile_out);
}
