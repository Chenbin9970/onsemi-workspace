#include "bs300_ram_sync.h"
#include "bs300_startup.h"
#include "bs300_storage.h"
#include "bs300_hal.h"
#include "bs300_encode_tables.h"
#include "app.h"
#include <rsl10.h>
#include <string.h>

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

/* ================================================================
 *  Single source of truth for current DSP state
 * ================================================================ */
static bs300_prog_struct_t  s_dsp_state;      /* 490B — always == DSP register state */
static bs300_prog_struct_t  s_target;         /* 490B — target program loaded during switch */
static bs300_calib_t        s_calib_cache;    /* ~80B — calibration, shared by all programs */
static uint8_t              s_volumes[4];     /* 4B — per-program volume, persists across switches */
static int8_t               s_eq_low[4];      /* 4B — per-program EQ low */
static int8_t               s_eq_mid[4];      /* 4B — per-program EQ mid */
static int8_t               s_eq_high[4];     /* 4B — per-program EQ high */
static uint8_t              s_denoise[4];     /* 4B — per-program denoise level 0-4 */
static uint8_t              s_cur_prog;       /* 1B — current active program index */
static bool                 s_boot_cached;    /* 1B — calibration loaded flag */

/* Shared raw Flash work buffer — also used by driver.c */
uint8_t bs300_work_buf[480];

/* ================================================================
 *  Per-command DSP state update: copy fields from target → dsp_state
 *  based on which I2C command just succeeded.
 * ================================================================ */
static void dsp_state_apply(uint32_t cmd, const bs300_prog_struct_t *src,
                            bs300_prog_struct_t *dst)
{
    switch (cmd & 0xFFFFFF) {

    /* ---- DDM2 (0x800022) ---- */
    case 0x800022:
        dst->modules.ddm2_enable    = src->modules.ddm2_enable;
        dst->modules.open_ear       = src->modules.open_ear;
        dst->modules.polar_pattern  = src->modules.polar_pattern;
        dst->modules.adm_fdm        = src->modules.adm_fdm;
        dst->modules.omni_threshold = src->modules.omni_threshold;
        break;

    /* ---- MM Plus (0x800062) ---- */
    case 0x800062:
        dst->modules.mm_plus_enable = src->modules.mm_plus_enable;
        dst->modules.mix_ratio      = src->modules.mix_ratio;
        break;

    /* ---- DFBC (0x800052) ---- */
    case 0x800052:
        dst->modules.dfbc_enable_mode = src->modules.dfbc_enable_mode;
        break;

    /* ---- ENR General (0x8000C2) ---- */
    case 0x8000C2:
        dst->enr.enable_num_ch = src->enr.enable_num_ch;
        break;

    /* ---- ENR Freq Spacing (0x8010C2) ---- */
    case 0x8010C2:
        memcpy(dst->enr.freq_idx, src->enr.freq_idx, 16);
        break;

    /* ---- ENR SNR Threshold (0x8020C2) ---- */
    case 0x8020C2:
        memcpy(dst->enr.snr_th_db, src->enr.snr_th_db, 16);
        break;

    /* ---- ENR Max Att (0x8030C2) ---- */
    case 0x8030C2:
        memcpy(dst->enr.max_att_db, src->enr.max_att_db, 16);
        break;

    /* ---- ENR Noise Threshold (0x8040C2) ---- */
    case 0x8040C2:
        memcpy(dst->enr.noise_th_db, src->enr.noise_th_db, 16);
        break;

    /* ---- ENR Upper Noise Threshold (0x8050C2) ---- */
    case 0x8050C2:
        memcpy(dst->enr.upper_noise_th_db, src->enr.upper_noise_th_db, 16);
        break;

    /* ---- ENR Smoothing (0x8060C2) ---- */
    case 0x8060C2:
        dst->enr.nfsf  = src->enr.nfsf;
        dst->enr.nhsf  = src->enr.nhsf;
        dst->enr.nnsf  = src->enr.nnsf;
        dst->enr.snasf = src->enr.snasf;
        break;

    /* ---- ENR ETR (0x8070C2) ---- */
    case 0x8070C2:
        memcpy(dst->enr.etr_x100, src->enr.etr_x100, 16);
        break;

    /* ---- ENR NRR (0x8080C2) ---- */
    case 0x8080C2:
        memcpy(dst->enr.nrr_x10, src->enr.nrr_x10, 16);
        break;

    /* ---- TC/DAI — no struct fields, skip ---- */

    /* ---- ISS (0x8001B2) ---- */
    case 0x8001B2:
        dst->modules.iss_enable    = src->modules.iss_enable;
        dst->modules.iss_threshold = src->modules.iss_threshold;
        break;

    /* ---- WNR Setup (0x8001C2) ---- */
    case 0x8001C2:
        dst->modules.wnr_enable_dual = src->modules.wnr_enable_dual;
        dst->modules.wnr_preset      = src->modules.wnr_preset;
        break;

    /* WNR Band/Single-Mic commands have no struct fields — skip 0x8011C2, 0x8411C2, 0x8021C2 */

    /* ---- AGCO (0x800382) ---- */
    case 0x800382:
        dst->modules.agco_enable        = src->modules.agco_enable;
        dst->modules.agco_threshold_db  = src->modules.agco_threshold_db;
        dst->modules.agco_attack_01ms   = src->modules.agco_attack_01ms;
        dst->modules.agco_release_01ms  = src->modules.agco_release_01ms;
        break;

    /* ---- Vol/Beep (0x800081) ---- */
    case 0x800081:
        dst->modules.vol_enable       = src->modules.vol_enable;
        dst->modules.beep_level       = src->modules.beep_level;
        dst->modules.beep_freq_idx    = src->modules.beep_freq_idx;
        dst->modules.min_vol          = src->modules.min_vol;
        dst->modules.max_vol          = src->modules.max_vol;
        dst->modules.input_selection  = src->modules.input_selection;
        dst->modules.batt_beep_level  = src->modules.batt_beep_level;
        dst->modules.batt_beep_freq_idx = src->modules.batt_beep_freq_idx;
        break;

    /* ---- WDRC General (0x8000B2) ---- */
    case 0x8000B2:
        dst->wdrc.total_channels = src->wdrc.total_channels;
        dst->wdrc.nsbc           = src->wdrc.nsbc;
        dst->wdrc.kp_mode        = src->wdrc.kp_mode;
        dst->wdrc.limiter        = src->wdrc.limiter;
        break;

    /* ---- WDRC Freq Spacing (0x8010B2) ---- */
    case 0x8010B2:
        memcpy(dst->wdrc.freq_idx, src->wdrc.freq_idx, 16);
        break;

    /* ---- WDRC KP Threshold (0x8020B2) ---- */
    case 0x8020B2:
        memcpy(dst->wdrc.kp1_th_db, src->wdrc.kp1_th_db, 16);
        memcpy(dst->wdrc.kp2_th_db, src->wdrc.kp2_th_db, 16);
        break;

    /* ---- WDRC Attack (0x8030B2) ---- */
    case 0x8030B2:
        memcpy(dst->wdrc.epd_at_idx, src->wdrc.epd_at_idx, 16);
        memcpy(dst->wdrc.kp1_at_idx, src->wdrc.kp1_at_idx, 16);
        memcpy(dst->wdrc.kp2_at_idx, src->wdrc.kp2_at_idx, 16);
        break;

    /* ---- WDRC Release (0x8040B2) ---- */
    case 0x8040B2:
        memcpy(dst->wdrc.epd_rt_idx, src->wdrc.epd_rt_idx, 16);
        memcpy(dst->wdrc.kp1_rt_idx, src->wdrc.kp1_rt_idx, 16);
        memcpy(dst->wdrc.kp2_rt_idx, src->wdrc.kp2_rt_idx, 16);
        break;

    /* ---- WDRC Ratio (0x8050B2) ---- */
    case 0x8050B2:
        memcpy(dst->wdrc.epd_r_idx,  src->wdrc.epd_r_idx,  16);
        memcpy(dst->wdrc.kp1_r_idx,  src->wdrc.kp1_r_idx,  16);
        memcpy(dst->wdrc.kp2_r_idx,  src->wdrc.kp2_r_idx,  16);
        break;

    /* ---- WDRC Bin Gain (0x8060B2) — also covers volume/EQ ---- */
    case 0x8060B2:
        memcpy(dst->wdrc.bin_gain, src->wdrc.bin_gain, 32);
        dst->modules.volume_level = src->modules.volume_level;
        dst->modules.eq_low       = src->modules.eq_low;
        dst->modules.eq_mid       = src->modules.eq_mid;
        dst->modules.eq_high      = src->modules.eq_high;
        break;

    /* ---- WDRC Lmt Threshold (0x8070B2) ---- */
    case 0x8070B2:
        memcpy(dst->wdrc.lmt_th_db, src->wdrc.lmt_th_db, 16);
        break;

    /* ---- WDRC Lmt Attack (0x8080B2) ---- */
    case 0x8080B2:
        memcpy(dst->wdrc.lmt_at_idx, src->wdrc.lmt_at_idx, 16);
        break;

    /* ---- WDRC Lmt Release (0x8090B2) ---- */
    case 0x8090B2:
        memcpy(dst->wdrc.lmt_rt_idx, src->wdrc.lmt_rt_idx, 16);
        break;

    /* ---- WDRC Lmt Ratio (0x80A0B2) ---- */
    case 0x80A0B2:
        memcpy(dst->wdrc.lmt_r_idx, src->wdrc.lmt_r_idx, 16);
        break;

    default:
        break;
    }
}

/* ================================================================
 *  Prompt Tone command words + helper
 * ================================================================ */

#define BS300_TONE_MODE_0    0xFD52F2
#define BS300_TONE_MODE_1    0xFD72F2
#define BS300_TONE_MODE_2    0xFD92F2
#define BS300_TONE_MODE_3    0xFDB2F2
#define BS300_TONE_VOL_0     0xFD12F2
#define BS300_TONE_VOL_OTHER 0xFCD2F2

static uint32_t bs300_tone_for_program(uint8_t program)
{
    switch (program) {
    case 0: return BS300_TONE_MODE_0;
    case 1: return BS300_TONE_MODE_1;
    case 2: return BS300_TONE_MODE_2;
    case 3: return BS300_TONE_MODE_3;
    default: return 0;
    }
}

/* ================================================================
 *  Public query API
 * ================================================================ */
uint8_t bs300_get_active_prog(void) { return s_cur_prog; }
bool bs300_is_boot_cached(void) { return s_boot_cached; }
bs300_prog_struct_t *bs300_get_dsp_state(void) { return &s_dsp_state; }

uint8_t bs300_get_module_volume(uint8_t prog_idx)
{
    if (prog_idx > 3) return 9;
    return s_volumes[prog_idx];
}

void bs300_set_prog_volume(uint8_t prog_idx, uint8_t level)
{
    if (prog_idx < 4 && level <= 9)
        s_volumes[prog_idx] = level;
}

void bs300_set_prog_denoise(uint8_t prog_idx, uint8_t level)
{
    if (prog_idx < 4 && level <= 4)
        s_denoise[prog_idx] = level;
}

void bs300_persist_active_prog(uint8_t prog)
{
    bs300_settings_save(prog, s_volumes, s_eq_low, s_eq_mid, s_eq_high, s_denoise);
}

const bs300_calib_t *bs300_get_cached_calib(void)
{
    return s_boot_cached ? &s_calib_cache : NULL;
}

/* ================================================================
 *  Settings restore (power-loss recovery)
 * ================================================================ */
void bs300_restore_settings(uint8_t active_prog, const uint8_t *volume,
                            const int8_t *eq_low, const int8_t *eq_mid,
                            const int8_t *eq_high, const uint8_t *denoise)
{
    uint8_t i;
    s_cur_prog = active_prog;
    if (volume != NULL) {
        for (i = 0; i < 4; i++) s_volumes[i] = volume[i];
    }
    if (eq_low != NULL) {
        for (i = 0; i < 4; i++) s_eq_low[i] = eq_low[i];
    }
    if (eq_mid != NULL) {
        for (i = 0; i < 4; i++) s_eq_mid[i] = eq_mid[i];
    }
    if (eq_high != NULL) {
        for (i = 0; i < 4; i++) s_eq_high[i] = eq_high[i];
    }
    if (denoise != NULL) {
        for (i = 0; i < 4; i++) s_denoise[i] = denoise[i];
    }
    PRINTF("[BS300] settings restored prog=%u\r\n", active_prog);
}

void bs300_print_settings(void)
{
    PRINTF("[SETTINGS] ========================================\r\n");
    PRINTF("[SETTINGS] cur_prog=%u\r\n", s_cur_prog);
    PRINTF("[SETTINGS] volume  = [%u,%u,%u,%u]\r\n",
           s_volumes[0], s_volumes[1], s_volumes[2], s_volumes[3]);
    PRINTF("[SETTINGS] eq_low  = [%d,%d,%d,%d]\r\n",
           s_eq_low[0], s_eq_low[1], s_eq_low[2], s_eq_low[3]);
    PRINTF("[SETTINGS] eq_mid  = [%d,%d,%d,%d]\r\n",
           s_eq_mid[0], s_eq_mid[1], s_eq_mid[2], s_eq_mid[3]);
    PRINTF("[SETTINGS] eq_high = [%d,%d,%d,%d]\r\n",
           s_eq_high[0], s_eq_high[1], s_eq_high[2], s_eq_high[3]);
    PRINTF("[SETTINGS] denoise = [%u,%u,%u,%u]\r\n",
           s_denoise[0], s_denoise[1], s_denoise[2], s_denoise[3]);
}

static void save_settings(void);

/* Reset user-adjustable params (volume=9, EQ=0) for a program.
 * Called when base calibration is modified (SetGain). */
void bs300_reset_user_params(uint8_t prog_idx)
{
    if (prog_idx >= 4) return;

    s_volumes[prog_idx] = 9;
    s_eq_low[prog_idx]  = 0;
    s_eq_mid[prog_idx]  = 0;
    s_eq_high[prog_idx] = 0;
    s_denoise[prog_idx] = 0;

    if (prog_idx == s_cur_prog) {
        s_dsp_state.modules.volume_level = 9;
        s_dsp_state.modules.eq_low  = 0;
        s_dsp_state.modules.eq_mid  = 0;
        s_dsp_state.modules.eq_high = 0;
    }

    save_settings();
}

static void save_settings(void)
{
    bs300_settings_save(s_cur_prog, s_volumes, s_eq_low, s_eq_mid, s_eq_high,
                        s_denoise);
}

/* Public wrapper — call on BLE disconnect or any safe idle moment. */
void bs300_settings_persist(void)
{
    save_settings();
}

/* ================================================================
 *  Boot-time cache: load active program into s_dsp_state + calib
 * ================================================================ */
void bs300_cache_boot_state(void)
{
    if (bs300_read_calibration(bs300_work_buf)) {
        bs300_parse_calibration(bs300_work_buf, &s_calib_cache);
        s_boot_cached = true;
    }

    bs300_storage_load_program(s_cur_prog, bs300_work_buf);
    bs300_flash_to_struct(bs300_work_buf, &s_dsp_state);
    s_dsp_state.modules.volume_level = s_volumes[s_cur_prog];
    s_dsp_state.modules.eq_low  = s_eq_low[s_cur_prog];
    s_dsp_state.modules.eq_mid  = s_eq_mid[s_cur_prog];
    s_dsp_state.modules.eq_high = s_eq_high[s_cur_prog];

    PRINTF("[BS300] boot cache: prog=%d vol=%d input=%d\r\n",
           s_cur_prog, s_dsp_state.modules.volume_level,
           s_dsp_state.modules.input_selection);
}

/* ================================================================
 *  Struct loading (Main Flash-backed)
 * ================================================================ */
static int load_struct(uint8_t prog_idx, bs300_prog_struct_t *out)
{
    bs300_storage_load_program(prog_idx, bs300_work_buf);
    return bs300_flash_to_struct(bs300_work_buf, out);
}

static int load_calib(bs300_calib_t *calib)
{
    if (s_boot_cached) {
        *calib = s_calib_cache;
        return 0;
    }
    if (!bs300_read_calibration(bs300_work_buf)) return -1;
    return bs300_parse_calibration(bs300_work_buf, calib);
}

/* ================================================================
 *  Frame checksum
 * ================================================================ */
static uint8_t calc_checksum(const uint8_t *buf, int len)
{
    uint16_t sum = 0;
    int i;
    for (i = 0; i < len; i++) sum += buf[i];
    return (uint8_t)(0xFF - (sum & 0xFF));
}

/* ================================================================
 *  Raw I2C frame send (no poll)
 * ================================================================ */
static int raw_write_packet(uint32_t cmd, const uint8_t *data)
{
    uint8_t frame[53];
    int i;

    frame[0] = 0x10;
    frame[1] = (uint8_t)(cmd & 0xFF);
    frame[2] = (uint8_t)((cmd >> 8) & 0xFF);
    frame[3] = (uint8_t)((cmd >> 16) & 0xFF);
    for (i = 0; i < 48; i++) frame[4 + i] = data[i];
    frame[52] = calc_checksum(frame, 52);

    PRINTF("[BS300] I2C TX CMD=0x%06lX:", (unsigned long)cmd);
    for (i = 0; i < 53; i++) PRINTF(" %02X", frame[i]);
    PRINTF("\r\n");

    return bs300_i2c_write(BS300_I2C_ADDR, frame, 53) ? 0 : -1;
}

/* ================================================================
 *  Poll FURPROC
 * ================================================================ */
static int poll_furproc(void)
{
    uint8_t req[2], resp[4];
    uint8_t chk;

    req[0] = 0x80;
    req[1] = (uint8_t)(0xFF - req[0]);
    if (!bs300_i2c_write(BS300_I2C_ADDR, req, 2)) return -1;

    if (!bs300_i2c_read(BS300_I2C_ADDR, resp, 4)) return -1;

    chk = (uint8_t)(0xFF - ((resp[0] + resp[1] + resp[2]) & 0xFF));
    if (chk != resp[3]) return -1;

    if (resp[2] & 0x80) return 1;
    return 0;
}

/* ================================================================
 *  Session management
 * ================================================================ */
void bs300_sync_session_init(bs300_sync_session_t *s)
{
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
    s->state = BS300_SYNC_IDLE;
}

int bs300_session_append(bs300_sync_session_t *s, uint32_t cmd, const uint8_t *data)
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

    if (s == NULL) return 0;

    /* Abort check — stop immediately, don't send more I2C */
    if (s->abort_requested) {
        s->state = BS300_SYNC_IDLE;
        PRINTF("[BS300] sync ABORTED at cmd=%d/%d\r\n", s->cmd_index, s->cmd_count);
        return 0;
    }

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

        PRINTF("[BS300] sync[%d/%d] SEND 0x%06lX\r\n",
               s->cmd_index + 1, s->cmd_count,
               (unsigned long)s->cmds[s->cmd_index]);

        ret = raw_write_packet(s->cmds[s->cmd_index],
                               s->datas[s->cmd_index]);
        Sys_Watchdog_Refresh();
        if (ret < 0) {
            s->retry_count++;
            if (s->retry_count >= 30) {
                s->fail_count++;
                s->state = BS300_SYNC_ERROR;
                PRINTF("[BS300] sync send fail cmd=0x%06lX\r\n",
                       (unsigned long)s->cmds[s->cmd_index]);
                return 0;
            }
            return 1;
        }

        s->retry_count = 0;
        s->state = BS300_SYNC_POLL;
        return 1;

    case BS300_SYNC_POLL:
        ret = poll_furproc();
        Sys_Watchdog_Refresh();
        if (ret < 0) {
            s->retry_count++;
            if (s->retry_count >= 30) {
                s->fail_count++;
                s->state = BS300_SYNC_ERROR;
                return 0;
            }
            s->state = BS300_SYNC_SEND;
            return 1;
        }
        if (ret == 0) {
            PRINTF("[BS300] sync[%d/%d] POLL OK\r\n",
                   s->cmd_index + 1, s->cmd_count);
            /* Success — update DSP state incrementally */
            if (s->dsp_state != NULL && s->target != NULL) {
                dsp_state_apply(s->cmds[s->cmd_index], s->target, s->dsp_state);
            }
            s->cmd_index++;
            s->retry_count = 0;

            if (s->cmd_index >= s->cmd_count) {
                s->state = BS300_SYNC_DONE;
                return 0;
            }
            s->state = BS300_SYNC_SEND;
            return 1;
        }

        s->retry_count++;
        if (s->retry_count >= 30) {
            s->fail_count++;
            s->state = BS300_SYNC_ERROR;
            return 0;
        }
        s->state = BS300_SYNC_SEND;
        return 1;
    }

    return 0;
}

/* ================================================================
 *  Dynamic encode sync — builds 31 commands into session or sends directly
 * ================================================================ */
static uint8_t get_input_type(uint8_t input_selection, uint8_t mm_type)
{
    switch (input_selection) {
    case 2: return 1;  /* Telecoil */
    case 3: return 2;  /* DAI */
    case 4:  /* MM Plus — igd determined by mm_type */
        if (mm_type == 0x00) return 1;  /* Telecoil */
        if (mm_type == 0x01) return 2;  /* DAI */
        return 0;
    default: return 0; /* Mic */
    }
}

static int bs300_sync_program_dynamic(bs300_prog_struct_t *prog,
                                       const bs300_calib_t *calib,
                                       uint8_t input_type,
                                       bs300_sync_session_t *session)
{
    uint8_t data[48];
    int sent = 0, fail = 0, ret;

#define SEND_CMD(cmd, fn) do { \
    memset(data, 0, 48); \
    ret = fn; \
    if (ret == 0) { \
        if (session) { \
            bs300_session_append(session, cmd, data); \
        } else { \
            if (bs300_advanced_write(cmd, data)) { sent++; } \
            else { fail++; \
                PRINTF("[BS300] sync FAIL cmd=0x%06lX\r\n", (unsigned long)(cmd)); \
            } \
        } \
    } \
} while(0)

    prog->modules.wnr_enable_dual |= 0x01;

    SEND_CMD(0x800022, bs300_encode_ddm2(&prog->modules, calib, data));
    SEND_CMD(0x800062, bs300_encode_mm_plus(&prog->modules, calib, input_type, data));
    SEND_CMD(0x800052, bs300_encode_dfbc(&prog->modules, calib, data));

    if (prog->enr.enable_num_ch & 0x80) {
        SEND_CMD(0x8000C2, bs300_encode_enr_general(&prog->enr, data));
        SEND_CMD(0x8010C2, bs300_encode_enr_freq_spacing(&prog->enr, data));
        SEND_CMD(0x8020C2, bs300_encode_enr_snr_threshold(&prog->enr, data));
        SEND_CMD(0x8030C2, bs300_encode_enr_max_att(&prog->enr, data));
        SEND_CMD(0x8040C2, bs300_encode_enr_noise_th(&prog->enr, calib, input_type, data));
        SEND_CMD(0x8050C2, bs300_encode_enr_upper_noise_th(&prog->enr, calib, input_type, data));
        SEND_CMD(0x8060C2, bs300_encode_enr_smoothing(&prog->enr, data));
        SEND_CMD(0x8070C2, bs300_encode_enr_etr(&prog->enr, data));
        SEND_CMD(0x8080C2, bs300_encode_enr_nrr(&prog->enr, data));
    } else {
        memset(data, 0, 48);
        if (session) bs300_session_append(session, 0x8000C2, data);
        else { if (bs300_advanced_write(0x8000C2, data)) sent++; else fail++; }
    }

    memset(data, 0, 48);
    if (session) bs300_session_append(session, 0x800172, data);
    else { if (bs300_advanced_write(0x800172, data)) sent++; else fail++; }
    SEND_CMD(0x804272, bs300_encode_tc_dai(calib, input_type, data));
    SEND_CMD(0x8001B2, bs300_encode_iss(&prog->modules, calib, input_type, data));

    SEND_CMD(0x8001C2, bs300_encode_wnr_setup(&prog->modules, calib, data));
    SEND_CMD(0x8011C2, bs300_encode_wnr_band_0_15(&prog->modules, calib, input_type, data));
    SEND_CMD(0x8411C2, bs300_encode_wnr_band_16_31(&prog->modules, calib, input_type, data));
    SEND_CMD(0x8021C2, bs300_encode_wnr_single_mic(&prog->modules, calib, input_type, data));

    SEND_CMD(0x800382, bs300_encode_agco(&prog->modules, data));
    SEND_CMD(0x800081, bs300_encode_volume_beep(&prog->modules, calib, data));

    SEND_CMD(0x8000B2, bs300_encode_wdrc_general(&prog->wdrc, data));
    SEND_CMD(0x8010B2, bs300_encode_wdrc_freq_spacing(&prog->wdrc, data));
    SEND_CMD(0x8020B2, bs300_encode_wdrc_kp_threshold(&prog->wdrc, calib, input_type, data));
    SEND_CMD(0x8030B2, bs300_encode_wdrc_attack_time(&prog->wdrc, data));
    SEND_CMD(0x8040B2, bs300_encode_wdrc_release_time(&prog->wdrc, data));
    SEND_CMD(0x8050B2, bs300_encode_wdrc_ratio(&prog->wdrc, data));
    SEND_CMD(0x8060B2, bs300_encode_wdrc_bin_gain(&prog->wdrc, calib, &prog->modules, input_type, data));
    SEND_CMD(0x8070B2, bs300_encode_wdrc_lmt_threshold(&prog->wdrc, calib, data));
    SEND_CMD(0x8080B2, bs300_encode_wdrc_lmt_attack(&prog->wdrc, data));
    SEND_CMD(0x8090B2, bs300_encode_wdrc_lmt_release(&prog->wdrc, data));
    SEND_CMD(0x80A0B2, bs300_encode_wdrc_lmt_ratio(&prog->wdrc, data));

#undef SEND_CMD

    PRINTF("[BS300] sync_dynamic done, sent=%d fail=%d\r\n", sent, fail);
    return (fail > 0) ? -1 : 0;
}

static int sync_program_inner(bs300_prog_struct_t *prog,
                               bs300_sync_session_t *session)
{
    bs300_calib_t calib;
    uint8_t input_type;

    if (prog == NULL) return -1;

    if (load_calib(&calib) < 0) {
        PRINTF("[BS300] sync calib load fail\r\n");
        return -1;
    }

    input_type = get_input_type(prog->modules.input_selection, prog->modules.mm_type);

    return bs300_sync_program_dynamic(prog, &calib, input_type, session);
}

int bs300_sync_program(bs300_prog_struct_t *prog)
{
    return sync_program_inner(prog, NULL);
}

int bs300_sync_program_start(bs300_sync_session_t *s, bs300_prog_struct_t *prog)
{
    if (s == NULL || prog == NULL) return -1;
    return sync_program_inner(prog, s);
}

/* ================================================================
 *  Per-module incremental diff helpers
 *  Compare s_dsp_state (old) vs s_target (new), using s_target as source.
 * ================================================================ */

#define SEND_IF_DIRTY(session, cmd, fn_call) do { \
    memset(data, 0, 48); \
    ret = (fn_call); \
    if (ret == 0) { \
        if (session) bs300_session_append(session, cmd, data); \
        else { if (bs300_advanced_write(cmd, data)) (*sent)++; else (*fail)++; } \
    } \
} while(0)

static void switch_diff_wdrc(const bs300_wdrc_t *nw,
                              const bs300_modules_t *nm,
                              const bs300_calib_t *calib,
                              uint8_t new_it, int igd_changed,
                              bs300_sync_session_t *session,
                              int *sent, int *fail, uint8_t *data)
{
    const bs300_wdrc_t *ow = &s_dsp_state.wdrc;
    const bs300_modules_t *om = &s_dsp_state.modules;
    int ret;
    int hdr_changed = (ow->total_channels != nw->total_channels)
                   || (ow->nsbc != nw->nsbc)
                   || (ow->kp_mode != nw->kp_mode)
                   || (ow->limiter != nw->limiter);
    int freq_changed = (memcmp(ow->freq_idx, nw->freq_idx, 16) != 0);
    int kp1_th_changed = (memcmp(ow->kp1_th_db, nw->kp1_th_db, 16) != 0);
    int kp2_th_changed = (memcmp(ow->kp2_th_db, nw->kp2_th_db, 16) != 0);
    int at_changed = (memcmp(ow->epd_at_idx, nw->epd_at_idx, 16) != 0)
                  || (memcmp(ow->kp1_at_idx, nw->kp1_at_idx, 16) != 0)
                  || (memcmp(ow->kp2_at_idx, nw->kp2_at_idx, 16) != 0);
    int rt_changed = (memcmp(ow->epd_rt_idx, nw->epd_rt_idx, 16) != 0)
                  || (memcmp(ow->kp1_rt_idx, nw->kp1_rt_idx, 16) != 0)
                  || (memcmp(ow->kp2_rt_idx, nw->kp2_rt_idx, 16) != 0);
    int ratio_changed = (memcmp(ow->epd_r_idx, nw->epd_r_idx, 16) != 0)
                     || (memcmp(ow->kp1_r_idx, nw->kp1_r_idx, 16) != 0)
                     || (memcmp(ow->kp2_r_idx, nw->kp2_r_idx, 16) != 0);
    int bg_changed = (memcmp(ow->bin_gain, nw->bin_gain, 32) != 0);
    int vol_eq_changed = (om->volume_level != nm->volume_level)
                      || (om->eq_low != nm->eq_low)
                      || (om->eq_mid != nm->eq_mid)
                      || (om->eq_high != nm->eq_high);
    int lmt_th_changed = (memcmp(ow->lmt_th_db, nw->lmt_th_db, 16) != 0);
    int lmt_at_changed = (memcmp(ow->lmt_at_idx, nw->lmt_at_idx, 16) != 0);
    int lmt_rt_changed = (memcmp(ow->lmt_rt_idx, nw->lmt_rt_idx, 16) != 0);
    int lmt_r_changed  = (memcmp(ow->lmt_r_idx,  nw->lmt_r_idx,  16) != 0);

    if (hdr_changed)
        SEND_IF_DIRTY(session, 0x8000B2, bs300_encode_wdrc_general(nw, data));
    if (hdr_changed || freq_changed)
        SEND_IF_DIRTY(session, 0x8010B2, bs300_encode_wdrc_freq_spacing(nw, data));
    if (hdr_changed || freq_changed || kp1_th_changed || kp2_th_changed || igd_changed)
        SEND_IF_DIRTY(session, 0x8020B2, bs300_encode_wdrc_kp_threshold(nw, calib, new_it, data));
    if (hdr_changed || at_changed)
        SEND_IF_DIRTY(session, 0x8030B2, bs300_encode_wdrc_attack_time(nw, data));
    if (hdr_changed || rt_changed)
        SEND_IF_DIRTY(session, 0x8040B2, bs300_encode_wdrc_release_time(nw, data));
    if (hdr_changed || ratio_changed)
        SEND_IF_DIRTY(session, 0x8050B2, bs300_encode_wdrc_ratio(nw, data));
    if (bg_changed || vol_eq_changed || igd_changed)
        SEND_IF_DIRTY(session, 0x8060B2, bs300_encode_wdrc_bin_gain(nw, calib, nm, new_it, data));

    if (ow->limiter == 0 && nw->limiter == 0) {
    } else if (ow->limiter == 0 && nw->limiter == 1) {
        SEND_IF_DIRTY(session, 0x8070B2, bs300_encode_wdrc_lmt_threshold(nw, calib, data));
        SEND_IF_DIRTY(session, 0x8080B2, bs300_encode_wdrc_lmt_attack(nw, data));
        SEND_IF_DIRTY(session, 0x8090B2, bs300_encode_wdrc_lmt_release(nw, data));
        SEND_IF_DIRTY(session, 0x80A0B2, bs300_encode_wdrc_lmt_ratio(nw, data));
    } else if (ow->limiter == 1 && nw->limiter == 0) {
        memset(data, 0, 48);
        if (session) {
            bs300_session_append(session, 0x8070B2, data);
            bs300_session_append(session, 0x8080B2, data);
            bs300_session_append(session, 0x8090B2, data);
            bs300_session_append(session, 0x80A0B2, data);
        } else {
            if (bs300_advanced_write(0x8070B2, data)) (*sent)++; else (*fail)++;
            if (bs300_advanced_write(0x8080B2, data)) (*sent)++; else (*fail)++;
            if (bs300_advanced_write(0x8090B2, data)) (*sent)++; else (*fail)++;
            if (bs300_advanced_write(0x80A0B2, data)) (*sent)++; else (*fail)++;
        }
    } else {
        if (freq_changed || lmt_th_changed)
            SEND_IF_DIRTY(session, 0x8070B2, bs300_encode_wdrc_lmt_threshold(nw, calib, data));
        if (lmt_at_changed)
            SEND_IF_DIRTY(session, 0x8080B2, bs300_encode_wdrc_lmt_attack(nw, data));
        if (lmt_rt_changed)
            SEND_IF_DIRTY(session, 0x8090B2, bs300_encode_wdrc_lmt_release(nw, data));
        if (lmt_r_changed)
            SEND_IF_DIRTY(session, 0x80A0B2, bs300_encode_wdrc_lmt_ratio(nw, data));
    }
}

static void switch_diff_vol_beep(const bs300_modules_t *nm,
                                  const bs300_calib_t *calib,
                                  int igd_changed,
                                  bs300_sync_session_t *session,
                                  int *sent, int *fail, uint8_t *data)
{
    const bs300_modules_t *om = &s_dsp_state.modules;
    int ret;
    if (om->vol_enable == 0 && nm->vol_enable == 0) {
    } else if (om->vol_enable == 0 && nm->vol_enable == 1) {
        SEND_IF_DIRTY(session, 0x800081, bs300_encode_volume_beep(nm, calib, data));
    } else if (om->vol_enable == 1 && nm->vol_enable == 0) {
        memset(data, 0, 48);
        if (session) bs300_session_append(session, 0x800081, data);
        else { if (bs300_advanced_write(0x800081, data)) (*sent)++; else (*fail)++; }
    } else {
        int vol_changed = (om->beep_level != nm->beep_level)
                       || (om->beep_freq_idx != nm->beep_freq_idx)
                       || (om->min_vol != nm->min_vol)
                       || (om->max_vol != nm->max_vol)
                       || (om->input_selection != nm->input_selection);
        if (vol_changed || igd_changed)
            SEND_IF_DIRTY(session, 0x800081, bs300_encode_volume_beep(nm, calib, data));
    }
}

static void switch_diff_enr(const bs300_enr_t *ne,
                             const bs300_calib_t *calib,
                             uint8_t new_it, int igd_changed,
                             bs300_sync_session_t *session,
                             int *sent, int *fail, uint8_t *data)
{
    const bs300_enr_t *oe = &s_dsp_state.enr;
    int ret;
    uint8_t oe_ena = (oe->enable_num_ch & 0x80) ? 1 : 0;
    uint8_t ne_ena = (ne->enable_num_ch & 0x80) ? 1 : 0;

    if (oe_ena == 0 && ne_ena == 0) {
    } else if (oe_ena == 0 && ne_ena == 1) {
        SEND_IF_DIRTY(session, 0x8000C2, bs300_encode_enr_general(ne, data));
        SEND_IF_DIRTY(session, 0x8010C2, bs300_encode_enr_freq_spacing(ne, data));
        SEND_IF_DIRTY(session, 0x8020C2, bs300_encode_enr_snr_threshold(ne, data));
        SEND_IF_DIRTY(session, 0x8030C2, bs300_encode_enr_max_att(ne, data));
        SEND_IF_DIRTY(session, 0x8040C2, bs300_encode_enr_noise_th(ne, calib, new_it, data));
        SEND_IF_DIRTY(session, 0x8050C2, bs300_encode_enr_upper_noise_th(ne, calib, new_it, data));
        SEND_IF_DIRTY(session, 0x8060C2, bs300_encode_enr_smoothing(ne, data));
        SEND_IF_DIRTY(session, 0x8070C2, bs300_encode_enr_etr(ne, data));
        SEND_IF_DIRTY(session, 0x8080C2, bs300_encode_enr_nrr(ne, data));
    } else if (oe_ena == 1 && ne_ena == 0) {
        memset(data, 0, 48);
        if (session) bs300_session_append(session, 0x8000C2, data);
        else { if (bs300_advanced_write(0x8000C2, data)) (*sent)++; else (*fail)++; }
    } else {
        int enr_hdr_changed = (oe->enable_num_ch != ne->enable_num_ch);
        int enr_freq_changed = (memcmp(oe->freq_idx, ne->freq_idx, 16) != 0);
        int snr_changed = (memcmp(oe->snr_th_db, ne->snr_th_db, 16) != 0);
        int ma_changed  = (memcmp(oe->max_att_db, ne->max_att_db, 16) != 0);
        int nt_changed  = (memcmp(oe->noise_th_db, ne->noise_th_db, 16) != 0);
        int unt_changed = (memcmp(oe->upper_noise_th_db, ne->upper_noise_th_db, 16) != 0);
        int sf_changed  = (oe->nfsf != ne->nfsf) || (oe->nhsf != ne->nhsf)
                       || (oe->nnsf != ne->nnsf) || (oe->snasf != ne->snasf);
        int etr_changed = (memcmp(oe->etr_x100, ne->etr_x100, 16) != 0);
        int nrr_changed = (memcmp(oe->nrr_x10, ne->nrr_x10, 16) != 0);

        if (enr_hdr_changed)
            SEND_IF_DIRTY(session, 0x8000C2, bs300_encode_enr_general(ne, data));
        if (enr_hdr_changed || enr_freq_changed)
            SEND_IF_DIRTY(session, 0x8010C2, bs300_encode_enr_freq_spacing(ne, data));
        if (snr_changed)
            SEND_IF_DIRTY(session, 0x8020C2, bs300_encode_enr_snr_threshold(ne, data));
        if (snr_changed || ma_changed)
            SEND_IF_DIRTY(session, 0x8030C2, bs300_encode_enr_max_att(ne, data));
        if (enr_freq_changed || nt_changed || igd_changed)
            SEND_IF_DIRTY(session, 0x8040C2, bs300_encode_enr_noise_th(ne, calib, new_it, data));
        if (enr_freq_changed || unt_changed || igd_changed)
            SEND_IF_DIRTY(session, 0x8050C2, bs300_encode_enr_upper_noise_th(ne, calib, new_it, data));
        if (sf_changed)
            SEND_IF_DIRTY(session, 0x8060C2, bs300_encode_enr_smoothing(ne, data));
        if (ma_changed || etr_changed)
            SEND_IF_DIRTY(session, 0x8070C2, bs300_encode_enr_etr(ne, data));
        if (ma_changed || nrr_changed)
            SEND_IF_DIRTY(session, 0x8080C2, bs300_encode_enr_nrr(ne, data));
    }
}

static void switch_diff_pre_enr(const bs300_modules_t *nm,
                                 const bs300_calib_t *calib,
                                 uint8_t new_it, int igd_changed,
                                 bs300_sync_session_t *session,
                                 int *sent, int *fail, uint8_t *data)
{
    const bs300_modules_t *om = &s_dsp_state.modules;
    int ret;

    /* DDM2 */
    {
        uint8_t o_dd = (om->input_selection == 5) ? 1 : 0;
        uint8_t n_dd = (nm->input_selection == 5) ? 1 : 0;
        if (o_dd == 0 && n_dd == 0) {
        } else if (o_dd == 0 && n_dd == 1) {
            SEND_IF_DIRTY(session, 0x800022, bs300_encode_ddm2(nm, calib, data));
        } else if (o_dd == 1 && n_dd == 0) {
            memset(data, 0, 48);
            if (session) bs300_session_append(session, 0x800022, data);
            else { if (bs300_advanced_write(0x800022, data)) (*sent)++; else (*fail)++; }
        } else {
            int ddm2_changed = (om->open_ear != nm->open_ear)
                            || (om->polar_pattern != nm->polar_pattern)
                            || (om->adm_fdm != nm->adm_fdm);
            if (ddm2_changed)
                SEND_IF_DIRTY(session, 0x800022, bs300_encode_ddm2(nm, calib, data));
        }
    }

    /* MM Plus */
    {
        uint8_t o_mm = (om->input_selection == 4) ? 1 : 0;
        uint8_t n_mm = (nm->input_selection == 4) ? 1 : 0;
        if (o_mm == 0 && n_mm == 0) {
        } else if (o_mm == 0 && n_mm == 1) {
            SEND_IF_DIRTY(session, 0x800062, bs300_encode_mm_plus(nm, calib, new_it, data));
        } else if (o_mm == 1 && n_mm == 0) {
            memset(data, 0, 48);
            if (session) bs300_session_append(session, 0x800062, data);
            else { if (bs300_advanced_write(0x800062, data)) (*sent)++; else (*fail)++; }
        } else {
            int mm_changed = (om->mix_ratio != nm->mix_ratio);
            if (mm_changed || igd_changed)
                SEND_IF_DIRTY(session, 0x800062, bs300_encode_mm_plus(nm, calib, new_it, data));
        }
    }

    /* DFBC */
    {
        uint8_t o_ena = (om->dfbc_enable_mode & 0x80) ? 1 : 0;
        uint8_t n_ena = (nm->dfbc_enable_mode & 0x80) ? 1 : 0;
        if (o_ena == 0 && n_ena == 0) {
        } else if (o_ena == 0 && n_ena == 1) {
            SEND_IF_DIRTY(session, 0x800052, bs300_encode_dfbc(nm, calib, data));
        } else if (o_ena == 1 && n_ena == 0) {
            memset(data, 0, 48);
            if (session) bs300_session_append(session, 0x800052, data);
            else { if (bs300_advanced_write(0x800052, data)) (*sent)++; else (*fail)++; }
        } else {
            int dfbc_changed = (om->dfbc_enable_mode != nm->dfbc_enable_mode);
            if (dfbc_changed)
                SEND_IF_DIRTY(session, 0x800052, bs300_encode_dfbc(nm, calib, data));
        }
    }
}

static void switch_diff_post_enr(bs300_modules_t *nm,
                                  const bs300_calib_t *calib,
                                  uint8_t new_it, int igd_changed,
                                  bs300_sync_session_t *session,
                                  int *sent, int *fail, uint8_t *data)
{
    const bs300_modules_t *om = &s_dsp_state.modules;
    int ret;

    /* TC/DAI — includes MM+ with mm_type=Telecoil/DAI */
    {
        uint8_t o_tc = (get_input_type(om->input_selection, om->mm_type) != 0) ? 1 : 0;
        uint8_t n_tc = (get_input_type(nm->input_selection, nm->mm_type) != 0) ? 1 : 0;
        if (o_tc == 0 && n_tc == 0) {
        } else if (o_tc == 0 && n_tc == 1) {
            SEND_IF_DIRTY(session, 0x804272, bs300_encode_tc_dai(calib, new_it, data));
        } else if (o_tc == 1 && n_tc == 0) {
            memset(data, 0, 48);
            if (session) bs300_session_append(session, 0x804272, data);
            else { if (bs300_advanced_write(0x804272, data)) (*sent)++; else (*fail)++; }
        } else {
            if (om->input_selection != nm->input_selection || igd_changed)
                SEND_IF_DIRTY(session, 0x804272, bs300_encode_tc_dai(calib, new_it, data));
        }
    }

    /* ISS */
    {
        uint8_t o_ena = om->iss_enable;
        uint8_t n_ena = nm->iss_enable;
        if (o_ena == 0 && n_ena == 0) {
        } else if (o_ena == 0 && n_ena == 1) {
            SEND_IF_DIRTY(session, 0x8001B2, bs300_encode_iss(nm, calib, new_it, data));
        } else if (o_ena == 1 && n_ena == 0) {
            memset(data, 0, 48);
            if (session) bs300_session_append(session, 0x8001B2, data);
            else { if (bs300_advanced_write(0x8001B2, data)) (*sent)++; else (*fail)++; }
        } else {
            int iss_changed = (om->iss_threshold != nm->iss_threshold);
            if (iss_changed || igd_changed)
                SEND_IF_DIRTY(session, 0x8001B2, bs300_encode_iss(nm, calib, new_it, data));
        }
    }

    /* WNR */
    {
        nm->wnr_enable_dual |= 0x01;
        uint8_t o_ena = om->wnr_enable_dual & 0x01;
        uint8_t n_ena = nm->wnr_enable_dual & 0x01;
        if (o_ena == 0 && n_ena == 0) {
        } else if (o_ena == 0 && n_ena == 1) {
            SEND_IF_DIRTY(session, 0x8001C2, bs300_encode_wnr_setup(nm, calib, data));
            SEND_IF_DIRTY(session, 0x8011C2, bs300_encode_wnr_band_0_15(nm, calib, new_it, data));
            SEND_IF_DIRTY(session, 0x8411C2, bs300_encode_wnr_band_16_31(nm, calib, new_it, data));
            SEND_IF_DIRTY(session, 0x8021C2, bs300_encode_wnr_single_mic(nm, calib, new_it, data));
        } else if (o_ena == 1 && n_ena == 0) {
            memset(data, 0, 48);
            if (session) bs300_session_append(session, 0x8001C2, data);
            else { if (bs300_advanced_write(0x8001C2, data)) (*sent)++; else (*fail)++; }
        } else {
            int wnr_changed = (om->wnr_preset != nm->wnr_preset);
            if (wnr_changed || igd_changed)
                SEND_IF_DIRTY(session, 0x8001C2, bs300_encode_wnr_setup(nm, calib, data));
            if (igd_changed) {
                SEND_IF_DIRTY(session, 0x8011C2, bs300_encode_wnr_band_0_15(nm, calib, new_it, data));
                SEND_IF_DIRTY(session, 0x8411C2, bs300_encode_wnr_band_16_31(nm, calib, new_it, data));
                SEND_IF_DIRTY(session, 0x8021C2, bs300_encode_wnr_single_mic(nm, calib, new_it, data));
            }
        }
    }

    /* AGCO */
    {
        uint8_t o_ena = om->agco_enable;
        uint8_t n_ena = nm->agco_enable;
        if (o_ena == 0 && n_ena == 0) {
        } else if (o_ena == 0 && n_ena == 1) {
            SEND_IF_DIRTY(session, 0x800382, bs300_encode_agco(nm, data));
        } else if (o_ena == 1 && n_ena == 0) {
            memset(data, 0, 48);
            if (session) bs300_session_append(session, 0x800382, data);
            else { if (bs300_advanced_write(0x800382, data)) (*sent)++; else (*fail)++; }
        } else {
            int agco_changed = (om->agco_threshold_db != nm->agco_threshold_db)
                            || (om->agco_attack_01ms != nm->agco_attack_01ms)
                            || (om->agco_release_01ms != nm->agco_release_01ms);
            if (agco_changed)
                SEND_IF_DIRTY(session, 0x800382, bs300_encode_agco(nm, data));
        }
    }
}

#undef SEND_IF_DIRTY

/* ================================================================
 *  Build diff session: s_dsp_state (old) vs s_target (new)
 * ================================================================ */
static int build_diff_session(bs300_sync_session_t *s)
{
    uint8_t data[48];
    bs300_calib_t calib;
    uint8_t old_it, new_it, igd_changed;
    int sent = 0, fail = 0;

    if (load_calib(&calib) < 0) return -1;

    old_it = get_input_type(s_dsp_state.modules.input_selection, s_dsp_state.modules.mm_type);
    new_it = get_input_type(s_target.modules.input_selection, s_target.modules.mm_type);
    igd_changed = (old_it != new_it);

    switch_diff_pre_enr(&s_target.modules, &calib, new_it, igd_changed,
                        s, &sent, &fail, data);
    switch_diff_enr(&s_target.enr, &calib, new_it, igd_changed,
                    s, &sent, &fail, data);
    switch_diff_post_enr(&s_target.modules, &calib, new_it, igd_changed,
                         s, &sent, &fail, data);
    switch_diff_vol_beep(&s_target.modules, &calib, igd_changed,
                         s, &sent, &fail, data);
    switch_diff_wdrc(&s_target.wdrc, &s_target.modules,
                     &calib, new_it, igd_changed,
                     s, &sent, &fail, data);

    /* Force DDM2/MM+ if target input requires them (diff may have missed
     * due to voice_prompt_input_switch side-effects on DSP registers). */
    if (s_target.modules.input_selection == 5) {
        memset(data, 0, 48);
        bs300_encode_ddm2(&s_target.modules, &calib, data);
        bs300_session_append(s, 0x800022, data);
    }
    if (s_target.modules.input_selection == 4) {
        memset(data, 0, 48);
        bs300_encode_mm_plus(&s_target.modules, &calib, new_it, data);
        bs300_session_append(s, 0x800062, data);
    }

    /* Force Vol/Beep — input_selection must be guaranteed */
    memset(data, 0, 48);
    bs300_encode_volume_beep(&s_target.modules, &calib, data);
    bs300_session_append(s, 0x800081, data);

    return 0;
}

/* ================================================================
 *  Program switch (blocking, for init)
 * ================================================================ */
int bs300_switch_program(uint8_t new_prog_idx)
{
    bs300_calib_t calib;
    uint8_t data[48];
    uint8_t old_it, new_it, igd_changed;
    int sent = 0, fail = 0;

    if (new_prog_idx >= 4) return -1;
    if (s_cur_prog == new_prog_idx) return 0;

    if (load_struct(new_prog_idx, &s_target) < 0) return -1;

    if (load_calib(&calib) < 0) return -1;

    old_it = get_input_type(s_dsp_state.modules.input_selection, s_dsp_state.modules.mm_type);
    new_it = get_input_type(s_target.modules.input_selection, s_target.modules.mm_type);
    igd_changed = (old_it != new_it);

    PRINTF("[BS300] switch RAM %d->%d\r\n", s_cur_prog, new_prog_idx);

    s_cur_prog = new_prog_idx;
    s_target.modules.volume_level = s_volumes[new_prog_idx];
    save_settings();

    switch_diff_pre_enr(&s_target.modules, &calib, new_it, igd_changed,
                        NULL, &sent, &fail, data);
    switch_diff_enr(&s_target.enr, &calib, new_it, igd_changed,
                    NULL, &sent, &fail, data);
    switch_diff_post_enr(&s_target.modules, &calib, new_it, igd_changed,
                         NULL, &sent, &fail, data);
    switch_diff_vol_beep(&s_target.modules, &calib, igd_changed,
                         NULL, &sent, &fail, data);
    switch_diff_wdrc(&s_target.wdrc, &s_target.modules,
                     &calib, new_it, igd_changed,
                     NULL, &sent, &fail, data);

    if (s_target.modules.input_selection == 5) {
        memset(data, 0, 48);
        bs300_encode_ddm2(&s_target.modules, &calib, data);
        if (bs300_advanced_write(0x800022, data)) sent++; else fail++;
    }
    if (s_target.modules.input_selection == 4) {
        memset(data, 0, 48);
        bs300_encode_mm_plus(&s_target.modules, &calib, new_it, data);
        if (bs300_advanced_write(0x800062, data)) sent++; else fail++;
    }
    memset(data, 0, 48);
    if (bs300_encode_volume_beep(&s_target.modules, &calib, data) == 0) {
        if (bs300_advanced_write(0x800081, data)) sent++; else fail++;
    }

    /* All commands sent — now s_dsp_state == s_target */
    memcpy(&s_dsp_state, &s_target, sizeof(s_dsp_state));

    PRINTF("[BS300] switch_program done, sent=%d fail=%d\r\n", sent, fail);
    return (fail > 0) ? -1 : 0;
}

/* ================================================================
 *  Voice prompt input switch — uses s_dsp_state directly
 * ================================================================ */
uint8_t bs300_voice_prompt_input_switch(uint8_t target_input)
{
    uint8_t data[48];
    uint8_t vb_data[48];
    bs300_modules_t mod_tmp;
    uint8_t original_input;
    int ret;

    if (!s_boot_cached) return 0xFF;

    original_input = s_dsp_state.modules.input_selection;
    if (original_input == target_input) return original_input;

    mod_tmp = s_dsp_state.modules;
    mod_tmp.input_selection = target_input;
    ret = bs300_encode_volume_beep(&mod_tmp, &s_calib_cache, vb_data);
    if (ret < 0) return 0xFF;

    ret = bs300_mute();
    if (ret < 0) return 0xFF;

    if (original_input == 5) {
        memset(data, 0, 48);
        bs300_advanced_write(0x800022, data);
    }
    if (original_input == 4) {
        memset(data, 0, 48);
        bs300_advanced_write(0x800062, data);
    }

    memset(data, 0, 48);
    bs300_advanced_write(0x8000C2, data);

    ret = bs300_advanced_write(0x800081, vb_data) ? 0 : -1;
    if (ret < 0) return 0xFF;

    bs300_active();
    return original_input;
}

int bs300_voice_prompt_input_restore(uint8_t original_input)
{
    uint8_t data[48];
    uint8_t vb_data[48];
    bs300_modules_t mod_tmp;
    int ret;

    if (!s_boot_cached) return -1;

    mod_tmp = s_dsp_state.modules;
    mod_tmp.input_selection = original_input;
    ret = bs300_encode_volume_beep(&mod_tmp, &s_calib_cache, vb_data);
    if (ret < 0) return -1;

    bs300_mute();

    if (original_input == 5) {
        bs300_encode_ddm2(&mod_tmp, &s_calib_cache, data);
        bs300_advanced_write(0x800022, data);
    }
    if (original_input == 4) {
        bs300_encode_mm_plus(&mod_tmp, &s_calib_cache, 0, data);
        bs300_advanced_write(0x800062, data);
    }

    bs300_encode_enr_general(&s_dsp_state.enr, data);
    bs300_advanced_write(0x8000C2, data);

    bs300_advanced_write(0x800081, vb_data);
    bs300_active();

    return 0;
}

/* ================================================================
 *  Volume / EQ
 * ================================================================ */
int bs300_set_volume(uint8_t level)
{
    uint8_t data[48];
    bs300_calib_t calib;

    if (level > 9) return -1;
    if (load_calib(&calib) < 0) return -1;

    s_dsp_state.modules.volume_level = level;
    s_volumes[s_cur_prog] = level;
    save_settings();

    return bs300_encode_wdrc_bin_gain(&s_dsp_state.wdrc, &calib,
                                       &s_dsp_state.modules,
                                       get_input_type(s_dsp_state.modules.input_selection, s_dsp_state.modules.mm_type),
                                       data) == 0
           && bs300_advanced_write(0x8060B2, data);
}

int bs300_set_eq(int8_t low, int8_t mid, int8_t high)
{
    uint8_t data[48];
    bs300_calib_t calib;

    if (load_calib(&calib) < 0) return -1;

    s_dsp_state.modules.eq_low  = low;
    s_dsp_state.modules.eq_mid  = mid;
    s_dsp_state.modules.eq_high = high;

    return bs300_encode_wdrc_bin_gain(&s_dsp_state.wdrc, &calib,
                                       &s_dsp_state.modules,
                                       get_input_type(s_dsp_state.modules.input_selection, s_dsp_state.modules.mm_type),
                                       data) == 0
           && bs300_advanced_write(0x8060B2, data);
}

/* ================================================================
 *  ITG (Input Tone Generator)
 * ================================================================ */
int bs300_itg_write(uint8_t level_db, uint16_t freq_hz,
                    const bs300_calib_t *calib)
{
    uint8_t data[48];
    int ret;

    if (calib == NULL) return -1;

    ret = bs300_encode_itg(level_db, freq_hz, 1, calib, data);
    if (ret < 0) return ret;

    if (!bs300_advanced_write(0x8001E2, data)) return -1;
    return 0;
}

int bs300_itg_clear(void)
{
    uint8_t data[48];
    memset(data, 0, 48);
    return bs300_advanced_write(0x8001E2, data) ? 0 : -1;
}

/* ================================================================
 *  Audiometry
 * ================================================================ */
int bs300_audiometry_enter(void)
{
    uint8_t data[48];
    int ret;
    uint8_t i;

    ret = bs300_mute();
    if (ret < 0) return ret;

    memset(data, 0, 48);
    bs300_advanced_write(0x8000C2, data);
    bs300_advanced_write(0x800022, data);
    bs300_advanced_write(0x800062, data);
    bs300_advanced_write(0x800052, data);
    bs300_advanced_write(0x800172, data);
    bs300_advanced_write(0x8001B2, data);
    bs300_advanced_write(0x8001C2, data);
    bs300_advanced_write(0x800382, data);

    memset(data, 0, 48);
    data[0]  = 0x01;
    data[3]  = 0x10;
    data[9]  = 0x10;
    data[12] = 0x03;
    bs300_advanced_write(0x8000B2, data);

    memset(data, 0, 48);
    for (i = 0; i < 16; i++) {
        data[i * 3 + 0] = 0x41;
        data[i * 3 + 1] = 0x10;
        data[i * 3 + 2] = 0x04;
    }
    bs300_advanced_write(0x8010B2, data);

    {
        static const uint8_t kp_th[32] = {
            0xA5,0xA5,0xA6,0xA6,0xA6,0xA6,0xA5,0xA5,
            0xA5,0xA5,0xA2,0xA2,0xA1,0xA1,0xA5,0xA5,
            0xA4,0xA4,0xA6,0xA6,0xA4,0xA4,0xA4,0xA4,
            0xA5,0xA5,0xA3,0xA3,0xA3,0xA3,0xA3,0xA3,
        };
        memset(data, 0, 48);
        for (i = 0; i < 32; i++) data[i] = kp_th[i];
        bs300_advanced_write(0x8020B2, data);
    }

    memset(data, 0x20, 47);
    data[47] = 20;
    bs300_advanced_write(0x8050B2, data);

    {
        static const uint8_t bin_gain[32] = {
            0xFB,0x09,0x09,0x09,0x09,0x0D,0xFF,0x0A,
            0x08,0x0B,0xFE,0x0F,0x0B,0x0A,0xF9,0x08,
            0x0E,0x0D,0x04,0x09,0x0B,0x0A,0x0A,0x15,
            0x0F,0x15,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,
        };
        memset(data, 0, 48);
        for (i = 0; i < 32; i++) data[i] = bin_gain[i];
        bs300_advanced_write(0x8060B2, data);
    }

    return bs300_active();
}

int bs300_audiometry_exit(void)
{
    int ret;

    ret = bs300_mute();
    if (ret < 0) return ret;

    bs300_itg_clear();

    /* Full re-sync from s_dsp_state — restore original config */
    ret = bs300_sync_program(&s_dsp_state);
    if (ret < 0) return ret;

    return bs300_active();
}

/* ================================================================
 *  Basic commands
 * ================================================================ */
int bs300_mute(void)
{
    bs300_i2c_set_speed(BS300_I2C_DELAY_NORMAL);
    bool ok = bs300_send_simple_cmd(BS300_CMD_MUTE);
    if (ok) bs300_i2c_set_speed(BS300_I2C_DELAY_FAST);
    return ok ? 0 : -1;
}

int bs300_active(void)
{
    bool ok;
    bs300_i2c_set_speed(BS300_I2C_DELAY_NORMAL);
    ok = bs300_send_simple_cmd(BS300_CMD_ACTIVE);
    if (ok) bs300_i2c_set_speed(BS300_I2C_DELAY_ACTIVE);
    return ok ? 0 : -1;
}

int bs300_is_connected(void)
{
    return bs300_send_simple_cmd(BS300_CMD_IS_CONNECT) ? 0 : -1;
}

/* ================================================================
 *  Resync diff (blocking) — compare s_dsp_state vs target struct
 * ================================================================ */
#define DIFF_SEND(sess, cmd, fn_call) do { \
    memset(data, 0, 48); \
    ret = (fn_call); \
    if (ret == 0) { \
        if (sess) bs300_session_append(sess, cmd, data); \
        else { if (bs300_advanced_write(cmd, data)) sent++; else fail++; } \
    } \
} while(0)

int bs300_resync_diff(bs300_prog_struct_t *_new)
{
    bs300_calib_t calib;
    uint8_t old_it, new_it, igd_changed;
    uint8_t data[48];
    int sent = 0, fail = 0;

    if (_new == NULL) return -1;
    if (load_calib(&calib) < 0) return -1;

    old_it = get_input_type(s_dsp_state.modules.input_selection, s_dsp_state.modules.mm_type);
    new_it = get_input_type(_new->modules.input_selection, _new->modules.mm_type);
    igd_changed = (old_it != new_it);

    PRINTF("[BS300] resync diff prog=%d\r\n", s_cur_prog);

    switch_diff_pre_enr(&_new->modules, &calib, new_it, igd_changed,
                        NULL, &sent, &fail, data);
    switch_diff_enr(&_new->enr, &calib, new_it, igd_changed,
                    NULL, &sent, &fail, data);
    switch_diff_post_enr(&_new->modules, &calib, new_it, igd_changed,
                         NULL, &sent, &fail, data);
    switch_diff_vol_beep(&_new->modules, &calib, igd_changed,
                         NULL, &sent, &fail, data);
    switch_diff_wdrc(&_new->wdrc, &_new->modules,
                     &calib, new_it, igd_changed,
                     NULL, &sent, &fail, data);

    memcpy(&s_dsp_state, _new, sizeof(s_dsp_state));

    PRINTF("[BS300] resync_diff done, sent=%d fail=%d\r\n", sent, fail);
    return (fail > 0) ? -1 : 0;
}

int bs300_param_modify(uint8_t prog_idx, uint16_t offset,
                       const uint8_t *val, uint8_t len)
{
    bs300_calib_t calib;
    bs300_prog_struct_t new_prog;
    uint8_t data[48];
    uint8_t old_it, new_it, igd_changed;
    uint8_t *raw;
    uint8_t i;
    int sent = 0, fail = 0;

    if (val == NULL || prog_idx >= 4) return -1;
    if (offset + len > sizeof(bs300_prog_struct_t)) return -1;

    if (prog_idx == s_cur_prog) {
        new_prog = s_dsp_state;
    } else {
        if (load_struct(prog_idx, &new_prog) < 0) return -1;
    }

    raw = (uint8_t *)&new_prog;
    for (i = 0; i < len; i++) raw[offset + i] = val[i];

    if (load_calib(&calib) < 0) return -1;
    old_it = get_input_type(s_dsp_state.modules.input_selection, s_dsp_state.modules.mm_type);
    new_it = get_input_type(new_prog.modules.input_selection, new_prog.modules.mm_type);
    igd_changed = (old_it != new_it);

    switch_diff_pre_enr(&new_prog.modules, &calib, new_it, igd_changed,
                        NULL, &sent, &fail, data);
    switch_diff_enr(&new_prog.enr, &calib, new_it, igd_changed,
                    NULL, &sent, &fail, data);
    switch_diff_post_enr(&new_prog.modules, &calib, new_it, igd_changed,
                         NULL, &sent, &fail, data);
    switch_diff_vol_beep(&new_prog.modules, &calib, igd_changed,
                         NULL, &sent, &fail, data);
    switch_diff_wdrc(&new_prog.wdrc, &new_prog.modules,
                     &calib, new_it, igd_changed,
                     NULL, &sent, &fail, data);

    if (prog_idx == s_cur_prog) {
        memcpy(&s_dsp_state, &new_prog, sizeof(s_dsp_state));
    }

    return (fail > 0) ? -1 : 0;
}

#undef DIFF_SEND

/* ================================================================
 *  Async session (single static session, timer-driven)
 * ================================================================ */
static bs300_sync_session_t g_bs300_sync;
static void (*g_bs300_sync_on_done)(void) = NULL;
static int8_t  s_pending_switch = -1;          /* deferred switch target */
static void (*s_pending_switch_on_done)(void) = NULL;
static int8_t  s_pending_volume = -1;          /* deferred volume level */
static void (*s_pending_volume_cb)(void) = NULL;

static int reencode_bin_gain_async_core(void (*on_done)(void), uint32_t tone_cmd);

int bs300_sync_is_busy(void)
{
    return (g_bs300_sync.state != BS300_SYNC_IDLE
            && g_bs300_sync.state != BS300_SYNC_DONE
            && g_bs300_sync.state != BS300_SYNC_ERROR);
}

void bs300_sync_timer_handler(void)
{
    uint16_t delay;

    if (!bs300_sync_tick(&g_bs300_sync)) {
        void (*cb)(void) = g_bs300_sync_on_done;
        g_bs300_sync_on_done = NULL;

        bool session_ended = (g_bs300_sync.state == BS300_SYNC_DONE
                              || g_bs300_sync.state == BS300_SYNC_ERROR
                              || g_bs300_sync.state == BS300_SYNC_IDLE);

        if (g_bs300_sync.state == BS300_SYNC_DONE) {
            /* Session complete — s_dsp_state has been updated per-command.
             * Copy final state = s_target (full sync guarantee). */
            if (g_bs300_sync.target != NULL && g_bs300_sync.dsp_state != NULL) {
                memcpy(g_bs300_sync.dsp_state, g_bs300_sync.target,
                       sizeof(bs300_prog_struct_t));
            }
            /* Restore volume if it was changed during the session */
            if (s_pending_volume >= 0 && g_bs300_sync.dsp_state != NULL) {
                g_bs300_sync.dsp_state->modules.volume_level
                    = (uint8_t)s_pending_volume;
            }
        }

        if (session_ended) {
            if (cb) cb();
            /* Deferred operations executed from Main_Loop via
             * bs300_process_deferred() — avoids stack overflow
             * from flash-erase stack frames inside timer handler. */
        }
        return;
    }

    delay = (g_bs300_sync.state == BS300_SYNC_POLL) ? 6 : 2;
    ke_timer_set(BS300_SYNC_TIMER, TASK_APP, delay);
}

/* Called from Main_Loop — executes deferred switch/volume that were
 * queued while a session was in progress.  Must NOT be called from
 * timer handler (save_settings allocates ~960B stack for flash ops). */
void bs300_process_deferred(void)
{
    if (bs300_sync_is_busy()) return;

    if (s_pending_switch >= 0) {
        int8_t next = s_pending_switch;
        void (*next_cb)(void) = s_pending_switch_on_done;
        s_pending_switch = -1;
        s_pending_switch_on_done = NULL;
        PRINTF("[BS300] deferred switch: running prog=%d\r\n", next);
        bs300_switch_program_async((uint8_t)next, next_cb);
        PRINTF("[BS300] deferred switch: done\r\n");
        return;
    }
    if (s_pending_volume >= 0) {
        uint8_t vol = (uint8_t)s_pending_volume;
        void (*vol_cb)(void) = s_pending_volume_cb;
        s_pending_volume = -1;
        s_pending_volume_cb = NULL;
        reencode_bin_gain_async_core(vol_cb,
            (vol == 0) ? BS300_TONE_VOL_0 : BS300_TONE_VOL_OTHER);
    }
}

static int start_async_session(void (*on_done)(void))
{
    g_bs300_sync_on_done = on_done;
    g_bs300_sync.state = BS300_SYNC_SEND;
    ke_timer_set(BS300_SYNC_TIMER, TASK_APP, 2);
    return 0;
}

int bs300_switch_program_async(uint8_t new_prog_idx, void (*on_done)(void))
{
    if (new_prog_idx >= 4) return -1;
    if (s_cur_prog == new_prog_idx) return 0;
    bs300_print_settings();

    /* Busy → abort current session and queue the new switch.
     * Must NOT call bs300_sync_session_init here because it would
     * clobber abort_requested and leave the I2C hardware in an
     * unknown state. */
    if (bs300_sync_is_busy()) {
        g_bs300_sync.abort_requested = true;
        s_pending_switch = (int8_t)new_prog_idx;
        s_pending_switch_on_done = on_done;
        PRINTF("[BS300] switch_async deferred (busy), prog=%d\r\n",
               new_prog_idx);
        return 0;
    }

    /* Load target into static s_target for diff + per-command apply */
    if (load_struct(new_prog_idx, &s_target) < 0) return -1;
    s_target.modules.volume_level = s_volumes[new_prog_idx];

    s_cur_prog = new_prog_idx;

    /* Defer flash write — Flash_EraseSector takes ~40ms and may
     * stall BLE timing / trigger watchdog during connected state.
     * Settings are persisted on BLE disconnect instead. */

    PRINTF("[BS300] switch RAM async %d->%d\r\n",
           s_cur_prog, new_prog_idx);

    bs300_sync_session_init(&g_bs300_sync);
    g_bs300_sync.dsp_state = &s_dsp_state;
    g_bs300_sync.target = &s_target;

    /* Prompt tone FIRST */
    {
        uint8_t tone_data[48];
        memset(tone_data, 0, 48);
        bs300_session_append(&g_bs300_sync, bs300_tone_for_program(new_prog_idx),
                             tone_data);
    }

    if (build_diff_session(&g_bs300_sync) < 0) return -1;

    return start_async_session(on_done);
}

int bs300_resync_diff_async(bs300_prog_struct_t *_new, void (*on_done)(void))
{
    if (bs300_sync_is_busy()) return -1;

    memcpy(&s_target, _new, sizeof(s_target));

    bs300_sync_session_init(&g_bs300_sync);
    g_bs300_sync.dsp_state = &s_dsp_state;
    g_bs300_sync.target = &s_target;

    if (bs300_resync_diff_start(&g_bs300_sync, _new) < 0) return -1;

    return start_async_session(on_done);
}

int bs300_param_modify_async(uint8_t prog_idx, uint16_t offset,
                              const uint8_t *val, uint8_t len)
{
    if (bs300_sync_is_busy()) return -1;

    bs300_sync_session_init(&g_bs300_sync);
    g_bs300_sync.dsp_state = &s_dsp_state;
    g_bs300_sync.target = &s_target;

    if (bs300_param_modify_start(&g_bs300_sync, prog_idx, offset, val, len) < 0)
        return -1;

    return start_async_session(NULL);
}

static int reencode_bin_gain_async_core(void (*on_done)(void), uint32_t tone_cmd)
{
    uint8_t data[48];
    bs300_calib_t calib;
    uint8_t input_type;
    int ret;

    if (load_calib(&calib) < 0) return -1;
    input_type = get_input_type(s_dsp_state.modules.input_selection, s_dsp_state.modules.mm_type);

    bs300_sync_session_init(&g_bs300_sync);
    g_bs300_sync_on_done = on_done;
    g_bs300_sync.dsp_state = &s_dsp_state;
    g_bs300_sync.target = &s_dsp_state;  /* apply is no-op for self */

    /* Prompt tone FIRST (if requested) */
    if (tone_cmd != 0) {
        memset(data, 0, 48);
        bs300_session_append(&g_bs300_sync, tone_cmd, data);
    }

    /* Bin Gain */
    memset(data, 0, 48);
    ret = bs300_encode_wdrc_bin_gain(&s_dsp_state.wdrc, &calib,
                                      &s_dsp_state.modules,
                                      input_type, data);
    if (ret < 0) return -1;

    bs300_session_append(&g_bs300_sync, 0x8060B2, data);
    return start_async_session(on_done);
}

int bs300_set_volume_async(uint8_t level, void (*on_done)(void))
{
    if (level > 9) return -1;
    bs300_print_settings();

    /* Update in-memory state. Flash persist deferred to BLE disconnect. */
    s_dsp_state.modules.volume_level = level;
    s_volumes[s_cur_prog] = level;

    if (bs300_sync_is_busy()) {
        s_pending_volume = (int8_t)level;
        s_pending_volume_cb = on_done;
        PRINTF("[BS300] volume=%d state saved, I2C deferred\r\n", level);
        return 0;
    }

    return reencode_bin_gain_async_core(on_done,
               (level == 0) ? BS300_TONE_VOL_0 : BS300_TONE_VOL_OTHER);
}

/* Same as bs300_set_volume_async but without prompt tone — for app-initiated
 * volume changes where the app already provides its own feedback. */
int bs300_set_volume_notone_async(uint8_t level, void (*on_done)(void))
{
    if (level > 9) return -1;
    bs300_print_settings();

    s_dsp_state.modules.volume_level = level;
    s_volumes[s_cur_prog] = level;

    if (bs300_sync_is_busy()) {
        s_pending_volume = (int8_t)level;
        s_pending_volume_cb = on_done;
        return 0;
    }

    return reencode_bin_gain_async_core(on_done, 0);
}

void bs300_async_done_callback(void)
{
    /* No pending queue — abort mechanism eliminates the need */
}

int bs300_set_eq_async(int8_t low, int8_t mid, int8_t high,
                        void (*on_done)(void))
{
    if (bs300_sync_is_busy()) return -1;
    bs300_print_settings();

    s_dsp_state.modules.eq_low  = low;
    s_dsp_state.modules.eq_mid  = mid;
    s_dsp_state.modules.eq_high = high;
    s_eq_low[s_cur_prog]  = low;
    s_eq_mid[s_cur_prog]  = mid;
    s_eq_high[s_cur_prog] = high;
    save_settings();
    return reencode_bin_gain_async_core(on_done, 0);  /* no tone for EQ */
}

/* ================================================================
 *  Dirty sync
 * ================================================================ */
static bool s_need_sync = false;

void bs300_sync_dirty(void)
{
    s_need_sync = true;
    if (!bs300_sync_is_busy()) {
        s_need_sync = false;
        reencode_bin_gain_async_core(NULL, 0);
    }
}

int bs300_vol_commit(uint8_t level)
{
    if (level > 9) return -1;
    s_dsp_state.modules.volume_level = level;
    s_volumes[s_cur_prog] = level;
    save_settings();
    bs300_sync_dirty();
    return 0;
}

/* ================================================================
 *  Async fill-mode functions
 * ================================================================ */
int bs300_switch_program_start(bs300_sync_session_t *s, uint8_t new_prog_idx)
{
    if (s == NULL || new_prog_idx >= 4) return -1;
    if (s_cur_prog == new_prog_idx) return 0;

    if (load_struct(new_prog_idx, &s_target) < 0) return -1;
    s_target.modules.volume_level = s_volumes[new_prog_idx];
    s_target.modules.eq_low  = s_eq_low[new_prog_idx];
    s_target.modules.eq_mid  = s_eq_mid[new_prog_idx];
    s_target.modules.eq_high = s_eq_high[new_prog_idx];

    s_cur_prog = new_prog_idx;
    save_settings();

    s->dsp_state = &s_dsp_state;
    s->target = &s_target;

    PRINTF("[BS300] switch RAM async %d->%d\r\n", s_cur_prog, new_prog_idx);

    /* Prompt tone FIRST */
    {
        uint8_t tone_data[48];
        memset(tone_data, 0, 48);
        bs300_session_append(s, bs300_tone_for_program(new_prog_idx), tone_data);
    }

    s->state = BS300_SYNC_SEND;
    return build_diff_session(s);
}

int bs300_resync_diff_start(bs300_sync_session_t *s, bs300_prog_struct_t *_new)
{
    bs300_calib_t calib;
    uint8_t old_it, new_it, igd_changed;
    uint8_t data[48];
    int sent = 0, fail = 0;

    if (s == NULL || _new == NULL) return -1;

    memcpy(&s_target, _new, sizeof(s_target));
    s->dsp_state = &s_dsp_state;
    s->target = &s_target;

    if (load_calib(&calib) < 0) return -1;
    old_it = get_input_type(s_dsp_state.modules.input_selection, s_dsp_state.modules.mm_type);
    new_it = get_input_type(_new->modules.input_selection, _new->modules.mm_type);
    igd_changed = (old_it != new_it);

    PRINTF("[BS300] resync diff async prog=%d\r\n", s_cur_prog);

    switch_diff_pre_enr(&_new->modules, &calib, new_it, igd_changed,
                        s, &sent, &fail, data);
    switch_diff_enr(&_new->enr, &calib, new_it, igd_changed,
                    s, &sent, &fail, data);
    switch_diff_post_enr(&_new->modules, &calib, new_it, igd_changed,
                         s, &sent, &fail, data);
    switch_diff_vol_beep(&_new->modules, &calib, igd_changed,
                         s, &sent, &fail, data);
    switch_diff_wdrc(&_new->wdrc, &_new->modules,
                     &calib, new_it, igd_changed,
                     s, &sent, &fail, data);

    s->state = BS300_SYNC_SEND;
    return (fail > 0) ? -1 : 0;
}

int bs300_param_modify_start(bs300_sync_session_t *s, uint8_t prog_idx,
                              uint16_t offset, const uint8_t *val, uint8_t len)
{
    bs300_calib_t calib;
    bs300_prog_struct_t new_prog;
    uint8_t data[48];
    uint8_t old_it, new_it, igd_changed;
    uint8_t *raw;
    uint8_t i;
    int sent = 0, fail = 0;

    if (s == NULL || val == NULL || prog_idx >= 4) return -1;
    if (offset + len > sizeof(bs300_prog_struct_t)) return -1;

    if (prog_idx == s_cur_prog) {
        new_prog = s_dsp_state;
    } else {
        if (load_struct(prog_idx, &new_prog) < 0) return -1;
    }

    raw = (uint8_t *)&new_prog;
    for (i = 0; i < len; i++) raw[offset + i] = val[i];

    memcpy(&s_target, &new_prog, sizeof(s_target));
    s->dsp_state = &s_dsp_state;
    s->target = &s_target;

    if (load_calib(&calib) < 0) return -1;
    old_it = get_input_type(s_dsp_state.modules.input_selection, s_dsp_state.modules.mm_type);
    new_it = get_input_type(new_prog.modules.input_selection, new_prog.modules.mm_type);
    igd_changed = (old_it != new_it);

    switch_diff_pre_enr(&new_prog.modules, &calib, new_it, igd_changed,
                        s, &sent, &fail, data);
    switch_diff_enr(&new_prog.enr, &calib, new_it, igd_changed,
                    s, &sent, &fail, data);
    switch_diff_post_enr(&new_prog.modules, &calib, new_it, igd_changed,
                         s, &sent, &fail, data);
    switch_diff_vol_beep(&new_prog.modules, &calib, igd_changed,
                         s, &sent, &fail, data);
    switch_diff_wdrc(&new_prog.wdrc, &new_prog.modules,
                     &calib, new_it, igd_changed,
                     s, &sent, &fail, data);

    s->state = BS300_SYNC_SEND;
    return 0;
}

void bs300_play_prompt_tone(uint8_t program, uint8_t volume)
{
    static uint8_t last_program = 0xFF;
    static uint8_t last_volume  = 0xFF;
    static uint8_t inited       = 0;
    uint32_t cmd = 0;
    uint8_t data[48];

    if (inited && last_program == program && last_volume == volume)
        return;

    memset(data, 0, sizeof(data));

    if (!inited || last_program != program) {
        switch (program) {
        case 0: cmd = BS300_TONE_MODE_0; break;
        case 1: cmd = BS300_TONE_MODE_1; break;
        case 2: cmd = BS300_TONE_MODE_2; break;
        case 3: cmd = BS300_TONE_MODE_3; break;
        default: return;
        }
    } else if (last_volume != volume) {
        cmd = (volume == 0) ? BS300_TONE_VOL_0 : BS300_TONE_VOL_OTHER;
    }

    last_program = program;
    last_volume  = volume;
    inited       = 1;

    raw_write_packet(cmd, data);
}
