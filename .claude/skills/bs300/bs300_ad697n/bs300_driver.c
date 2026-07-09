#include "bs300_driver.h"
#include "system/includes.h"
#include "syscfg_id.h"

extern void clr_wdt(void);

/* delay(1) ≈ 0.46μs (measured: BS300_MS(60) = 68ms), so delay(2188) ≈ 1ms */
#define BS300_MS(ms)       delay((unsigned int)((ms) * 2188UL))
#define BS300_BUF_DELAY()  delay(500)  /* 200μs buffer between Read Request and I2C Read */

/* ================================================================
 *  Internal state + boot-time caches (populated once, used by
 *  input-switch to avoid VM reads during tone playback).
 * ================================================================ */
static u8 s_active_prog;
static bs300_calib_t           s_calib_cache;
static u8                      s_prog_input[4];
static bs300_modules_t         s_prog_modules[4];
static bs300_enr_t             s_prog_enr[4];
static u8                      s_boot_cached;

u8 bs300_get_prog_input(u8 prog_idx)
{
    if (prog_idx < 4) return s_prog_input[prog_idx];
    return 0;
}

void bs300_cache_prog_inputs(void)
{
    bs300_prog_struct_t prog;
    u8 calib_raw[144];
    u8 i;
    for (i = 0; i < 4; i++) {
        if (bs300_vm_load_struct(i, &prog) == 0) {
            s_prog_input[i]   = prog.modules.input_selection;
            s_prog_modules[i] = prog.modules;
            s_prog_enr[i]     = prog.enr;
        }
    }
    if (bs300_calib_vm_load(calib_raw) == 0) {
        bs300_parse_calibration(calib_raw, &s_calib_cache);
        s_boot_cached = 1;
    }
}

/* Called by bs300_switch_program_start after VM active-prog is written */
void bs300_on_active_prog_changed(u8 new_prog_idx)
{
    bs300_prog_struct_t prog;
    s_active_prog = new_prog_idx;
    s_prog_input[new_prog_idx] = 0;  /* will be refreshed below */
    if (bs300_vm_load_struct(new_prog_idx, &prog) == 0) {
        s_prog_input[new_prog_idx]   = prog.modules.input_selection;
        s_prog_modules[new_prog_idx] = prog.modules;
        s_prog_enr[new_prog_idx]     = prog.enr;
    }
}

/* ================================================================
 *  I2C transport helpers (internal)
 * ================================================================ */

/**
 * @brief  Write frame to BS300 via I2C.
 * @return bytes sent on success, -1 on error
 */
static int bs300_i2c_write_frame(soft_iic_dev iic, const u8 *frame, int len)
{
    u8 ack;
    int sent;

    if (frame == NULL || len <= 0) {
        return -1;
    }
    bs300_debug("i2c wr(%d): ", len);
    put_buf(frame, len);
    soft_iic_start(iic);
    ack = soft_iic_tx_byte(iic, BS300_I2C_ADDR);
    if (ack == 0) {
        bs300_debug("bs300: i2c write nack on addr\n");
        soft_iic_stop(iic);
        return -1;
    }
    sent = soft_iic_write_buf(iic, frame, len);
    soft_iic_stop(iic);
    clr_wdt();
    if (sent != len) {
        bs300_debug("bs300: i2c write fail, sent=%d expected=%d\n", sent, len);
        return -1;
    }
    return sent;
}

/**
 * @brief  Read bytes from BS300 via I2C.
 * @return bytes read on success, -1 on error
 */
static int bs300_i2c_read_bytes(soft_iic_dev iic, u8 *buf, int len)
{
    u8 ack;
    int recv;

    if (buf == NULL || len <= 0) {
        return -1;
    }
    soft_iic_start(iic);
    ack = soft_iic_tx_byte(iic, BS300_I2C_ADDR | 0x01);
    if (ack == 0) {
        bs300_debug("bs300: i2c read nack on addr\n");
        soft_iic_stop(iic);
        return -1;
    }
    recv = soft_iic_read_buf(iic, buf, len);
    soft_iic_stop(iic);
    clr_wdt();
    bs300_debug("i2c rd(%d): ", recv);
    put_buf(buf, recv);
    if (recv != len) {
        bs300_debug("bs300: i2c read fail, recv=%d expected=%d\n", recv, len);
        return -1;
    }
    return recv;
}

/**
 * @brief  Calculate BS300 frame checksum.
 * @param  buf   Frame data (after slave address)
 * @param  len   Frame length in bytes
 * @return checksum byte
 */
static u8 bs300_calc_checksum(const u8 *buf, int len)
{
    u16 sum = 0;
    int i;

    for (i = 0; i < len; i++) {
        sum += buf[i];
    }
    return (u8)(0xFF - (sum & 0xFF));
}

/* ================================================================
 *  Frame building & sending (internal)
 * ================================================================ */

/**
 * @brief  Send a Simple Command and poll until device ready.
 * @return 0 on success, -1 on error, -2 on timeout
 */
static int bs300_send_simple_cmd(soft_iic_dev iic, u32 cmd)
{
    u8 frame[5];
    u8 chk;
    int ret;

    frame[0] = BS300_LEN_NO_DATA;
    frame[1] = (u8)(cmd & 0xFF);
    frame[2] = (u8)((cmd >> 8) & 0xFF);
    frame[3] = (u8)((cmd >> 16) & 0xFF);
    chk = bs300_calc_checksum(frame, 4);
    frame[4] = chk;

    ret = bs300_i2c_write_frame(iic, frame, 5);
    if (ret < 0) {
        return -1;
    }
    return 0;
}

/**
 * @brief  Send an Advanced Write command with 48-byte data.
 * @return 0 on success, -1 on error
 */
static int bs300_send_advanced_write(soft_iic_dev iic, u32 cmd, const u8 *data)
{
    u8 frame[53];
    u8 chk;
    int i;
    int ret;

    if (data == NULL) {
        return -1;
    }
    frame[0] = BS300_LEN_HAS_DATA;
    frame[1] = (u8)(cmd & 0xFF);
    frame[2] = (u8)((cmd >> 8) & 0xFF);
    frame[3] = (u8)((cmd >> 16) & 0xFF);
    for (i = 0; i < BS300_DATA_SIZE; i++) {
        frame[4 + i] = data[i];
    }
    chk = bs300_calc_checksum(frame, 52);
    frame[52] = chk;

    ret = bs300_i2c_write_frame(iic, frame, 53);
    if (ret < 0) {
        return -1;
    }
    return 0;
}

/**
 * @brief  Poll device status via Read Request (len=0x00).
 *         Checks FURPROC bit (bit23) of returned command word.
 * @return 0 = ready, 1 = busy, -1 = I2C error
 */
static int bs300_poll_ready(soft_iic_dev iic)
{
    u8 req_frame[2];
    u8 resp[4];
    u8 chk;
    int ret;

    req_frame[0] = BS300_LEN_NO_DATA | BS300_LEN_RD_REQ_FLAG;
    req_frame[1] = (u8)(0xFF - req_frame[0]);
    ret = bs300_i2c_write_frame(iic, req_frame, 2);
    if (ret < 0) {
        return -1;
    }
    BS300_BUF_DELAY();
    ret = bs300_i2c_read_bytes(iic, resp, 4);
    if (ret < 0) {
        return -1;
    }
    chk = (u8)(0xFF - ((resp[0] + resp[1] + resp[2]) & 0xFF));
    if (chk != resp[3]) {
        bs300_debug("bs300: poll_ready chk fail exp=0x%02X got=0x%02X\n", chk, resp[3]);
        return -1;
    }
    if (resp[2] & 0x80) {
        return 1;
    }
    return 0;
}

/**
 * @brief  Wait for device to become ready (poll with timeout).
 * @return 0 = ready, -2 = timeout, -1 = I2C error
 */
static int bs300_wait_ready(soft_iic_dev iic, int timeout_ms)
{
    int try_count = 0;
    int max_tries = timeout_ms / BS300_CMD_DELAY_MS;
    int status;

    do {
        BS300_MS(BS300_CMD_DELAY_MS);
        status = bs300_poll_ready(iic);
        if (status < 0) {
            return -1;
        }
        if (status == 0) {
            return 0;
        }
        try_count++;
    } while (try_count < max_tries);
    bs300_debug("bs300: wait_ready timeout\n");
    return -2;
}

/**
 * @brief  Full write flow: send Simple/Advanced command then wait ready.
 *         Retries up to 30 times on I2C failure.
 * @param  type  0=Simple, 1=Advanced
 * @return 0 on success, negative on error
 */
static int bs300_write_and_wait(soft_iic_dev iic, u32 cmd, const u8 *data, int type)
{
    int ret;
    int attempt;

    for (attempt = 0; attempt < 30; attempt++) {
        if (type == 0) {
            ret = bs300_send_simple_cmd(iic, cmd);
        } else {
            ret = bs300_send_advanced_write(iic, cmd, data);
        }
        if (ret == 0) {
            ret = bs300_wait_ready(iic, BS300_POLL_TIMEOUT_MS);
            if (ret == 0) {
                return 0;
            }
            bs300_debug("bs300: write cmd=0x%06x wait fail ret=%d attempt=%d\n", cmd, ret, attempt);
        } else {
            bs300_debug("bs300: write cmd=0x%06x send fail attempt=%d\n", cmd, attempt);
        }
        if (attempt < 29) {
            BS300_MS(100);
        }
    }
    return -1;
}

/**
 * @brief  Read data packet from BS300.
 *         Sends prepare command, polls ready, then reads 52 bytes.
 * @param  prepare_cmd  Simple Command to prepare data
 * @param  data_out     48-byte output buffer
 * @return 0 on success, negative on error
 */
static int bs300_read_packet(soft_iic_dev iic, u32 prepare_cmd, u8 *data_out)
{
    u8 req_frame[2];
    u8 resp[52];
    int ret;
    int i;
    int attempt;

    if (data_out == NULL) {
        return -1;
    }
    for (attempt = 0; attempt < 30; attempt++) {
        ret = bs300_send_simple_cmd(iic, prepare_cmd);
        if (ret < 0) {
            bs300_debug("bs300: read_pkt prepare fail attempt=%d\n", attempt);
            goto retry_wait;
        }
        ret = bs300_wait_ready(iic, BS300_POLL_TIMEOUT_MS);
        if (ret < 0) {
            bs300_debug("bs300: read_pkt wait fail ret=%d attempt=%d\n", ret, attempt);
            goto retry_wait;
        }
        req_frame[0] = BS300_LEN_HAS_DATA | BS300_LEN_RD_REQ_FLAG;
        req_frame[1] = (u8)(0xFF - req_frame[0]);
        ret = bs300_i2c_write_frame(iic, req_frame, 2);
        if (ret < 0) {
            bs300_debug("bs300: read_pkt req fail attempt=%d\n", attempt);
            goto retry_wait;
        }
        BS300_BUF_DELAY();
        ret = bs300_i2c_read_bytes(iic, resp, 52);
        if (ret < 0) {
            bs300_debug("bs300: read_pkt i2c read fail attempt=%d\n", attempt);
            goto retry_wait;
        }
        if (bs300_calc_checksum(resp, 51) != resp[51]) {
            bs300_debug("bs300: read_pkt chk fail attempt=%d\n", attempt);
            goto retry_wait;
        }
        for (i = 0; i < BS300_DATA_SIZE; i++) {
            data_out[i] = resp[3 + i];
        }
        return 0;
retry_wait:
        if (attempt < 29) {
            BS300_MS(100);
        }
    }
    return -1;
}

/**
 * @brief  Send Verify Communication Code (Advanced Write).
 *         Security code (0x582901) placed at data[5-7], rest zero-filled.
 * @return 0 on success, negative on error
 */
static int bs300_send_verify_comm(soft_iic_dev iic, u32 security_code)
{
    u8 data[BS300_DATA_SIZE];
    int i;

    for (i = 0; i < BS300_DATA_SIZE; i++) {
        data[i] = 0x00;
    }
    data[0] = (u8)((security_code >> 16) & 0xFF);
    data[1] = (u8)((security_code >> 8) & 0xFF);
    data[2] = (u8)(security_code & 0xFF);
    return bs300_write_and_wait(iic, BS300_CMD_VERIFY_COMM, data, 1);
}

/**
 * @brief  Read calibration data (3 packets × 48B = 144B).
 * @param  buf  Output buffer (BS300_CALIB_SIZE bytes)
 * @return 0 on success, negative on error
 */
static int bs300_calib_read(soft_iic_dev iic, u8 *buf)
{
    int pkt;
    int ret;

    for (pkt = 0; pkt < BS300_CALIB_PKT_COUNT; pkt++) {
        u32 cmd = BS300_CMD_CALIB_READ_BASE + ((u32)pkt << 12);
        if (pkt > 0) {
            BS300_MS(2);
        }
        ret = bs300_read_packet(iic, cmd, buf + pkt * BS300_DATA_SIZE);
        if (ret < 0) {
            bs300_debug("bs300: calib_read pkt=%d fail ret=%d\n", pkt, ret);
            return ret;
        }
    }
    return 0;
}

/* ================================================================
 *  Program Burn — Read / Write
 * ================================================================ */

int bs300_program_read(soft_iic_dev iic, u8 prog_idx, u8 *buf)
{
    u32 read_start_cmd;
    u32 read_cmd;
    int ret;
    int pkt;
    u8 *buf_pos;

    if (prog_idx >= BS300_PROG_COUNT || buf == NULL) {
        return -1;
    }
    bs300_debug("bs300: program_read Y=%d\n", prog_idx);
    read_start_cmd = BS300_CMD_READ_START_BASE | ((u32)prog_idx << 12);
    ret = bs300_write_and_wait(iic, read_start_cmd, NULL, 0);
    if (ret < 0) {
        bs300_debug("bs300: read_start fail ret=%d\n", ret);
        return ret;
    }
    for (pkt = 0; pkt < BS300_PROGRAM_PKTS; pkt++) {
        if (pkt > 0) {
            BS300_MS(2);
        }
        read_cmd = BS300_CMD_PROG_READ_BASE + ((u32)pkt << 12);
        buf_pos = buf + (pkt * BS300_DATA_SIZE);
        ret = bs300_read_packet(iic, read_cmd, buf_pos);
        if (ret < 0) {
            bs300_debug("bs300: read pkt=%d fail ret=%d\n", pkt, ret);
            return ret;
        }
    }
    bs300_debug("bs300: program_read done\n");
    return 0;
}

int bs300_program_write(soft_iic_dev iic, u8 prog_idx, const u8 *buf)
{
    u32 write_cmd;
    u32 burn_end_cmd;
    int ret;
    int pkt;

    if (prog_idx >= BS300_PROG_COUNT || buf == NULL) {
        return -1;
    }
    bs300_debug("bs300: program_write Y=%d\n", prog_idx);
    for (pkt = 0; pkt < BS300_PROGRAM_PKTS; pkt++) {
        if (pkt > 0) {
            BS300_MS(2);
        }
        write_cmd = BS300_CMD_PROG_WRITE_BASE + ((u32)pkt << 12);
        ret = bs300_write_and_wait(iic, write_cmd,
                                   buf + (pkt * BS300_DATA_SIZE), 1);
        if (ret < 0) {
            bs300_debug("bs300: write pkt=%d fail ret=%d\n", pkt, ret);
            return ret;
        }
    }
    BS300_MS(2);
    burn_end_cmd = BS300_CMD_BURN_END_BASE | ((u32)prog_idx << 12);
    ret = bs300_write_and_wait(iic, burn_end_cmd, NULL, 0);
    if (ret < 0) {
        bs300_debug("bs300: burn_end fail ret=%d\n", ret);
        return ret;
    }
    bs300_debug("bs300: program_write done\n");
    return 0;
}

/* ================================================================
 *  Param I2C — Single packet write
 * ================================================================ */

int bs300_param_write_packet(soft_iic_dev iic, u32 cmd, const u8 *data)
{
    int ret;

    if (data == NULL) {
        return -1;
    }
    ret = bs300_write_and_wait(iic, cmd, data, 1);
    if (ret < 0) {
        bs300_debug("bs300: param_write cmd=0x%06x fail ret=%d\n", cmd, ret);
    }
    return ret;
}

/* ================================================================
 *  Module command → Param I2C base command mapping (internal)
 * ================================================================ */

static u32 bs300_cmd_data_to_base(u8 cmd_data)
{
    switch (cmd_data) {
    case 0x12: return 0x8000B2; /* WDRC */
    case 0x07: return 0x800081; /* Volume & Beep */
    case 0x03: return 0x800062; /* Front Mic */
    case 0x04: return 0x800062; /* Rear Mic */
    case 0x05: return 0x804272; /* Telecoil */
    case 0x06: return 0x800022; /* DAI */
    case 0x17: return 0x800062; /* MM Plus */
    case 0x1B: return 0x800062; /* DDM2 */
    case 0x1E: return 0x800062; /* Dual Mic */
    case 0x14: return 0x800052; /* DFBC */
    case 0x1C: return 0x8000C2; /* ENR */
    case 0x21: return 0x800172; /* Noise Gen2 */
    case 0x1D: return 0x8001B2; /* ISS */
    case 0x1F: return 0x8001C2; /* WNR */
    case 0x26: return 0x8022A2; /* WDRC Acclimatization */
    case 0x23: return 0x800382; /* AGCO */
    default:
        bs300_debug("bs300: unknown cmd_data=0x%02x\n", cmd_data);
        return 0;
    }
}

/**
 * @brief  Sync one module's data to BS300 via Param I2C.
 *         Splits module data into 48-byte packets, sends each.
 *
 * @param  iic         I2C device
 * @param  base_cmd    Param I2C base command for this module
 * @param  data        Pointer to module data in buffer
 * @param  word_count  Number of 24-bit words in this module
 * @return 0 on success, negative on error
 */
static int bs300_sync_module(soft_iic_dev iic, u32 base_cmd,
                             const u8 *data, u8 word_count)
{
    u8 packet[BS300_DATA_SIZE];
    u16 total_bytes;
    u8 pkt_count;
    u8 pkt_num;
    u32 cmd;
    int offset;
    int copy_len;
    int i;
    int ret;

    if (data == NULL || base_cmd == 0 || word_count == 0) {
        return -1;
    }
    total_bytes = (u16)word_count * 3;
    pkt_count = (u8)((total_bytes + BS300_DATA_SIZE - 1) / BS300_DATA_SIZE);
    offset = 0;

    for (pkt_num = 0; pkt_num < pkt_count; pkt_num++) {
        if (pkt_num > 0) {
            BS300_MS(2);
        }
        copy_len = total_bytes - offset;
        if (copy_len > BS300_DATA_SIZE) {
            copy_len = BS300_DATA_SIZE;
        }
        for (i = 0; i < copy_len; i++) {
            packet[i] = data[offset + i];
        }
        for (i = copy_len; i < BS300_DATA_SIZE; i++) {
            packet[i] = 0x00;
        }
        cmd = base_cmd + ((u32)pkt_num << BS300_CMD_PKTNUM_SHIFT);
        ret = bs300_param_write_packet(iic, cmd, packet);
        if (ret < 0) {
            bs300_debug("bs300: sync_module pkt=%d fail\n", pkt_num);
            return ret;
        }
        offset += BS300_DATA_SIZE;
    }
    return 0;
}

/* ================================================================
 *  Sync All — Parse buffer and sync all modules (public)
 * ================================================================ */

int bs300_sync_all(soft_iic_dev iic, const u8 *buf)
{
    u8 module_count;
    u8 cmd_data;
    u8 data_length_words;
    u32 base_cmd;
    u8 data_offset;
    const u8 *module_data;
    int i;
    int ret;

    if (buf == NULL) {
        return -1;
    }
    if (buf[1] != 0x80 || buf[2] != 0x00) {
        bs300_debug("bs300: sync_all invalid header\n");
        return -1;
    }
    module_count = buf[3] - 1;
    if (module_count == 0 || module_count > 16) {
        bs300_debug("bs300: sync_all bad module_count=%d\n", module_count);
        return -1;
    }
    bs300_debug("bs300: sync_all modules=%d\n", module_count);
    data_offset = 6 + module_count * 3;

    for (i = 0; i < module_count; i++) {
        cmd_data = buf[4 + i * 3];
        data_length_words = buf[4 + i * 3 + 2];
        if (data_length_words == 0) {
            continue;
        }
        base_cmd = bs300_cmd_data_to_base(cmd_data);
        if (base_cmd == 0) {
            bs300_debug("bs300: skip unknown module cmd=0x%02x\n", cmd_data);
            continue;
        }
        module_data = buf + data_offset;
        ret = bs300_sync_module(iic, base_cmd, module_data, data_length_words);
        if (ret < 0) {
            return ret;
        }
        data_offset += data_length_words * 3;
    }
    bs300_debug("bs300: sync_all done\n");
    return 0;
}

/* ================================================================
 *  High-level API (public)
 * ================================================================ */

int bs300_startup(soft_iic_dev iic, u32 security_code)
{
    int ret;

    bs300_debug("bs300: startup\n");
    soft_iic_init(iic);
    clr_wdt();
    delay_2ms(800);
    clr_wdt();

    soft_iic_set_delay(iic, 200);
    ret = bs300_write_and_wait(iic, BS300_CMD_MUTE, NULL, 0);
    soft_iic_reset_delay(iic);
    if (ret < 0) {
        bs300_debug("bs300: startup mute fail ret=%d\n", ret);
        return ret;
    }
    BS300_MS(2);
    ret = bs300_write_and_wait(iic, BS300_CMD_KEY_LOCK, NULL, 0);
    if (ret < 0) {
        bs300_debug("bs300: startup key_lock fail ret=%d\n", ret);
        return ret;
    }
    BS300_MS(2);
    ret = bs300_send_verify_comm(iic, security_code);
    if (ret < 0) {
        bs300_debug("bs300: startup verify comm fail ret=%d\n", ret);
        return ret;
    }
    bs300_debug("bs300: startup done\n");
    return 0;
}

int bs300_init(soft_iic_dev iic, u8 prog_idx)
{
    u8 gp_buf[BS300_DATA_SIZE];
    u8 calib_buf[BS300_CALIB_SIZE];
    u8 *buf;
    bs300_prog_struct_t *prog_struct;
    u8 active_prog;
    u8 loaded_from_vm;
    int ret;

    if (prog_idx >= BS300_PROG_COUNT) {
        return -1;
    }
    bs300_debug("bs300: init prog=%d\n", prog_idx);

    if (prog_idx < 3) {
        ret = bs300_calib_vm_load(calib_buf);
        if (ret == 0) {
            bs300_debug("bs300: init calib from VM\n");
        } else {
            ret = bs300_calib_read(iic, calib_buf);
            if (ret < 0) {
                bs300_debug("bs300: init calib read fail ret=%d\n", ret);
                return ret;
            }
            bs300_calib_vm_save(calib_buf);
        }
    }
    BS300_MS(10);
    ret = bs300_read_packet(iic, BS300_CMD_GLOBAL_PROFILE, gp_buf);
    if (ret < 0) {
        bs300_debug("bs300: init global profile fail ret=%d\n", ret);
        return ret;
    }
    buf = (u8 *)malloc(BS300_PROGRAM_SIZE);
    if (buf == NULL) {
        bs300_debug("bs300: init malloc fail\n");
        return -1;
    }
    prog_struct = (bs300_prog_struct_t *)malloc(sizeof(bs300_prog_struct_t));
    if (prog_struct == NULL) {
        bs300_debug("bs300: init struct malloc fail\n");
        free(buf);
        return -1;
    }

    /* Read VM struct version; force Flash re-read on version mismatch */
    ret = bs300_vm_read_active_prog(&active_prog);
    if (ret == -2) {
        bs300_debug("bs300: init VM version mismatch, re-reading Flash\n");
        active_prog = 0;
    }
    if (ret < 0) {
        active_prog = 0;
    }
    if (prog_idx == 0) {
        bs300_vm_write_active_prog(active_prog);
    }

    /* Try VM struct first, fall back to Flash decode */
    loaded_from_vm = 0;
    ret = bs300_vm_load_struct(prog_idx, prog_struct);
    if (ret == 0 && prog_struct->wdrc.total_channels > 0
        && prog_struct->wdrc.total_channels <= 16) {
        bs300_debug("bs300: loaded struct from VM\n");
        loaded_from_vm = 1;
    } else {
        bs300_debug("bs300: VM struct empty/invalid, reading Flash\n");
        ret = bs300_program_read(iic, prog_idx, buf);
        if (ret < 0) {
            bs300_debug("bs300: program_read fail ret=%d\n", ret);
            free(prog_struct);
            free(buf);
            return ret;
        }
        ret = bs300_flash_to_struct(buf, prog_struct);
        if (ret < 0) {
            bs300_debug("bs300: flash_to_struct fail ret=%d\n", ret);
            free(prog_struct);
            free(buf);
            return ret;
        }
        bs300_vm_save_struct(prog_idx, prog_struct);
    }

    /* Program 0: force input to FrontMic (Flash may store non-zero for mute) */
    if (prog_idx == 0 && prog_struct->modules.input_selection != 0) {
        bs300_debug("bs300: p=0 input %d→0(FrontMic)\n",
                    prog_struct->modules.input_selection);
        prog_struct->modules.input_selection = 0;
        bs300_vm_save_struct(prog_idx, prog_struct);
    }

    /* Print decoded struct for verification (from VM or Flash) */
    {
        u8 nch, ench, i;
        nch = prog_struct->wdrc.total_channels;
        if (nch > 16) nch = 16;
        ench = prog_struct->enr.enable_num_ch & 0x3F;
        if (ench > 16) ench = 16;

        bs300_debug("bs300: ====== struct p=%d ======\n", prog_idx);

        /* ---- WDRC ---- */
        bs300_debug("bs300: [WDRC] total_ch=%d nsbc=%d kp_mode=%d lim=%d\n",
                    nch, prog_struct->wdrc.nsbc,
                    prog_struct->wdrc.kp_mode, prog_struct->wdrc.limiter);
        bs300_debug("bs300: [WDRC] bin_gain[0..31]: ");
        put_buf((u8 *)prog_struct->wdrc.bin_gain, 32);
        for (i = 0; i < nch; i++) {
            bs300_debug("bs300: [WDRC ch%02d]"
                        " f=%3d"
                        " epd(at=%3d rt=%3d r=%3d)"
                        " kp1(th=%4d at=%3d rt=%3d r=%3d)"
                        " kp2(th=%4d at=%3d rt=%3d r=%3d)"
                        " lmt(th=%4d at=%3d rt=%3d r=%3d)\n",
                        i,
                        prog_struct->wdrc.freq_idx[i],
                        prog_struct->wdrc.epd_at_idx[i],
                        prog_struct->wdrc.epd_rt_idx[i],
                        prog_struct->wdrc.epd_r_idx[i],
                        prog_struct->wdrc.kp1_th_db[i],
                        prog_struct->wdrc.kp1_at_idx[i],
                        prog_struct->wdrc.kp1_rt_idx[i],
                        prog_struct->wdrc.kp1_r_idx[i],
                        prog_struct->wdrc.kp2_th_db[i],
                        prog_struct->wdrc.kp2_at_idx[i],
                        prog_struct->wdrc.kp2_rt_idx[i],
                        prog_struct->wdrc.kp2_r_idx[i],
                        prog_struct->wdrc.lmt_th_db[i],
                        prog_struct->wdrc.lmt_at_idx[i],
                        prog_struct->wdrc.lmt_rt_idx[i],
                        prog_struct->wdrc.lmt_r_idx[i]);
        }

        /* ---- ENR ---- */
        bs300_debug("bs300: [ENR] en=0x%02x ch=%d"
                    " nfsf=%d nhsf=%d nnsf=%d snasf=%d\n",
                    prog_struct->enr.enable_num_ch, ench,
                    prog_struct->enr.nfsf, prog_struct->enr.nhsf,
                    prog_struct->enr.nnsf, prog_struct->enr.snasf);
        bs300_debug("bs300: [ENR] freq_idx[0..%d]: ", ench);
        put_buf(prog_struct->enr.freq_idx, ench);
        bs300_debug("bs300: [ENR] snr_th_db[0..%d]: ", ench);
        put_buf(prog_struct->enr.snr_th_db, ench);
        bs300_debug("bs300: [ENR] max_att_db[0..%d]: ", ench);
        put_buf(prog_struct->enr.max_att_db, ench);
        bs300_debug("bs300: [ENR] noise_th_db[0..%d]: ", ench);
        put_buf(prog_struct->enr.noise_th_db, ench);
        bs300_debug("bs300: [ENR] upper_noise_th_db[0..%d]: ", ench);
        put_buf(prog_struct->enr.upper_noise_th_db, ench);
        bs300_debug("bs300: [ENR] etr_x100[0..%d]: ", ench);
        put_buf(prog_struct->enr.etr_x100, ench);
        bs300_debug("bs300: [ENR] nrr_x10[0..%d]: ", ench);
        put_buf(prog_struct->enr.nrr_x10, ench);
        bs300_debug("bs300: [ENR] sasf[0..%d]: ", ench);
        put_buf(prog_struct->enr.sasf, ench);

        /* ---- Modules ---- */
        bs300_debug("bs300: [Mod] vol_en=%d beep=%d/%d"
                    " min_vol=%d max_vol=%d in_sel=%d"
                    " batt_beep=%d/%d\n",
                    prog_struct->modules.vol_enable,
                    prog_struct->modules.beep_level,
                    prog_struct->modules.beep_freq_idx,
                    prog_struct->modules.min_vol,
                    prog_struct->modules.max_vol,
                    prog_struct->modules.input_selection,
                    prog_struct->modules.batt_beep_level,
                    prog_struct->modules.batt_beep_freq_idx);
        bs300_debug("bs300: [Mod] dfbc=%d iss(en=%d thr=%d)"
                    " wnr(en_dual=%d preset=%d)"
                    " agco(en=%d thr=%d atk=%d rel=%d)\n",
                    prog_struct->modules.dfbc_enable_mode,
                    prog_struct->modules.iss_enable,
                    prog_struct->modules.iss_threshold,
                    prog_struct->modules.wnr_enable_dual,
                    prog_struct->modules.wnr_preset,
                    prog_struct->modules.agco_enable,
                    prog_struct->modules.agco_threshold_db,
                    prog_struct->modules.agco_attack_01ms,
                    prog_struct->modules.agco_release_01ms);
        bs300_debug("bs300: [Mod] mm_plus(en=%d mix=%d)"
                    " ddm2(en=%d open=%d polar=%d adm_fdm=%d)\n",
                    prog_struct->modules.mm_plus_enable,
                    prog_struct->modules.mix_ratio,
                    prog_struct->modules.ddm2_enable,
                    prog_struct->modules.open_ear,
                    prog_struct->modules.polar_pattern,
                    prog_struct->modules.adm_fdm);
        bs300_debug("bs300: [Mod] rt: vol=%d eq=[%d,%d,%d]\n",
                    prog_struct->modules.volume_level,
                    prog_struct->modules.eq_low,
                    prog_struct->modules.eq_mid,
                    prog_struct->modules.eq_high);
    }

    /* Sync deferred — caller syncs after all programs are loaded */

    /* Checksum validation only when Flash data was actually read */
    if (prog_idx < 3 && !loaded_from_vm) {
        u32 chk, accumulated;
        chk = bs300_checksum32(buf, BS300_PROGRAM_SIZE);
        if (prog_idx == 0) {
            accumulated = chk;
        } else {
            syscfg_read(BS300_VM_ID_PROG_CHK_BASE, (u8 *)&accumulated, 4);
            accumulated += chk;
        }
        syscfg_write(BS300_VM_ID_PROG_CHK_BASE, (u8 *)&accumulated, 4);
        bs300_debug("bs300: init p=%d chk=%u acc=%u\n",
                    prog_idx, (unsigned)chk, (unsigned)accumulated);
        if (prog_idx == 2 && accumulated != BS300_CALIB_CHECKSUM) {
            u8 zero[BS300_VM_SEG_SIZE];
            int p, zi;
            bs300_debug("bs300: prog chk fail, clearing VM struct\n");
            for (zi = 0; zi < BS300_VM_SEG_SIZE; zi++) {
                zero[zi] = 0x00;
            }
            for (p = 0; p < 3; p++) {
                bs300_vm_save_seg(p, 0, zero, BS300_VM_SEG_SIZE);
                bs300_vm_save_seg(p, 1, zero, BS300_VM_SEG_SIZE);
            }
            accumulated = 0;
            syscfg_write(BS300_VM_ID_PROG_CHK_BASE, (u8 *)&accumulated, 4);
        }
    }

    free(prog_struct);
    free(buf);
    s_active_prog = prog_idx;
    return 0;
}

/* bs300_param_modify implemented in bs300_param.c (shares switch_diff helpers) */
/* ================================================================
 *  Voice prompt input switch (no igd chain, no VM change)
 * ================================================================ */

u8 bs300_voice_prompt_input_switch(soft_iic_dev iic, u8 target_input)
{
    u8 data[48];
    u8 vb_data[48];
    bs300_modules_t mod_tmp;
    u8 original_input;
    int ret;

    if (!s_boot_cached) return 0xFF;

    original_input = s_prog_input[s_active_prog];
    if (original_input == target_input) return original_input;

    /* Encode Vol/Beep with new input using cached modules + calib */
    mod_tmp = s_prog_modules[s_active_prog];
    mod_tmp.input_selection = target_input;
    ret = bs300_encode_volume_beep(&mod_tmp, &s_calib_cache, vb_data);
    if (ret < 0) return 0xFF;

    ret = bs300_mute(iic);
    if (ret < 0) return 0xFF;

    /* Disable DDM2/MM+ if original input uses them (HW order: DDM2 → MM+) */
    if (original_input == 5) {
        memset(data, 0, 48);
        bs300_param_write_packet(iic, 0x800022, data);
    }
    if (original_input == 4) {
        memset(data, 0, 48);
        bs300_param_write_packet(iic, 0x800062, data);
    }

    /* Disable ENR (0x8000C2): first byte=0 disables all ENR processing */
    memset(data, 0, 48);
    bs300_param_write_packet(iic, 0x8000C2, data);

    ret = bs300_param_write_packet(iic, 0x800081, vb_data);
    if (ret < 0) return 0xFF;

    ret = bs300_active(iic);
    if (ret < 0) return 0xFF;

    soft_iic_set_delay(iic, 500);  // restore default (2x slower)

    return original_input;
}

int bs300_voice_prompt_input_restore(soft_iic_dev iic, u8 original_input)
{
    u8 data[48];
    u8 vb_data[48];
    bs300_modules_t mod_tmp;
    int ret;

    if (!s_boot_cached) return -1;

    /* Encode Vol/Beep with original input using cached modules + calib */
    mod_tmp = s_prog_modules[s_active_prog];
    mod_tmp.input_selection = original_input;
    ret = bs300_encode_volume_beep(&mod_tmp, &s_calib_cache, vb_data);
    if (ret < 0) return -1;

    ret = bs300_mute(iic);
    if (ret < 0) return -1;

    /* Re-enable DDM2/MM+ if original input uses them (HW order: DDM2 → MM+) */
    if (original_input == 5) {
        bs300_encode_ddm2(&mod_tmp, &s_calib_cache, data);
        bs300_param_write_packet(iic, 0x800022, data);
    }
    if (original_input == 4) {
        bs300_encode_mm_plus(&mod_tmp, &s_calib_cache, 0, data);
        bs300_param_write_packet(iic, 0x800062, data);
    }

    /* Re-enable ENR from boot cached data */
    bs300_encode_enr_general(&s_prog_enr[s_active_prog], data);
    bs300_param_write_packet(iic, 0x8000C2, data);

    ret = bs300_param_write_packet(iic, 0x800081, vb_data);
    if (ret < 0) return -1;

    ret = bs300_active(iic);
    if (ret < 0) return -1;

    soft_iic_set_delay(iic, 500);  // restore default (2x slower)

    return 0;
}

/* ================================================================
 *  Runtime Volume / EQ control
 * ================================================================ */

int bs300_reencode_bin_gain(soft_iic_dev iic)
{
    u8 data[48];
    u8 calib_raw[BS300_CALIB_SIZE];
    bs300_calib_t calib;
    bs300_prog_struct_t prog;
    u8 active_prog;
    u8 input_type;
    int ret;

    ret = bs300_vm_read_active_prog(&active_prog);
    if (ret < 0) return -1;

    ret = bs300_vm_load_struct(active_prog, &prog);
    if (ret < 0) return -1;

    ret = bs300_calib_vm_load(calib_raw);
    if (ret < 0) return -1;
    ret = bs300_parse_calibration(calib_raw, &calib);
    if (ret < 0) return -1;

    input_type = (prog.modules.input_selection == 2) ? 1 :
                 (prog.modules.input_selection == 3) ? 2 : 0;

    ret = bs300_encode_wdrc_bin_gain(&prog.wdrc, &calib, &prog.modules,
                                      input_type, data);
    if (ret < 0) return -1;

    return bs300_param_write_packet(iic, 0x8060B2, data);
}

/* Public: reload active program from VM and resync all I2C RAM commands.
 * Used after VM modifications to the active program (volume/EQ/ENR/etc). */
int bs300_resync_active(soft_iic_dev iic)
{
    bs300_prog_struct_t prog;
    u8 ap;
    extern int bs300_vm_read_active_prog(u8 *prog_idx);
    extern int bs300_vm_load_struct(u8 prog_idx, bs300_prog_struct_t *out);
    extern int bs300_sync_program(soft_iic_dev iic, bs300_prog_struct_t *prog);

    if (bs300_vm_read_active_prog(&ap) < 0) return -1;
    if (bs300_vm_load_struct(ap, &prog) < 0) return -1;
    return bs300_sync_program(iic, &prog);
}

int bs300_set_volume(soft_iic_dev iic, u8 level)
{
    bs300_prog_struct_t prog;
    u8 active_prog;
    int ret;

    if (level > 9) return -1;

    ret = bs300_vm_read_active_prog(&active_prog);
    if (ret < 0) return -1;

    ret = bs300_vm_load_struct(active_prog, &prog);
    if (ret < 0) return -1;

    prog.modules.volume_level = level;
    ret = bs300_vm_save_struct(active_prog, &prog);
    if (ret < 0) return -1;

    return bs300_reencode_bin_gain(iic);
}

int bs300_set_eq(soft_iic_dev iic, s8 low_gain, s8 mid_gain, s8 high_gain)
{
    bs300_prog_struct_t prog;
    u8 active_prog;
    int ret;

    if (low_gain < -12 || low_gain > 12) return -1;
    if (mid_gain < -12 || mid_gain > 12) return -1;
    if (high_gain < -12 || high_gain > 12) return -1;

    ret = bs300_vm_read_active_prog(&active_prog);
    if (ret < 0) return -1;

    ret = bs300_vm_load_struct(active_prog, &prog);
    if (ret < 0) return -1;

    prog.modules.eq_low  = low_gain;
    prog.modules.eq_mid  = mid_gain;
    prog.modules.eq_high = high_gain;
    ret = bs300_vm_save_struct(active_prog, &prog);
    if (ret < 0) return -1;

    return bs300_reencode_bin_gain(iic);
}

int bs300_is_connected(soft_iic_dev iic)
{
    int ret;

    ret = bs300_send_simple_cmd(iic, BS300_CMD_IS_CONNECT);
    if (ret < 0) {
        return -1;
    }
    ret = bs300_wait_ready(iic, 500);
    if (ret < 0) {
        return -1;
    }
    return 0;
}

/* ================================================================
 *  Non-blocking sync session
 * ================================================================ */

void bs300_sync_session_init(bs300_sync_session_t *s, soft_iic_dev iic)
{
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
    s->iic = iic;
    s->state = BS300_SYNC_IDLE;
}

int bs300_session_append(bs300_sync_session_t *s, u32 cmd, const u8 *data)
{
    if (s == NULL || data == NULL) return -1;
    if (s->cmd_count >= BS300_SYNC_MAX_CMDS) return -1;

    s->cmds[s->cmd_count] = cmd;
    memcpy(s->datas[s->cmd_count], data, 48);
    s->cmd_count++;
    return 0;
}

int bs300_sync_tick(bs300_sync_session_t *s)
{
    int ret;
    u32 now;

    if (s == NULL) return 0;

    switch (s->state) {
    case BS300_SYNC_IDLE:
    case BS300_SYNC_DONE:
    case BS300_SYNC_ERROR:
        return 0;

    case BS300_SYNC_SEND:
        if (s->cmd_index >= s->cmd_count) {
            s->state = BS300_SYNC_DONE;
            return 0;
        }

        ret = bs300_send_advanced_write(s->iic,
                                         s->cmds[s->cmd_index],
                                         s->datas[s->cmd_index]);
        clr_wdt();
        if (ret < 0) {
            s->retry_count++;
            if (s->retry_count >= 30) {
                s->fail_count++;
                s->state = BS300_SYNC_ERROR;
                bs300_debug("bs300: sync send fail cmd=0x%06X\n",
                            (unsigned int)s->cmds[s->cmd_index]);
                return 0;
            }
            return 1;
        }

        s->last_action_ms = sys_timer_get_ms();
        s->retry_count = 0;
        s->state = BS300_SYNC_POLL;
        return 1;

    case BS300_SYNC_POLL:
        now = sys_timer_get_ms();

        if (now - s->last_action_ms < (u32)BS300_CMD_DELAY_MS) {
            return 1;
        }

        ret = bs300_poll_ready(s->iic);
        clr_wdt();
        if (ret < 0) {
            s->retry_count++;
            if (s->retry_count >= 30) {
                s->fail_count++;
                s->state = BS300_SYNC_ERROR;
                bs300_debug("bs300: sync poll err cmd=0x%06X\n",
                            (unsigned int)s->cmds[s->cmd_index]);
                return 0;
            }
            s->state = BS300_SYNC_SEND;
            return 1;
        }
        if (ret == 0) {
            s->cmd_index++;
            s->retry_count = 0;

            if (s->cmd_index >= s->cmd_count) {
                s->state = BS300_SYNC_DONE;
                return 0;
            }
            s->state = BS300_SYNC_SEND;
            return 1;
        }

        if (now - s->last_action_ms > (u32)BS300_POLL_TIMEOUT_MS) {
            s->retry_count++;
            if (s->retry_count >= 30) {
                s->fail_count++;
                s->state = BS300_SYNC_ERROR;
                bs300_debug("bs300: sync poll timeout cmd=0x%06X\n",
                            (unsigned int)s->cmds[s->cmd_index]);
                return 0;
            }
            s->state = BS300_SYNC_SEND;
        }
        return 1;
    }

    return 0;
}

/* ================================================================
 *  Async API — single static session + auto-rescheduling callback
 * ================================================================ */

static void bs300_sync_done(void);
static void bs300_switch_done(void);

static bs300_sync_session_t g_bs300_sync;
static u16 g_bs300_timeout_id;
static void (*g_bs300_sync_on_done)(void) = NULL;

static void bs300_sync_tick_cb(void *ctx)
{
    u32 elapsed, delay;

    sys_timeout_del(g_bs300_timeout_id);
    g_bs300_timeout_id = 0;

    if (!bs300_sync_tick(&g_bs300_sync)) {
        if (g_bs300_sync.state == BS300_SYNC_DONE
            || g_bs300_sync.state == BS300_SYNC_ERROR) {
            if (g_bs300_sync_on_done) {
                void (*cb)(void) = g_bs300_sync_on_done;
                g_bs300_sync_on_done = NULL;
                cb();
            }
        }
        return;
    }

    elapsed = sys_timer_get_ms() - g_bs300_sync.last_action_ms;

    if (g_bs300_sync.state == BS300_SYNC_POLL) {
        /* Wait exactly until 60ms after the command was sent */
        delay = (elapsed < (u32)BS300_CMD_DELAY_MS)
                ? (u32)BS300_CMD_DELAY_MS - elapsed
                : (u32)BS300_CMD_DELAY_MS;
    } else {
        /* SEND: fire ASAP to send next command */
        delay = 2;
    }

    g_bs300_timeout_id = sys_timeout_add(NULL, bs300_sync_tick_cb, delay);
}

int bs300_sync_is_busy(void)
{
    return (g_bs300_sync.state != BS300_SYNC_IDLE
            && g_bs300_sync.state != BS300_SYNC_DONE
            && g_bs300_sync.state != BS300_SYNC_ERROR);
}

int bs300_switch_program_async(u8 new_prog_idx)
{
    if (g_bs300_sync.state != BS300_SYNC_IDLE
        && g_bs300_sync.state != BS300_SYNC_DONE
        && g_bs300_sync.state != BS300_SYNC_ERROR) {
        return -1;
    }
    if (g_bs300_timeout_id) {
        sys_timeout_del(g_bs300_timeout_id);
        g_bs300_timeout_id = 0;
    }

    g_bs300_sync_on_done = bs300_switch_done;

    bs300_sync_session_init(&g_bs300_sync, 0);
    if (bs300_switch_program_start(&g_bs300_sync, new_prog_idx) < 0) {
        g_bs300_sync_on_done = NULL;
        return -1;
    }

    g_bs300_timeout_id = sys_timeout_add(NULL, bs300_sync_tick_cb,
                                          BS300_CMD_DELAY_MS);
    return 0;
}

int bs300_resync_diff_async(bs300_prog_struct_t *_new, void (*on_done)(void))
{
    if (g_bs300_sync.state != BS300_SYNC_IDLE
        && g_bs300_sync.state != BS300_SYNC_DONE
        && g_bs300_sync.state != BS300_SYNC_ERROR) {
        return -1;
    }
    if (g_bs300_timeout_id) {
        sys_timeout_del(g_bs300_timeout_id);
        g_bs300_timeout_id = 0;
    }
    g_bs300_sync_on_done = on_done;

    bs300_sync_session_init(&g_bs300_sync, 0);
    if (bs300_resync_diff_start(&g_bs300_sync, _new) < 0) {
        g_bs300_sync_on_done = NULL;
        return -1;
    }

    g_bs300_timeout_id = sys_timeout_add(NULL, bs300_sync_tick_cb,
                                          BS300_CMD_DELAY_MS);
    return 0;
}

int bs300_param_modify_async(u8 prog_idx, u16 offset, const u8 *val, u8 len)
{
    if (g_bs300_sync.state != BS300_SYNC_IDLE
        && g_bs300_sync.state != BS300_SYNC_DONE
        && g_bs300_sync.state != BS300_SYNC_ERROR) {
        return -1;
    }
    if (g_bs300_timeout_id) {
        sys_timeout_del(g_bs300_timeout_id);
        g_bs300_timeout_id = 0;
    }

    bs300_sync_session_init(&g_bs300_sync, 0);
    if (bs300_param_modify_start(&g_bs300_sync, prog_idx, offset, val, len) < 0) {
        return -1;
    }

    g_bs300_timeout_id = sys_timeout_add(NULL, bs300_sync_tick_cb,
                                          BS300_CMD_DELAY_MS);
    return 0;
}

/* ================================================================
 *  Async single-command wrappers (volume/EQ)
 * ================================================================ */

static void (*g_reencode_retry_on_done)(void) = NULL;

static void bs300_reencode_bin_gain_retry(void *unused)
{
    g_bs300_timeout_id = 0;
    bs300_reencode_bin_gain_async(g_reencode_retry_on_done);
}

int bs300_reencode_bin_gain_async(void (*on_done)(void))
{
    u8 data[48];
    u8 calib_raw[BS300_CALIB_SIZE];
    bs300_calib_t calib;
    bs300_prog_struct_t prog;
    u8 active_prog, input_type;
    int ret;

    if (bs300_sync_is_busy()) {
        g_reencode_retry_on_done = on_done;
        if (g_bs300_timeout_id) sys_timeout_del(g_bs300_timeout_id);
        g_bs300_timeout_id = sys_timeout_add(NULL,
            bs300_reencode_bin_gain_retry, 50);
        return 0;
    }

    ret = bs300_vm_read_active_prog(&active_prog);
    if (ret < 0) return -1;
    ret = bs300_vm_load_struct(active_prog, &prog);
    if (ret < 0) return -1;
    clr_wdt();
    ret = bs300_calib_vm_load(calib_raw);
    if (ret < 0) return -1;
    clr_wdt();
    ret = bs300_parse_calibration(calib_raw, &calib);
    if (ret < 0) return -1;

    input_type = (prog.modules.input_selection == 2) ? 1 :
                 (prog.modules.input_selection == 3) ? 2 : 0;

    ret = bs300_encode_wdrc_bin_gain(&prog.wdrc, &calib, &prog.modules,
                                      input_type, data);
    if (ret < 0) return -1;

    bs300_sync_session_init(&g_bs300_sync, 0);
    g_bs300_sync_on_done = on_done;
    bs300_session_append(&g_bs300_sync, 0x8060B2, data);
    g_bs300_sync.state = BS300_SYNC_SEND;  // must set after append
    g_bs300_timeout_id = sys_timeout_add(NULL, bs300_sync_tick_cb,
                                          BS300_CMD_DELAY_MS);
    return 0;
}

/* Encode bin_gain from in-memory struct (no VM read), queue to async session */
static int bs300_reencode_from_struct(bs300_prog_struct_t *prog, void (*on_done)(void))
{
    u8 data[48];
    u8 calib_raw[BS300_CALIB_SIZE];
    bs300_calib_t calib;
    u8 input_type;
    int ret;

    ret = bs300_calib_vm_load(calib_raw);
    if (ret < 0) return -1;
    ret = bs300_parse_calibration(calib_raw, &calib);
    if (ret < 0) return -1;

    input_type = (prog->modules.input_selection == 2) ? 1 :
                 (prog->modules.input_selection == 3) ? 2 : 0;

    ret = bs300_encode_wdrc_bin_gain(&prog->wdrc, &calib, &prog->modules,
                                      input_type, data);
    if (ret < 0) return -1;

    bs300_sync_session_init(&g_bs300_sync, 0);
    g_bs300_sync_on_done = on_done;
    bs300_session_append(&g_bs300_sync, 0x8060B2, data);
    g_bs300_sync.state = BS300_SYNC_SEND;
    g_bs300_timeout_id = sys_timeout_add(NULL, bs300_sync_tick_cb,
                                          BS300_CMD_DELAY_MS);
    return 0;
}

int bs300_set_volume_async(u8 level, void (*on_done)(void))
{
    bs300_prog_struct_t prog;
    u8 active_prog;
    int ret;

    if (level > 9) return -1;
    if (bs300_sync_is_busy()) return -1;

    ret = bs300_vm_read_active_prog(&active_prog);
    if (ret < 0) return -1;
    ret = bs300_vm_load_struct(active_prog, &prog);
    if (ret < 0) return -1;
    clr_wdt();

    prog.modules.volume_level = level;
    ret = bs300_vm_save_struct(active_prog, &prog);
    if (ret < 0) return -1;

    return bs300_reencode_from_struct(&prog, on_done);
}

int bs300_set_eq_async(s8 low, s8 mid, s8 high, void (*on_done)(void))
{
    bs300_prog_struct_t prog;
    u8 active_prog;
    int ret;

    if (bs300_sync_is_busy()) return -1;

    ret = bs300_vm_read_active_prog(&active_prog);
    if (ret < 0) return -1;
    ret = bs300_vm_load_struct(active_prog, &prog);
    if (ret < 0) return -1;
    clr_wdt();

    prog.modules.eq_low  = low;
    prog.modules.eq_mid  = mid;
    prog.modules.eq_high = high;
    ret = bs300_vm_save_struct(active_prog, &prog);
    if (ret < 0) return -1;

    return bs300_reencode_from_struct(&prog, on_done);
}

/* ================================================================
 *  Clean sync: all RAM ops → save VM → bs300_sync_dirty → diff → tick
 * ================================================================ */

static u8 s_need_sync = 0;

static int bs300_vol_save_vm(u8 level)
{
    static bs300_prog_struct_t s_prog;
    u8 ap; int ret;
    if (level > 9) return -1;
    ret = bs300_vm_read_active_prog(&ap);
    if (ret < 0) return -1;
    ret = bs300_vm_load_struct(ap, &s_prog);
    if (ret < 0) return -1;
    clr_wdt();
    s_prog.modules.volume_level = level;
    return bs300_vm_save_struct(ap, &s_prog);
}

static void bs300_kick_sync(void)
{
    static bs300_prog_struct_t s_prog;
    u8 ap;
    if (bs300_sync_is_busy()) return;
    if (bs300_vm_read_active_prog(&ap) < 0) return;
    if (bs300_vm_load_struct(ap, &s_prog) < 0) return;
    s_need_sync = 0;
    bs300_reencode_from_struct(&s_prog, bs300_sync_done);
}

void bs300_sync_dirty(void)
{
    s_need_sync = 1;
    bs300_kick_sync();
}

static void bs300_sync_done(void)
{
    if (s_need_sync) {
        bs300_kick_sync();
    }
    extern void bs300_sync_chain(void);
    bs300_sync_chain();
}

static void bs300_switch_done(void)
{
    bs300_active(0);
    soft_iic_set_delay(0, 500);  // restore default after mute→active
    bs300_sync_done();
}

int bs300_vol_commit(u8 level)
{
    int ret = bs300_vol_save_vm(level);
    if (ret < 0) return ret;
    bs300_sync_dirty();
    return 0;
}

int bs300_mute(soft_iic_dev iic)
{
    int ret, n;
    u8 req_frame[2];
    u8 resp[4];
    u8 chk;
    u32 echoed;
    extern void usage_track_pause(void);
    usage_track_pause();
    soft_iic_set_delay(iic, 250);  // mute uses normal speed

    req_frame[0] = BS300_LEN_NO_DATA | BS300_LEN_RD_REQ_FLAG;
    req_frame[1] = (u8)(0xFF - req_frame[0]);

    for (n = 0; n < 50; n++) {
        ret = bs300_write_and_wait(iic, BS300_CMD_MUTE, NULL, 0);
        if (ret != 0) {
            delay(2188UL);  /* 1ms */
            continue;
        }
        ret = bs300_i2c_write_frame(iic, req_frame, 2);
        if (ret < 0) {
            bs300_debug("bs300: mute vrfy req fail attempt=%d\n", n);
            delay(2188UL);
            continue;
        }
        BS300_BUF_DELAY();
        ret = bs300_i2c_read_bytes(iic, resp, 4);
        if (ret < 0) {
            bs300_debug("bs300: mute vrfy read fail attempt=%d\n", n);
            delay(2188UL);
            continue;
        }
        chk = (u8)(0xFF - ((resp[0] + resp[1] + resp[2]) & 0xFF));
        if (chk != resp[3]) {
            bs300_debug("bs300: mute vrfy chk fail attempt=%d exp=0x%02X got=0x%02X\n",
                n, chk, resp[3]);
            ret = -1;
            delay(2188UL);
            continue;
        }
        if (resp[2] & 0x80) {
            bs300_debug("bs300: mute vrfy busy attempt=%d resp=%02X%02X%02X\n",
                n, resp[0], resp[1], resp[2]);
            ret = -1;
            delay(2188UL);
            continue;
        }
        echoed = resp[0] | ((u32)resp[1] << 8) | ((u32)(resp[2] & 0x7F) << 16);
        if (echoed != (BS300_CMD_MUTE & 0x7FFFFF)) {
            bs300_debug("bs300: mute vrfy cmd mismatch attempt=%d echoed=0x%06X\n",
                n, (unsigned int)echoed);
            ret = -1;
            delay(2188UL);
            continue;
        }
        ret = 0;
        break;
    }
    soft_iic_reset_delay(iic);  // stay at 250, caller restores
    if (n > 0) printf("[BS300] mute retry=%d ret=%d\n", n, ret);
    return ret;
}

int bs300_key_lock(soft_iic_dev iic)
{
    return bs300_write_and_wait(iic, BS300_CMD_KEY_LOCK, NULL, 0);
}

int bs300_verify_comm(soft_iic_dev iic, u32 security_code)
{
    u8 data[BS300_DATA_SIZE];
    int i;
    for (i = 0; i < BS300_DATA_SIZE; i++) data[i] = 0;
    data[0] = (u8)((security_code >> 16) & 0xFF);
    data[1] = (u8)((security_code >> 8) & 0xFF);
    data[2] = (u8)(security_code & 0xFF);
    return bs300_write_and_wait(iic, BS300_CMD_VERIFY_COMM, data, 1);
}

int bs300_unlock(soft_iic_dev iic)
{
    return bs300_write_and_wait(iic, BS300_CMD_UNLOCK, NULL, 0);
}

int bs300_active(soft_iic_dev iic)
{
    int ret;
    ret = bs300_send_simple_cmd(iic, BS300_CMD_ACTIVE);
    if (ret == 0) {
        extern void usage_track_active(void);
        usage_track_active();
    }
    return ret;
}
