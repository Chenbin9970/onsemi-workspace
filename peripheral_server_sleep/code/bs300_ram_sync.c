#include "bs300_ram_sync.h"
#include "bs300_driver.h"
#include "bs300_calib.h"
#include "bs300_param_encode.h"
#include "bs300_startup.h"
#include "app.h"
#include <string.h>

/* Fallback: if DEBUG_UART_ENABLE is removed, PRINTF becomes no-op */
#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

/* Determine input_type string from program inputs */
static const char *get_input_type(const bs300_program_data_t *prog)
{
    if (!prog) return "front_mic";
    if (strncmp(prog->inputs.input_type, "telecoil", 8) == 0)
        return "telecoil";
    if (strncmp(prog->inputs.input_type, "dai", 3) == 0)
        return "dai";
    if (strncmp(prog->inputs.input_type, "rear_mic", 8) == 0)
        return "rear_mic";
    if (strncmp(prog->inputs.input_type, "dual_mic", 8) == 0)
        return "dual_mic";
    if (strncmp(prog->inputs.input_type, "ddm2", 4) == 0)
        return "ddm2";
    if (strncmp(prog->inputs.input_type, "mm_plus", 7) == 0)
        return "mm_plus";
    return "front_mic";
}

/* Encode + send one command. Returns true on success. */
static bool send_one(uint32_t cmd, const uint8_t *data)
{
    if (!bs300_advanced_write(cmd, data)) {
        PRINTF("BS300: sync FAIL at 0x%06lX\r\n", cmd);
        return false;
    }
    return true;
}

bool bs300_ram_sync(uint8_t prog_idx)
{
    const bs300_program_data_t *prog;
    const uint8_t *calib_raw;
    bs300_calib_data_t calib;
    const char *input_type;
    uint8_t data[48];
    uint8_t input_sel;

    if (prog_idx > 3) return false;

    /* 1. Get program data */
    prog = bs300_driver_get_program(prog_idx);
    if (!prog) {
        PRINTF("BS300: ram_sync — program %u not available\r\n", prog_idx);
        return false;
    }

    /* 2. Startup: unlock chip (MUTE → KEY_LOCK → VERIFY_COMM) */
    if (!bs300_startup()) {
        PRINTF("BS300: ram_sync — startup FAIL\r\n");
        return false;
    }

    /* 3. Get + parse calibration (read from chip if not cached) */
    calib_raw = bs300_driver_get_calibration();
    if (!calib_raw) {
        static uint8_t calib_buf[144];
        PRINTF("BS300: reading calibration from chip...\r\n");
        if (!bs300_read_calibration(calib_buf)) {
            PRINTF("BS300: ram_sync — calibration read FAIL\r\n");
            return false;
        }
        calib_raw = calib_buf;
    }
    if (!bs300_calib_parse(calib_raw, &calib)) {
        PRINTF("BS300: ram_sync — calibration parse FAIL\r\n");
        return false;
    }

    /* 4. Detect input type */
    input_type = get_input_type(prog);
    input_sel = (strncmp(input_type, "telecoil", 8) == 0
                 || strncmp(input_type, "dai", 3) == 0) ? 1 : 0;

    PRINTF("BS300: ram_sync prog=%u input=%s\r\n", prog_idx, input_type);

    /* 5. Send all 31 commands in order */

    /* DDM2 (disabled) */
    bs300_enc_ddm2_disabled(data);
    if (!send_one(0x800022, data)) return false;

    /* MM Plus (disabled) */
    bs300_enc_mm_plus_disabled(data);
    if (!send_one(0x800062, data)) return false;

    /* DFBC */
    {
        uint8_t dfbc_mode = prog->has_dfbc ? prog->dfbc.dfbc_mode : 0x0F;
        bs300_enc_dfbc(dfbc_mode, &calib, data);
        if (!send_one(0x800052, data)) return false;
    }

    /* ENR × 8 (without SASF 0x8090C2) */
    if (prog->has_enr) {
        bs300_enc_enr_general(prog->enr.num_channels, data);
        if (!send_one(0x8000C2, data)) return false;

        {
            uint8_t fidx[16];
            uint8_t i;
            for (i = 0; i < prog->enr.num_channels && i < 16; i++)
                fidx[i] = prog->enr.channels[i].frequency_idx;
            bs300_enc_enr_freq_spacing(fidx, prog->enr.num_channels, data);
        }
        if (!send_one(0x8010C2, data)) return false;

        bs300_enc_enr_snr_threshold(&prog->enr, data);
        if (!send_one(0x8020C2, data)) return false;

        bs300_enc_enr_max_att(&prog->enr, data);
        if (!send_one(0x8030C2, data)) return false;

        bs300_enc_enr_noise_th(&prog->enr, &calib, input_type, data);
        if (!send_one(0x8040C2, data)) return false;

        bs300_enc_enr_upper_noise_th(&prog->enr, &calib, input_type, data);
        if (!send_one(0x8050C2, data)) return false;

        bs300_enc_enr_smoothing(&prog->enr, data);
        if (!send_one(0x8060C2, data)) return false;

        bs300_enc_enr_etr(&prog->enr, data);
        if (!send_one(0x8070C2, data)) return false;

        bs300_enc_enr_nrr(&prog->enr, data);
        if (!send_one(0x8080C2, data)) return false;
    }

    /* TC/DAI Gain Diff */
    bs300_enc_tc_dai_gain_diff(&calib, data);
    if (!send_one(0x804272, data)) return false;

    /* ISS */
    if (prog->has_iss) {
        bs300_enc_iss(prog->iss.iss_threshold, &calib, input_type, data);
        if (!send_one(0x8001B2, data)) return false;
    }

    /* WNR × 4 */
    if (prog->has_wnr) {
        uint8_t ssp = prog->wnr.suppression_strength_preset;
        bs300_enc_wnr_general(&calib, ssp, data);
        if (!send_one(0x8001C2, data)) return false;

        bs300_enc_wnr_bands_0_15(&calib, input_type, data);
        if (!send_one(0x8011C2, data)) return false;

        bs300_enc_wnr_bands_16_31(&calib, input_type, data);
        if (!send_one(0x8411C2, data)) return false;

        bs300_enc_wnr_single_mic(&calib, input_type, data);
        if (!send_one(0x8021C2, data)) return false;
    }

    /* AGCO */
    if (prog->has_agco) {
        bs300_enc_agco(prog->agco.threshold,
                       prog->agco.attack_time,
                       prog->agco.release_time, data);
        if (!send_one(0x800382, data)) return false;
    }

    /* Volume/Beep/Input */
    bs300_enc_volume_beep(&prog->volume, input_sel, &calib, data);
    if (!send_one(0x800081, data)) return false;

    /* WDRC × 11 */
    {
        uint8_t fidx[16];
        uint8_t i;
        for (i = 0; i < prog->wdrc.num_channels && i < 16; i++)
            fidx[i] = prog->wdrc.channels[i].frequency_idx;

        bs300_enc_wdrc_general(prog->wdrc.num_channels,
                                prog->wdrc.output_limiting_sel != 0, data);
        if (!send_one(0x8000B2, data)) return false;

        bs300_enc_wdrc_freq_spacing(fidx, prog->wdrc.num_channels, data);
        if (!send_one(0x8010B2, data)) return false;

        bs300_enc_wdrc_kp_threshold(&prog->wdrc, &calib, input_type, data);
        if (!send_one(0x8020B2, data)) return false;

        bs300_enc_wdrc_attack_time(&prog->wdrc, data);
        if (!send_one(0x8030B2, data)) return false;

        bs300_enc_wdrc_release_time(&prog->wdrc, data);
        if (!send_one(0x8040B2, data)) return false;

        bs300_enc_wdrc_ratio(&prog->wdrc, data);
        if (!send_one(0x8050B2, data)) return false;

        bs300_enc_wdrc_bin_gain(&prog->wdrc, &calib, input_type, data);
        if (!send_one(0x8060B2, data)) return false;

        bs300_enc_wdrc_lmt_threshold(&prog->wdrc, &calib, data);
        if (!send_one(0x8070B2, data)) return false;

        bs300_enc_wdrc_lmt_attack(&prog->wdrc, data);
        if (!send_one(0x8080B2, data)) return false;

        bs300_enc_wdrc_lmt_release(&prog->wdrc, data);
        if (!send_one(0x8090B2, data)) return false;

        bs300_enc_wdrc_lmt_ratio(&prog->wdrc, data);
        if (!send_one(0x80A0B2, data)) return false;
    }

    /* 6. ACTIVE — fire-and-forget */
    bs300_send_simple_cmd(0x800010);

    PRINTF("BS300: ram_sync prog=%u DONE\r\n", prog_idx);
    return true;
}
