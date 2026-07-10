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
 *  Boot-time cache + shared work buffer
 * ================================================================ */
static uint8_t          s_active_prog;
static bs300_calib_t    s_calib_cache;
static uint8_t          s_prog_input[4];
static bs300_modules_t  s_prog_modules[4];
static bs300_enr_t      s_prog_enr[4];
static bool             s_boot_cached;
static uint8_t          s_work[480];        /* shared raw buffer */

uint8_t bs300_get_prog_input(uint8_t prog_idx)
{
    if (prog_idx < 4) return s_prog_input[prog_idx];
    return 0;
}

bool bs300_is_boot_cached(void) { return s_boot_cached; }
uint8_t bs300_get_active_prog(void) { return s_active_prog; }
uint8_t bs300_get_module_volume(uint8_t prog_idx)
{
    if (prog_idx > 3) return 9;
    return s_prog_modules[prog_idx].volume_level;
}

void bs300_restore_settings(uint8_t active_prog, const uint8_t *volume)
{
    uint8_t i;

    s_active_prog = active_prog;

    if (volume == NULL) return;

    /* Update in-memory volume cache — settings sector is the authoritative source */
    for (i = 0; i < 4; i++) {
        s_prog_modules[i].volume_level = volume[i];
    }

    PRINTF("[BS300] settings restored prog=%u\r\n", active_prog);
}

/* Save current active program and all volume levels to settings sector */
static void save_settings(void)
{
    uint8_t vols[4];
    uint8_t i;
    for (i = 0; i < 4; i++) vols[i] = s_prog_modules[i].volume_level;
    bs300_settings_save(s_active_prog, vols);
}

void bs300_cache_prog_inputs(void)
{
    bs300_prog_struct_t prog;
    uint8_t i;

    for (i = 0; i < 4; i++) {
        bs300_storage_load_program(i, s_work);
        if (bs300_flash_to_struct(s_work, &prog) == 0) {
            s_prog_input[i]   = prog.modules.input_selection;
            s_prog_modules[i] = prog.modules;
            s_prog_enr[i]     = prog.enr;
        }
    }
    if (bs300_read_calibration(s_work)) {
        bs300_parse_calibration(s_work, &s_calib_cache);
        s_boot_cached = true;
    }
}

void bs300_on_active_prog_changed(uint8_t new_prog_idx)
{
    bs300_prog_struct_t prog;

    s_active_prog = new_prog_idx;
    bs300_storage_load_program(new_prog_idx, s_work);
    if (bs300_flash_to_struct(s_work, &prog) == 0) {
        s_prog_input[new_prog_idx]   = prog.modules.input_selection;
        s_prog_modules[new_prog_idx] = prog.modules;
        s_prog_enr[new_prog_idx]     = prog.enr;
    }
}

/* ================================================================
 *  Struct load/save (Main Flash-backed: raw 480B → flash_to_struct)
 * ================================================================ */
static int load_struct(uint8_t prog_idx, bs300_prog_struct_t *out)
{
    bs300_storage_load_program(prog_idx, s_work);
    return bs300_flash_to_struct(s_work, out);
}

const bs300_calib_t *bs300_get_cached_calib(void)
{
    return s_boot_cached ? &s_calib_cache : NULL;
}

static int load_calib(bs300_calib_t *calib)
{
    if (s_boot_cached) {
        *calib = s_calib_cache;  /* use cached copy, skip I2C */
        return 0;
    }
    if (!bs300_read_calibration(s_work)) return -1;
    return bs300_parse_calibration(s_work, calib);
}

/* ================================================================
 *  Sync session
 * ================================================================ */
/* ================================================================
 *  Frame checksum helper (matches protocol: 0xFF - sum_of_bytes)
 * ================================================================ */
static uint8_t calc_checksum(const uint8_t *buf, int len)
{
    uint16_t sum = 0;
    int i;
    for (i = 0; i < len; i++) sum += buf[i];
    return (uint8_t)(0xFF - (sum & 0xFF));
}

/* ================================================================
 *  Raw I2C frame send (no poll) — matches bs300_send_advanced_write
 *  but returns immediately after I2C bus transaction.
 * ================================================================ */
static int raw_write_packet(uint32_t cmd, const uint8_t *data)
{
    uint8_t frame[53];
    int i;

    frame[0] = 0x10;  /* Len: has data */
    frame[1] = (uint8_t)(cmd & 0xFF);
    frame[2] = (uint8_t)((cmd >> 8) & 0xFF);
    frame[3] = (uint8_t)((cmd >> 16) & 0xFF);
    for (i = 0; i < 48; i++) frame[4 + i] = data[i];
    frame[52] = calc_checksum(frame, 52);

    PRINTF("[BS300] ASYNC CMD=0x%06lX:", (unsigned long)cmd);
    for (i = 0; i < 53; i++) PRINTF(" %02X", frame[i]);
    PRINTF("\r\n");

    return bs300_i2c_write(BS300_I2C_ADDR, frame, 53) ? 0 : -1;
}

/* ================================================================
 *  Poll FURPROC — matches bs300_poll_ready pattern
 * ================================================================ */
static int poll_furproc(void)
{
    uint8_t req[2], resp[4];
    uint8_t chk;

    req[0] = 0x80;  /* Read Request, no data */
    req[1] = (uint8_t)(0xFF - req[0]);
    if (!bs300_i2c_write(BS300_I2C_ADDR, req, 2)) return -1;

    if (!bs300_i2c_read(BS300_I2C_ADDR, resp, 4)) return -1;

    chk = (uint8_t)(0xFF - ((resp[0] + resp[1] + resp[2]) & 0xFF));
    if (chk != resp[3]) return -1;

    if (resp[2] & 0x80) return 1;  /* busy */
    return 0;  /* ready */
}

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
                PRINTF("[BS300] sync poll err cmd=0x%06lX\r\n",
                       (unsigned long)s->cmds[s->cmd_index]);
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

        s->retry_count++;
        if (s->retry_count >= 30) {
            s->fail_count++;
            s->state = BS300_SYNC_ERROR;
            PRINTF("[BS300] sync poll timeout cmd=0x%06lX\r\n",
                   (unsigned long)s->cmds[s->cmd_index]);
            return 0;
        }
        s->state = BS300_SYNC_SEND;
        return 1;
    }

    return 0;
}

/* ================================================================
 *  Dynamic encode sync (sends all 31 modules via I2C)
 * ================================================================ */
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
        { uint8_t _j; PRINTF("[BS300] CMD 0x%06lX:", (unsigned long)(cmd)); \
          for (_j=0;_j<48;_j++) PRINTF(" %02X", data[_j]); PRINTF("\r\n"); } \
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

    /* Input source */
    SEND_CMD(0x800022, bs300_encode_ddm2(&prog->modules, calib, data));
    SEND_CMD(0x800062, bs300_encode_mm_plus(&prog->modules, calib, input_type, data));
    SEND_CMD(0x800052, bs300_encode_dfbc(&prog->modules, calib, data));

    /* ENR */
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

    /* NoiseGen2, TC/DAI, ISS */
    memset(data, 0, 48);
    if (session) bs300_session_append(session, 0x800172, data);
    else { if (bs300_advanced_write(0x800172, data)) sent++; else fail++; }
    SEND_CMD(0x804272, bs300_encode_tc_dai(calib, input_type, data));
    SEND_CMD(0x8001B2, bs300_encode_iss(&prog->modules, calib, input_type, data));

    /* WNR */
    SEND_CMD(0x8001C2, bs300_encode_wnr_setup(&prog->modules, calib, data));
    SEND_CMD(0x8011C2, bs300_encode_wnr_band_0_15(&prog->modules, calib, input_type, data));
    SEND_CMD(0x8411C2, bs300_encode_wnr_band_16_31(&prog->modules, calib, input_type, data));
    SEND_CMD(0x8021C2, bs300_encode_wnr_single_mic(&prog->modules, calib, input_type, data));

    SEND_CMD(0x800382, bs300_encode_agco(&prog->modules, data));
    SEND_CMD(0x800081, bs300_encode_volume_beep(&prog->modules, calib, data));

    /* WDRC */
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

static uint8_t get_input_type(uint8_t input_selection)
{
    switch (input_selection) {
    case 2: return 1;  /* Telecoil */
    case 3: return 2;  /* DAI */
    default: return 0; /* Mic */
    }
}

/* ================================================================
 *  Inner sync dispatcher
 * ================================================================ */
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

    input_type = get_input_type(prog->modules.input_selection);

    PRINTF("[BS300] sync_program ch=%d kp=%d lim=%d enr=0x%02x input=%d\r\n",
           prog->wdrc.total_channels, prog->wdrc.kp_mode,
           prog->wdrc.limiter, prog->enr.enable_num_ch,
           prog->modules.input_selection);
    PRINTF("[BS300] CALIB mic2_gd=%d mic_delay=%d tc_gd=%d dai_gd=%d fbc_bd=%d\r\n",
           calib.mic2_gain_diff, calib.mic_delay,
           calib.telecoil_gain_diff, calib.dai_gain_diff,
           calib.fbc_bulk_delay);

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
 * ================================================================ */

#define SEND_IF_DIRTY(session, cmd, fn_call) do { \
    memset(data, 0, 48); \
    ret = (fn_call); \
    if (ret == 0) { \
        if (session) { \
            bs300_session_append(session, cmd, data); \
        } else { \
            if (bs300_advanced_write(cmd, data)) (*sent)++; else (*fail)++; \
        } \
    } \
} while(0)

static void switch_diff_wdrc(const bs300_wdrc_t *ow, const bs300_wdrc_t *nw,
                              const bs300_modules_t *om, const bs300_modules_t *nm,
                              const bs300_calib_t *calib,
                              uint8_t new_it, int igd_changed,
                              bs300_sync_session_t *session,
                              int *sent, int *fail, uint8_t *data)
{
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
        /* both disabled, skip */
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

static void switch_diff_vol_beep(const bs300_modules_t *om,
                                  const bs300_modules_t *nm,
                                  const bs300_calib_t *calib,
                                  int igd_changed,
                                  bs300_sync_session_t *session,
                                  int *sent, int *fail, uint8_t *data)
{
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

static void switch_diff_enr(const bs300_enr_t *oe, const bs300_enr_t *ne,
                             const bs300_calib_t *calib,
                             uint8_t new_it, int igd_changed,
                             bs300_sync_session_t *session,
                             int *sent, int *fail, uint8_t *data)
{
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
        if (snr_changed || ma_changed || etr_changed)
            SEND_IF_DIRTY(session, 0x8070C2, bs300_encode_enr_etr(ne, data));
        if (snr_changed || ma_changed || nrr_changed)
            SEND_IF_DIRTY(session, 0x8080C2, bs300_encode_enr_nrr(ne, data));
    }
}

static void switch_diff_pre_enr(const bs300_modules_t *om, bs300_modules_t *nm,
                                 const bs300_calib_t *calib,
                                 uint8_t new_it, int igd_changed,
                                 bs300_sync_session_t *session,
                                 int *sent, int *fail, uint8_t *data)
{
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

static void switch_diff_post_enr(const bs300_modules_t *om, bs300_modules_t *nm,
                                  const bs300_calib_t *calib,
                                  uint8_t new_it, int igd_changed,
                                  bs300_sync_session_t *session,
                                  int *sent, int *fail, uint8_t *data)
{
    int ret;

    /* TC/DAI */
    {
        uint8_t o_tc = (om->input_selection == 2 || om->input_selection == 3) ? 1 : 0;
        uint8_t n_tc = (nm->input_selection == 2 || nm->input_selection == 3) ? 1 : 0;
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
 *  Program switch (blocking)
 * ================================================================ */
int bs300_switch_program(uint8_t new_prog_idx)
{
    uint8_t data[48];
    bs300_calib_t calib;
    bs300_prog_struct_t old_prog, new_prog;
    uint8_t active_prog = s_active_prog;
    uint8_t old_it, new_it;
    int igd_changed;
    int sent = 0, fail = 0;
    int ret;

    if (new_prog_idx >= 4) return -1;
    if (active_prog == new_prog_idx) return 0;

    ret = load_struct(active_prog, &old_prog);
    if (ret < 0) {
        ret = load_struct(new_prog_idx, &new_prog);
        if (ret < 0) return -1;
        s_active_prog = new_prog_idx;
        return bs300_sync_program(&new_prog);
    }
    ret = load_struct(new_prog_idx, &new_prog);
    if (ret < 0) return -1;

    s_active_prog = new_prog_idx;
    bs300_on_active_prog_changed(new_prog_idx);
    save_settings();

    PRINTF("[BS300] switch RAM %d->%d\r\n", active_prog, new_prog_idx);

    if (load_calib(&calib) < 0) return -1;

    old_it = get_input_type(old_prog.modules.input_selection);
    new_it = get_input_type(new_prog.modules.input_selection);
    igd_changed = (old_it != new_it);

    switch_diff_pre_enr(&old_prog.modules, &new_prog.modules,
                        &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_enr(&old_prog.enr, &new_prog.enr,
                    &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_post_enr(&old_prog.modules, &new_prog.modules,
                         &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_vol_beep(&old_prog.modules, &new_prog.modules,
                         &calib, igd_changed, NULL, &sent, &fail, data);
    switch_diff_wdrc(&old_prog.wdrc, &new_prog.wdrc,
                     &old_prog.modules, &new_prog.modules,
                     &calib, new_it, igd_changed, NULL, &sent, &fail, data);

    /* Force DDM2/MM+ if new prog uses them */
    if (new_prog.modules.input_selection == 5) {
        memset(data, 0, 48);
        bs300_encode_ddm2(&new_prog.modules, &calib, data);
        if (bs300_advanced_write(0x800022, data)) sent++; else fail++;
    }
    if (new_prog.modules.input_selection == 4) {
        memset(data, 0, 48);
        bs300_encode_mm_plus(&new_prog.modules, &calib, new_it, data);
        if (bs300_advanced_write(0x800062, data)) sent++; else fail++;
    }
    /* Force Vol/Beep */
    memset(data, 0, 48);
    if (bs300_encode_volume_beep(&new_prog.modules, &calib, data) == 0) {
        if (bs300_advanced_write(0x800081, data)) sent++; else fail++;
    }

    PRINTF("[BS300] switch_program done, sent=%d fail=%d\r\n", sent, fail);
    return (fail > 0) ? -1 : 0;
}

/* ================================================================
 *  Voice prompt input switch
 * ================================================================ */
uint8_t bs300_voice_prompt_input_switch(uint8_t target_input)
{
    uint8_t data[48];
    uint8_t vb_data[48];
    bs300_modules_t mod_tmp;
    uint8_t original_input;
    int ret;

    if (!s_boot_cached) return 0xFF;

    original_input = s_prog_input[s_active_prog];
    if (original_input == target_input) return original_input;

    mod_tmp = s_prog_modules[s_active_prog];
    mod_tmp.input_selection = target_input;
    ret = bs300_encode_volume_beep(&mod_tmp, &s_calib_cache, vb_data);
    if (ret < 0) return 0xFF;

    ret = bs300_mute();
    if (ret < 0) return 0xFF;

    /* Disable DDM2/MM+ if original input uses them */
    if (original_input == 5) {
        memset(data, 0, 48);
        bs300_advanced_write(0x800022, data);
    }
    if (original_input == 4) {
        memset(data, 0, 48);
        bs300_advanced_write(0x800062, data);
    }

    /* Disable ENR */
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

    mod_tmp = s_prog_modules[s_active_prog];
    mod_tmp.input_selection = original_input;
    ret = bs300_encode_volume_beep(&mod_tmp, &s_calib_cache, vb_data);
    if (ret < 0) return -1;

    bs300_mute();

    /* Re-enable DDM2/MM+ if original input uses them */
    if (original_input == 5) {
        bs300_encode_ddm2(&mod_tmp, &s_calib_cache, data);
        bs300_advanced_write(0x800022, data);
    }
    if (original_input == 4) {
        bs300_encode_mm_plus(&mod_tmp, &s_calib_cache, 0, data);
        bs300_advanced_write(0x800062, data);
    }

    /* Re-enable ENR */
    bs300_encode_enr_general(&s_prog_enr[s_active_prog], data);
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
    bs300_prog_struct_t prog;

    if (level > 9) return -1;
    if (load_struct(s_active_prog, &prog) < 0) return -1;
    if (load_calib(&calib) < 0) return -1;

    prog.modules.volume_level = level;
    return bs300_encode_wdrc_bin_gain(&prog.wdrc, &calib, &prog.modules,
                                       get_input_type(prog.modules.input_selection),
                                       data) == 0
           && bs300_advanced_write(0x8060B2, data);
}

int bs300_set_eq(int8_t low, int8_t mid, int8_t high)
{
    uint8_t data[48];
    bs300_calib_t calib;
    bs300_prog_struct_t prog;

    if (load_struct(s_active_prog, &prog) < 0) return -1;
    if (load_calib(&calib) < 0) return -1;

    prog.modules.eq_low  = low;
    prog.modules.eq_mid  = mid;
    prog.modules.eq_high = high;
    return bs300_encode_wdrc_bin_gain(&prog.wdrc, &calib, &prog.modules,
                                       get_input_type(prog.modules.input_selection),
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

    if (!bs300_advanced_write(0x8001E2, data)) {
        PRINTF("[BS300] itg_write fail\r\n");
        return -1;
    }
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

    /* Disable all non-WDRC modules */
    memset(data, 0, 48);
    bs300_advanced_write(0x8000C2, data);  /* ENR */
    bs300_advanced_write(0x800022, data);  /* DDM2 */
    bs300_advanced_write(0x800062, data);  /* MM+ */
    bs300_advanced_write(0x800052, data);  /* DFBC */
    bs300_advanced_write(0x800172, data);  /* NoiseGen2 */
    bs300_advanced_write(0x8001B2, data);  /* ISS */
    bs300_advanced_write(0x8001C2, data);  /* WNR */
    bs300_advanced_write(0x800382, data);  /* AGCO */

    /* WDRC General */
    memset(data, 0, 48);
    data[0]  = 0x01;
    data[3]  = 0x10;
    data[9]  = 0x10;
    data[12] = 0x03;
    bs300_advanced_write(0x8000B2, data);

    /* WDRC Freq Spacing */
    memset(data, 0, 48);
    for (i = 0; i < 16; i++) {
        data[i * 3 + 0] = 0x41;
        data[i * 3 + 1] = 0x10;
        data[i * 3 + 2] = 0x04;
    }
    bs300_advanced_write(0x8010B2, data);

    /* WDRC KP Threshold */
    memset(data, 0, 48);
    {
        static const uint8_t kp_th[32] = {
            0xA5,0xA5,0xA6,0xA6,0xA6,0xA6,0xA5,0xA5,
            0xA5,0xA5,0xA2,0xA2,0xA1,0xA1,0xA5,0xA5,
            0xA4,0xA4,0xA6,0xA6,0xA4,0xA4,0xA4,0xA4,
            0xA5,0xA5,0xA3,0xA3,0xA3,0xA3,0xA3,0xA3,
        };
        for (i = 0; i < 32; i++) data[i] = kp_th[i];
    }
    bs300_advanced_write(0x8020B2, data);

    /* WDRC Ratio */
    memset(data, 0x20, 47);
    data[47] = 20;
    bs300_advanced_write(0x8050B2, data);

    /* WDRC Bin Gain */
    memset(data, 0, 48);
    {
        static const uint8_t bin_gain[32] = {
            0xFB,0x09,0x09,0x09,0x09,0x0D,0xFF,0x0A,
            0x08,0x0B,0xFE,0x0F,0x0B,0x0A,0xF9,0x08,
            0x0E,0x0D,0x04,0x09,0x0B,0x0A,0x0A,0x15,
            0x0F,0x15,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,
        };
        for (i = 0; i < 32; i++) data[i] = bin_gain[i];
    }
    bs300_advanced_write(0x8060B2, data);

    return bs300_active();
}

int bs300_audiometry_exit(void)
{
    bs300_prog_struct_t prog;
    int ret;

    ret = bs300_mute();
    if (ret < 0) return ret;

    bs300_itg_clear();

    if (load_struct(s_active_prog, &prog) < 0) return -1;
    ret = bs300_sync_program(&prog);
    if (ret < 0) return ret;

    return bs300_active();
}

/* ================================================================
 *  Basic commands
 * ================================================================ */
int bs300_mute(void)
{
    bs300_i2c_set_speed(BS300_I2C_DELAY_NORMAL);  /* slow before critical cmd */
    bool ok = bs300_send_simple_cmd(BS300_CMD_MUTE);
    if (ok) bs300_i2c_set_speed(BS300_I2C_DELAY_FAST);  /* DSP stopped, go fast */
    return ok ? 0 : -1;
}

int bs300_active(void)
{
    bs300_i2c_set_speed(BS300_I2C_DELAY_NORMAL);  /* slow before critical cmd */
    return bs300_send_simple_cmd(BS300_CMD_ACTIVE) ? 0 : -1;
}

int bs300_is_connected(void)
{
    return bs300_send_simple_cmd(BS300_CMD_IS_CONNECT) ? 0 : -1;
}

/* ================================================================
 *  Resync diff (blocking)
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
    bs300_prog_struct_t old_prog;
    bs300_calib_t calib;
    uint8_t old_it, new_it, igd_changed;
    uint8_t data[48];
    int sent = 0, fail = 0;

    if (_new == NULL) return -1;
    if (load_struct(s_active_prog, &old_prog) < 0) return -1;
    if (load_calib(&calib) < 0) return -1;

    old_it = get_input_type(old_prog.modules.input_selection);
    new_it = get_input_type(_new->modules.input_selection);
    igd_changed = (old_it != new_it);

    PRINTF("[BS300] resync diff prog=%d\r\n", s_active_prog);

    switch_diff_pre_enr(&old_prog.modules, &_new->modules,
                        &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_enr(&old_prog.enr, &_new->enr,
                    &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_post_enr(&old_prog.modules, &_new->modules,
                         &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_vol_beep(&old_prog.modules, &_new->modules,
                         &calib, igd_changed, NULL, &sent, &fail, data);
    switch_diff_wdrc(&old_prog.wdrc, &_new->wdrc,
                     &old_prog.modules, &_new->modules,
                     &calib, new_it, igd_changed, NULL, &sent, &fail, data);

    PRINTF("[BS300] resync_diff done, sent=%d fail=%d\r\n", sent, fail);
    return (fail > 0) ? -1 : 0;
}

int bs300_param_modify(uint8_t prog_idx, uint16_t offset,
                       const uint8_t *val, uint8_t len)
{
    bs300_prog_struct_t old_prog, new_prog;
    bs300_calib_t calib;
    uint8_t data[48];
    uint8_t old_it, new_it, igd_changed;
    uint8_t *raw;
    uint8_t i;
    int sent = 0, fail = 0;

    if (val == NULL || prog_idx >= 4) return -1;
    if (offset + len > sizeof(bs300_prog_struct_t)) return -1;
    if (load_struct(prog_idx, &old_prog) < 0) return -1;

    new_prog = old_prog;
    raw = (uint8_t *)&new_prog;
    for (i = 0; i < len; i++) raw[offset + i] = val[i];

    if (load_calib(&calib) < 0) return -1;
    old_it = get_input_type(old_prog.modules.input_selection);
    new_it = get_input_type(new_prog.modules.input_selection);
    igd_changed = (old_it != new_it);

    switch_diff_pre_enr(&old_prog.modules, &new_prog.modules,
                        &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_enr(&old_prog.enr, &new_prog.enr,
                    &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_post_enr(&old_prog.modules, &new_prog.modules,
                         &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_vol_beep(&old_prog.modules, &new_prog.modules,
                         &calib, igd_changed, NULL, &sent, &fail, data);
    switch_diff_wdrc(&old_prog.wdrc, &new_prog.wdrc,
                     &old_prog.modules, &new_prog.modules,
                     &calib, new_it, igd_changed, NULL, &sent, &fail, data);

    return (fail > 0) ? -1 : 0;
}

#undef DIFF_SEND

/* ================================================================
 *  Async session (matches AD697N: single static session + timer-driven)
 * ================================================================ */
static bs300_sync_session_t g_bs300_sync;
static void (*g_bs300_sync_on_done)(void) = NULL;

int bs300_sync_is_busy(void)
{
    return (g_bs300_sync.state != BS300_SYNC_IDLE
            && g_bs300_sync.state != BS300_SYNC_DONE
            && g_bs300_sync.state != BS300_SYNC_ERROR);
}

/* Called from ke_timer callback. Integrate in app.c APP_Timer handler:
 *   if (msg_id == BS300_SYNC_TIMER) bs300_sync_timer_handler(); */
void bs300_sync_timer_handler(void)
{
    uint16_t delay;

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

    /* Match AD697N: POLL waits 60ms, SEND fires ASAP */
    delay = (g_bs300_sync.state == BS300_SYNC_POLL) ? 6 : 2;
    ke_timer_set(BS300_SYNC_TIMER, TASK_APP, delay);
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
    if (bs300_sync_is_busy()) return -1;
    bs300_sync_session_init(&g_bs300_sync);
    if (bs300_switch_program_start(&g_bs300_sync, new_prog_idx) < 0)
        return -1;
    return start_async_session(on_done);
}

int bs300_resync_diff_async(bs300_prog_struct_t *_new, void (*on_done)(void))
{
    if (bs300_sync_is_busy()) return -1;
    bs300_sync_session_init(&g_bs300_sync);
    if (bs300_resync_diff_start(&g_bs300_sync, _new) < 0)
        return -1;
    return start_async_session(on_done);
}

int bs300_param_modify_async(uint8_t prog_idx, uint16_t offset,
                              const uint8_t *val, uint8_t len)
{
    if (bs300_sync_is_busy()) return -1;
    bs300_sync_session_init(&g_bs300_sync);
    if (bs300_param_modify_start(&g_bs300_sync, prog_idx, offset, val, len) < 0)
        return -1;
    return start_async_session(NULL);
}

static int reencode_bin_gain_async_core(bs300_prog_struct_t *prog,
                                         void (*on_done)(void))
{
    uint8_t data[48];
    bs300_calib_t calib;
    uint8_t input_type;
    int ret;

    if (load_calib(&calib) < 0) return -1;
    input_type = get_input_type(prog->modules.input_selection);

    ret = bs300_encode_wdrc_bin_gain(&prog->wdrc, &calib, &prog->modules,
                                      input_type, data);
    if (ret < 0) return -1;

    bs300_sync_session_init(&g_bs300_sync);
    g_bs300_sync_on_done = on_done;
    bs300_session_append(&g_bs300_sync, 0x8060B2, data);
    return start_async_session(on_done);
}

static int8_t s_pending_volume = -1;  /* -1 = none, 0-9 = pending */

int bs300_set_volume_async(uint8_t level, void (*on_done)(void))
{
    bs300_prog_struct_t prog;
    if (level > 9) return -1;
    if (bs300_sync_is_busy()) {
        s_pending_volume = (int8_t)level;
        PRINTF("[BS300] volume=%d queued (sync busy)\r\n", level);
        return 0;
    }
    if (load_struct(s_active_prog, &prog) < 0) return -1;
    prog.modules.volume_level = level;
    s_prog_modules[s_active_prog].volume_level = level;
    save_settings();
    return reencode_bin_gain_async_core(&prog, on_done);
}

void bs300_async_done_callback(void)
{
    if (s_pending_volume < 0) return;
    {
        int8_t vol = s_pending_volume;
        s_pending_volume = -1;
        bs300_set_volume_async((uint8_t)vol, bs300_async_done_callback);
        PRINTF("[BS300] pending volume=%d applied\r\n", vol);
    }
}

int bs300_set_eq_async(int8_t low, int8_t mid, int8_t high,
                        void (*on_done)(void))
{
    bs300_prog_struct_t prog;
    if (bs300_sync_is_busy()) return -1;
    if (load_struct(s_active_prog, &prog) < 0) return -1;
    prog.modules.eq_low  = low;
    prog.modules.eq_mid  = mid;
    prog.modules.eq_high = high;
    return reencode_bin_gain_async_core(&prog, on_done);
}

/* ================================================================
 *  Dirty sync (deferred diff on active program)
 * ================================================================ */
static bool s_need_sync = false;

void bs300_sync_dirty(void)
{
    s_need_sync = true;
    if (!bs300_sync_is_busy()) {
        bs300_prog_struct_t prog;
        s_need_sync = false;
        if (load_struct(s_active_prog, &prog) == 0) {
            reencode_bin_gain_async_core(&prog, NULL);
        }
    }
}

int bs300_vol_commit(uint8_t level)
{
    bs300_prog_struct_t prog;
    if (level > 9) return -1;
    if (load_struct(s_active_prog, &prog) < 0) return -1;
    prog.modules.volume_level = level;
    s_prog_modules[s_active_prog].volume_level = level;
    save_settings();
    bs300_sync_dirty();
    return 0;
}

static uint32_t bs300_tone_for_program(uint8_t program);

/* ================================================================
 *  Async fill-mode functions
 * ================================================================ */
int bs300_switch_program_start(bs300_sync_session_t *s, uint8_t new_prog_idx)
{
    uint8_t data[48];
    bs300_prog_struct_t old_prog, new_prog;
    bs300_calib_t calib;
    uint8_t active_prog = s_active_prog;
    uint8_t old_it, new_it, igd_changed;
    int sent = 0, fail = 0;

    if (s == NULL || new_prog_idx >= 4) return -1;
    if (active_prog == new_prog_idx) return 0;
    if (load_struct(active_prog, &old_prog) < 0) return -1;
    if (load_struct(new_prog_idx, &new_prog) < 0) return -1;

    s_active_prog = new_prog_idx;
    bs300_on_active_prog_changed(new_prog_idx);
    save_settings();

    PRINTF("[BS300] switch RAM async %d->%d\r\n", active_prog, new_prog_idx);

    /* Prompt tone FIRST — before all diff commands */
    memset(data, 0, 48);
    bs300_session_append(s, bs300_tone_for_program(new_prog_idx), data);

    if (load_calib(&calib) < 0) return -1;

    old_it = get_input_type(old_prog.modules.input_selection);
    new_it = get_input_type(new_prog.modules.input_selection);
    igd_changed = (old_it != new_it);

    switch_diff_pre_enr(&old_prog.modules, &new_prog.modules,
                        &calib, new_it, igd_changed, s, &sent, &fail, data);
    switch_diff_enr(&old_prog.enr, &new_prog.enr,
                    &calib, new_it, igd_changed, s, &sent, &fail, data);
    switch_diff_post_enr(&old_prog.modules, &new_prog.modules,
                         &calib, new_it, igd_changed, s, &sent, &fail, data);
    switch_diff_vol_beep(&old_prog.modules, &new_prog.modules,
                         &calib, igd_changed, s, &sent, &fail, data);
    switch_diff_wdrc(&old_prog.wdrc, &new_prog.wdrc,
                     &old_prog.modules, &new_prog.modules,
                     &calib, new_it, igd_changed, s, &sent, &fail, data);

    /* Force DDM2/MM+ if new prog input type requires them */
    if (new_prog.modules.input_selection == 5) {
        memset(data, 0, 48);
        bs300_encode_ddm2(&new_prog.modules, &calib, data);
        bs300_session_append(s, 0x800022, data);
    }
    if (new_prog.modules.input_selection == 4) {
        memset(data, 0, 48);
        bs300_encode_mm_plus(&new_prog.modules, &calib, new_it, data);
        bs300_session_append(s, 0x800062, data);
    }

    s->state = BS300_SYNC_SEND;
    return 0;
}

int bs300_resync_diff_start(bs300_sync_session_t *s, bs300_prog_struct_t *_new)
{
    bs300_prog_struct_t old_prog;
    bs300_calib_t calib;
    uint8_t old_it, new_it, igd_changed;
    uint8_t data[48];
    int sent = 0, fail = 0;

    if (s == NULL || _new == NULL) return -1;
    if (load_struct(s_active_prog, &old_prog) < 0) return -1;
    if (load_calib(&calib) < 0) return -1;

    old_it = get_input_type(old_prog.modules.input_selection);
    new_it = get_input_type(_new->modules.input_selection);
    igd_changed = (old_it != new_it);

    PRINTF("[BS300] resync diff async prog=%d\r\n", s_active_prog);

    switch_diff_pre_enr(&old_prog.modules, &_new->modules,
                        &calib, new_it, igd_changed, s, &sent, &fail, data);
    switch_diff_enr(&old_prog.enr, &_new->enr,
                    &calib, new_it, igd_changed, s, &sent, &fail, data);
    switch_diff_post_enr(&old_prog.modules, &_new->modules,
                         &calib, new_it, igd_changed, s, &sent, &fail, data);
    switch_diff_vol_beep(&old_prog.modules, &_new->modules,
                         &calib, igd_changed, s, &sent, &fail, data);
    switch_diff_wdrc(&old_prog.wdrc, &_new->wdrc,
                     &old_prog.modules, &_new->modules,
                     &calib, new_it, igd_changed, s, &sent, &fail, data);

    s->state = BS300_SYNC_SEND;
    return (fail > 0) ? -1 : 0;
}

int bs300_param_modify_start(bs300_sync_session_t *s, uint8_t prog_idx,
                              uint16_t offset, const uint8_t *val, uint8_t len)
{
    bs300_prog_struct_t old_prog, new_prog;
    bs300_calib_t calib;
    uint8_t data[48];
    uint8_t old_it, new_it, igd_changed;
    uint8_t *raw;
    uint8_t i;
    int sent = 0, fail = 0;

    if (s == NULL || val == NULL || prog_idx >= 4) return -1;
    if (offset + len > sizeof(bs300_prog_struct_t)) return -1;
    if (load_struct(prog_idx, &old_prog) < 0) return -1;

    new_prog = old_prog;
    raw = (uint8_t *)&new_prog;
    for (i = 0; i < len; i++) raw[offset + i] = val[i];

    if (load_calib(&calib) < 0) return -1;
    old_it = get_input_type(old_prog.modules.input_selection);
    new_it = get_input_type(new_prog.modules.input_selection);
    igd_changed = (old_it != new_it);

    switch_diff_pre_enr(&old_prog.modules, &new_prog.modules,
                        &calib, new_it, igd_changed, s, &sent, &fail, data);
    switch_diff_enr(&old_prog.enr, &new_prog.enr,
                    &calib, new_it, igd_changed, s, &sent, &fail, data);
    switch_diff_post_enr(&old_prog.modules, &new_prog.modules,
                         &calib, new_it, igd_changed, s, &sent, &fail, data);
    switch_diff_vol_beep(&old_prog.modules, &new_prog.modules,
                         &calib, igd_changed, s, &sent, &fail, data);
    switch_diff_wdrc(&old_prog.wdrc, &new_prog.wdrc,
                     &old_prog.modules, &new_prog.modules,
                     &calib, new_it, igd_changed, s, &sent, &fail, data);

    s->state = BS300_SYNC_SEND;
    return 0;
}

/* ================================================================
 *  Prompt Tone — played on mode switch and volume change
 * ================================================================ */

#define BS300_TONE_MODE_0    0xFD52F2
#define BS300_TONE_MODE_1    0xFD72F2
#define BS300_TONE_MODE_2    0xFD92F2
#define BS300_TONE_MODE_3    0xFDB2F2
#define BS300_TONE_VOL_0     0xFD12F2
#define BS300_TONE_VOL_OTHER 0xFCD2F2
#define BS300_TONE_DATA_BYTES 48

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

void bs300_play_prompt_tone(uint8_t program, uint8_t volume)
{
    static uint8_t last_program = 0xFF;
    static uint8_t last_volume  = 0xFF;
    static uint8_t inited       = 0;
    uint32_t cmd = 0;
    uint8_t data[BS300_TONE_DATA_BYTES];

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
