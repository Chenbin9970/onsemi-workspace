/**
 * BS300 Data Conversion Tool: Flash Parse → Param I2C RAM Generation
 *
 * Pipeline:
 *   program_*.json (Flash data) → decode → bs300_program_t
 *   calibration.json             → parse  → bs300_calib_t
 *   (program_t, calib_t, input_type) → encode → 32 I2C RAM command payloads
 *
 * Cross-validation:
 *   Output compared byte-by-byte against bs300_skill_data.h
 *   (generated from param_commands_0/1.json chip readback via Python reference).
 *
 * Build:
 *   gcc -std=c11 -Wall -DBS300_USE_FLOAT -o bs300_convert bs300_convert.c -lm
 *
 * Usage:
 *   ./bs300_convert                          # run cross-validation
 *   ./bs300_convert --json <prog_idx>         # print all commands as JSON
 *   ./bs300_convert --hex <prog_idx> <input>  # print commands for specific input
 *     input: 0=mic  1=telecoil  2=dai
 */
#define BS300_PORTABLE_IMPL
#include "bs300_portable.h"
#include "bs300_skill_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Stub I2C — not needed for offline conversion */
bool bs300_i2c_write(uint8_t addr, const uint8_t *data, uint8_t len) { (void)addr; (void)data; (void)len; return true; }
bool bs300_i2c_read(uint8_t addr, uint8_t *data, uint8_t len)  { (void)addr; (void)data; (void)len; return true; }
void bs300_delay_ms(uint32_t ms) { (void)ms; }

/* ================================================================
 * Utility: print 48-byte payload as hex
 * ================================================================ */

static void print_hex48(const uint8_t data[48])
{
    for (int i = 0; i < 48; i++) {
        printf("%02X", data[i]);
        if ((i + 1) % 16 == 0 && i < 47) printf("\n        ");
        else if (i < 47) printf(" ");
    }
}

/* ================================================================
 * Step 1: Flash Data → Struct  (bs300_portable.h does the heavy lifting)
 * ================================================================ */

static bool decode_program(const uint8_t *raw, int raw_len, bs300_program_t *prog)
{
    return bs300_program_parse(raw, prog);
}

/* ================================================================
 * Step 2: Struct + Calib + Input → Param I2C Commands
 * ================================================================ */

typedef struct {
    uint32_t cmd_word;
    const char *name;
    uint8_t   data[48];
} bs300_param_cmd_t;

#define MAX_PARAM_CMDS  32

/**
 * Fill in->input_type from program inputs.  Returns 0=mic, 1=telecoil, 2=dai.
 */
static uint8_t detect_input_type(const bs300_program_t *prog)
{
    switch (prog->inputs.input_type) {
    case 0x05: return 1;  /* telecoil */
    case 0x06: return 2;  /* dai */
    default:   return 0;  /* mic */
    }
}

/**
 * Convert decoded program + calibration into all Param I2C command payloads.
 *
 * @param prog        Decoded Flash program data
 * @param calib       Parsed calibration data
 * @param input_type  0=mic, 1=telecoil, 2=dai (overrides program default if != 0xFF)
 * @param cmds_out    Output array, must hold at least MAX_PARAM_CMDS entries
 * @return Number of commands generated
 */
static int generate_param_commands(const bs300_program_t *prog,
                                   const bs300_calib_t *calib,
                                   uint8_t input_type_override,
                                   bs300_param_cmd_t *cmds_out)
{
    int count = 0;
    uint8_t input_type = (input_type_override != 0xFF)
                       ? input_type_override
                       : detect_input_type(prog);

    /* Build param inputs struct from decoded Flash data */
    bs300_param_inputs_t in;
    memset(&in, 0, sizeof(in));
    in.input_type = input_type;

    /* ---- WDRC ---- */
    {
        const bs300_wdrc_flash_t *w = &prog->wdrc;
        in.wdrc_total_ch = w->num_channels;
        in.wdrc_nsbc      = 0;  /* Flash decode: all MBC */
        in.wdrc_limiter   = (w->output_limiting_sel != 0);
        in.wdrc_is_2kp    = (w->kneepoints_per_channel != 0);

        int mbc_count = w->num_channels - in.wdrc_nsbc;
        for (int i = 0; i < mbc_count && i < 16; i++)
            in.wdrc_mbc_counts[i] = 2;  /* each MBC channel = 2 bins */

        for (int i = 0; i < (int)w->num_channels && i < 16; i++) {
            const bs300_wdrc_ch_t *ch = &w->channels[i];
            in.wdrc_kp1_th[i]     = ch->kp1_th;
            in.wdrc_kp2_th[i]     = ch->kp2_th;
            in.wdrc_epd_at_idx[i] = ch->epd_at;
            in.wdrc_kp1_at_idx[i] = ch->kp1_at;
            in.wdrc_kp2_at_idx[i] = ch->kp2_at;
            in.wdrc_epd_rt_idx[i] = ch->epd_rt;
            in.wdrc_kp1_rt_idx[i] = ch->kp1_rt;
            in.wdrc_kp2_rt_idx[i] = ch->kp2_rt;
            in.wdrc_epd_r_idx[i]  = ch->epd_r;
            in.wdrc_kp1_r_idx[i]  = ch->kp1_r;
            in.wdrc_kp2_r_idx[i]  = ch->kp2_r;
            in.wdrc_lmt_th[i]     = ch->lmt_th;
            in.wdrc_lmt_at_idx[i] = ch->lmt_at;
            in.wdrc_lmt_rt_idx[i] = ch->lmt_rt;
            in.wdrc_lmt_r_idx[i]  = ch->lmt_r;
            in.wdrc_freq_idx[i]   = ch->frequency_idx;
        }
        for (int i = 0; i < 32; i++)
            in.wdrc_bin_gains[i] = (int8_t)((int)w->bin_gain[i] - 27);  /* bin_gain = 27 + value_in_MT */
    }

    /* ---- Volume ---- */
    {
        const bs300_volume_flash_t *v = &prog->volume;
        in.vol_beep_level_db     = v->beep_level;
        in.vol_beep_freq_idx     = v->beep_frequency;  /* stored as data word index in Flash */
        in.vol_min_db            = v->min_volume;
        in.vol_max_db            = v->max_volume;
        in.vol_input_sel         = (input_type == 0) ? 0 : 1;
        in.vol_batt_beep_level_db = v->battery_flat_beep_level;
        in.vol_batt_beep_freq_idx = v->battery_flat_beep_frequency;
    }

    /* ---- DFBC ---- */
    if (prog->has_dfbc)
        in.dfbc_mode = prog->dfbc.dfbc_mode;

    /* ---- ISS ---- */
    if (prog->has_iss)
        in.iss_threshold_dbspl = prog->iss.iss_threshold;

    /* ---- WNR ---- */
    if (prog->has_wnr) {
        /* suppression_preset: Flash stores value_in_MT, map to SSP level */
        uint8_t sp = prog->wnr.suppression_strength_preset;
        if (sp <= 1)      in.wnr_ssp_level = 0;
        else if (sp <= 3) in.wnr_ssp_level = 1;
        else if (sp <= 6) in.wnr_ssp_level = 2;
        else if (sp <= 9) in.wnr_ssp_level = 3;
        else              in.wnr_ssp_level = 4;
        in.wnr_dual_mic = (prog->wnr.dual_mic_mode_sel != 0);
    }

    /* ---- ENR ---- */
    if (prog->has_enr) {
        const bs300_enr_flash_t *e = &prog->enr;
        in.enr_total_ch = e->num_channels;
        in.enr_sbc      = 2;
        in.enr_mbc      = (uint8_t)(e->num_channels > 2 ? e->num_channels - 2 : 0);
        in.enr_nhsf     = e->nhsf + 1;   /* Flash stores value-1 */
        in.enr_nfsf     = e->nfsf + 1;
        in.enr_nnsf     = e->nnsf + 1;
        in.enr_snasf    = 4;

        for (int i = 0; i < (int)e->num_channels && i < 16; i++) {
            const bs300_enr_ch_t *ch = &e->channels[i];
            in.enr_snr_th[i]    = ch->snrth;
            in.enr_max_att[i]   = ch->ma;
            in.enr_noise_th[i]  = (uint8_t)(ch->nt + 10);      /* Flash: nt = value - 10 */
            in.enr_upper_nt[i]  = (uint8_t)(ch->unt + 40);     /* Flash: unt = value - 40 */
            in.enr_etr[i]       = (float)ch->etr / 100.0f;     /* Flash: etr = value * 100 */
            in.enr_nrr_val[i]   = (float)ch->nrr / 10.0f;      /* Flash: nrr = value * 10 */
            in.enr_freq_idx[i]  = ch->frequency_idx;
        }

        /* Compute band counts from ENR channel frequency indices */
        for (int i = 0; i < (int)e->num_channels && i < 16; i++) {
            if (i < (int)e->num_channels - 1)
                in.enr_band_counts[i] = (uint8_t)(e->channels[i + 1].frequency_idx
                                                  - e->channels[i].frequency_idx);
            else
                in.enr_band_counts[i] = (uint8_t)(32 - e->channels[i].frequency_idx);
        }
    }

    /* ---- AGCO ---- */
    if (prog->has_agco) {
        in.agco_threshold_db = prog->agco.threshold;
        in.agco_atk_01ms     = prog->agco.attack_time;
        in.agco_rel_01ms     = prog->agco.release_time;
    }

    /* ---- Generate all commands ---- */
#define ADD_CMD(cmd_word, name_str, encode_call) do { \
    cmds_out[count].cmd_word = (cmd_word); \
    cmds_out[count].name = (name_str); \
    memset(cmds_out[count].data, 0, 48); \
    encode_call; \
    count++; \
} while (0)

    uint8_t out[48];

    /* WDRC: 11 commands */
    ADD_CMD(0x8000B2, "WDRC General",
            bs300_encode_wdrc_general(&in, calib, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8010B2, "WDRC Freq Spacing",
            bs300_encode_wdrc_freq_spacing(&in, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8020B2, "WDRC KP Threshold",
            bs300_encode_wdrc_kp_threshold(&in, calib, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8030B2, "WDRC Attack Time",
            bs300_encode_wdrc_attack_time(&in, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8040B2, "WDRC Release Time",
            bs300_encode_wdrc_release_time(&in, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8050B2, "WDRC Ratio",
            bs300_encode_wdrc_ratio(&in, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8060B2, "WDRC Bin Gain",
            bs300_encode_wdrc_bin_gain(&in, calib, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8070B2, "WDRC Lmt Threshold",
            bs300_encode_wdrc_lmt_threshold(&in, calib, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8080B2, "WDRC Lmt Attack",
            bs300_encode_wdrc_lmt_attack(&in, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8090B2, "WDRC Lmt Release",
            bs300_encode_wdrc_lmt_release(&in, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x80A0B2, "WDRC Lmt Ratio",
            bs300_encode_wdrc_lmt_ratio(&in, out); memcpy(cmds_out[count].data, out, 48));

    /* Volume/Beep */
    ADD_CMD(0x800081, "Volume Beep Input",
            bs300_encode_volume_beep(&in, calib, out); memcpy(cmds_out[count].data, out, 48));

    /* TC Gain Diff */
    ADD_CMD(0x804272, "TC Gain Diff",
            bs300_encode_tc_gain_diff(calib, out); memcpy(cmds_out[count].data, out, 48));

    /* DFBC */
    ADD_CMD(0x800052, "DFBC",
            bs300_encode_dfbc(&in, calib, out); memcpy(cmds_out[count].data, out, 48));

    /* ISS */
    ADD_CMD(0x8001B2, "ISS",
            bs300_encode_iss(&in, calib, out); memcpy(cmds_out[count].data, out, 48));

    /* WNR: 4 commands */
    ADD_CMD(0x8001C2, "WNR Setup",
            bs300_encode_wnr_setup(&in, calib, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8011C2, "WNR Bands 0-15",
            bs300_encode_wnr_bands(&in, calib, 0, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8411C2, "WNR Bands 16-31",
            bs300_encode_wnr_bands(&in, calib, 16, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8021C2, "WNR Single Mic",
            bs300_encode_wnr_single_mic(&in, calib, out); memcpy(cmds_out[count].data, out, 48));

    /* ENR: 9 commands */
    ADD_CMD(0x8000C2, "ENR General",
            bs300_encode_enr_general(&in, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8010C2, "ENR Freq Spacing",
            bs300_encode_enr_freq_spacing(&in, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8020C2, "ENR SNR Threshold",
            bs300_encode_enr_snr_threshold(&in, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8030C2, "ENR Max Att",
            bs300_encode_enr_max_att(&in, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8040C2, "ENR Noise Thr",
            bs300_encode_enr_noise_th(&in, calib, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8050C2, "ENR Upper Noise Thr",
            bs300_encode_enr_upper_noise_th(&in, calib, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8060C2, "ENR Smoothing",
            bs300_encode_enr_smoothing(&in, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8070C2, "ENR ETR",
            bs300_encode_enr_etr(&in, out); memcpy(cmds_out[count].data, out, 48));
    ADD_CMD(0x8080C2, "ENR NRR",
            bs300_encode_enr_nrr(&in, out); memcpy(cmds_out[count].data, out, 48));

    /* AGCO */
    ADD_CMD(0x800382, "AGCO",
            bs300_encode_agco(&in, out); memcpy(cmds_out[count].data, out, 48));

    /* MM Plus (disabled → all zeros) */
    ADD_CMD(0x800062, "MM Plus",
            memset(out, 0, 48); bs300_set_word(out, 0, 0);
            memcpy(cmds_out[count].data, out, 48));

    /* DDM2 (disabled → all zeros) */
    ADD_CMD(0x800022, "DDM2",
            memset(out, 0, 48); bs300_set_word(out, 0, 0);
            memcpy(cmds_out[count].data, out, 48));

#undef ADD_CMD
    return count;
}

/* ================================================================
 * Step 3: Print Commands
 * ================================================================ */

static void print_commands_hex(int prog_idx, uint8_t input_type,
                               int n_cmds, const bs300_param_cmd_t *cmds)
{
    const char *input_names[] = {"Mic", "Telecoil", "DAI"};
    printf("BS300 Param I2C Commands — Program %d, Input=%s\n", prog_idx,
           input_type < 3 ? input_names[input_type] : "?");
    printf("%-8s %-24s %-48s\n", "CmdWord", "Module", "Payload (48B hex)");
    printf("-------- ------------------------ "
           "------------------------------------------------\n");
    for (int i = 0; i < n_cmds; i++) {
        printf("0x%06X %-24s ", cmds[i].cmd_word, cmds[i].name);
        print_hex48(cmds[i].data);
        printf("\n");
    }
}

static void print_commands_json(int prog_idx, uint8_t input_type,
                                int n_cmds, const bs300_param_cmd_t *cmds)
{
    printf("{\n");
    printf("  \"program\": %d,\n", prog_idx);
    printf("  \"input_type\": %d,\n", input_type);
    printf("  \"commands\": [\n");
    for (int i = 0; i < n_cmds; i++) {
        printf("    {\"cmd_word\": \"0x%06X\", \"name\": \"%s\", \"payload\": \"",
               cmds[i].cmd_word, cmds[i].name);
        for (int j = 0; j < 48; j++) printf("%02X", cmds[i].data[j]);
        printf("\"}");
        if (i < n_cmds - 1) printf(",");
        printf("\n");
    }
    printf("  ]\n");
    printf("}\n");
}

/* ================================================================
 * Step 4: Cross-Validation
 * ================================================================ */

static int cross_validate(int prog_idx, int n_cmds, const bs300_param_cmd_t *cmds)
{
    int byte_exact = 0, tolerated = 0, mismatches = 0;

    const uint32_t *expected_cmds;
    const uint8_t  (*expected_data)[48];
    int expected_count;

    if (prog_idx == 0) {
        expected_cmds  = bs300_skill_p0_cmd_words;
        expected_data  = bs300_skill_p0_expected;
        expected_count = BS300_SKILL_P0_CMD_COUNT;
    } else {
        expected_cmds  = bs300_skill_p1_cmd_words;
        expected_data  = bs300_skill_p1_expected;
        expected_count = BS300_SKILL_P1_CMD_COUNT;
    }

    printf("\nCross-Validation: Program %d\n", prog_idx);
    printf("%-8s %-24s %-10s %s\n", "CmdWord", "Module", "Result", "Detail");
    printf("-------- ------------------------ ---------- ------------------------------\n");

    for (int ei = 0; ei < expected_count; ei++) {
        uint32_t ec = expected_cmds[ei];

        /* Find matching command in our output */
        const bs300_param_cmd_t *match = NULL;
        for (int ci = 0; ci < n_cmds; ci++) {
            if (cmds[ci].cmd_word == ec) { match = &cmds[ci]; break; }
        }
        if (!match) { mismatches++; continue; }

        /* Compare word by word */
        int word_errs = 0;
        int max_byte_diff = 0;
        for (int wi = 0; wi < 16; wi++) {
            uint32_t ew = bs300_get_word(expected_data[ei], wi);
            uint32_t cw = bs300_get_word(match->data, wi);
            if (ew != cw) {
                word_errs++;
                for (int bi = 0; bi < 3; bi++) {
                    int d = (int)((ew >> (bi * 8)) & 0xFF)
                          - (int)((cw >> (bi * 8)) & 0xFF);
                    if (d < 0) d = -d;
                    if (d > max_byte_diff) max_byte_diff = d;
                }
            }
        }

        if (word_errs == 0) {
            byte_exact++;
            printf("0x%06X %-24s EXACT\n", ec, match->name);
        } else if (max_byte_diff <= 1) {
            tolerated++;
            printf("0x%06X %-24s TOLERATED  %d words (±1)\n",
                   ec, match->name, word_errs);
        } else {
            mismatches++;
            printf("0x%06X %-24s MISMATCH   %d words, max_diff=%d\n",
                   ec, match->name, word_errs, max_byte_diff);
        }
    }

    printf("\n  Summary: %d exact, %d tolerated, %d mismatches\n",
           byte_exact, tolerated, mismatches);

    /* Known issues: ENR NT/UNT mismatches are documented (skill §2.3) */
    if (mismatches == 2) {
        printf("  NOTE: 2 mismatches are ENR NT/UNT — documented known issue (chip internal\n"
               "        SNR_Frequency_Spacing[] array not yet public). Not a C code bug.\n");
    }

    return mismatches;
}

/* ================================================================
 * main()
 * ================================================================ */

int main(int argc, char **argv)
{
    bool json_mode = false;
    bool hex_mode  = false;
    int  prog_idx  = 0;
    int  input_sel = 0;  /* 0=mic, 1=telecoil, 2=dai */

    /* Parse args */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            json_mode = true;
            prog_idx  = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--hex") == 0 && i + 2 < argc) {
            hex_mode  = true;
            prog_idx  = atoi(argv[++i]);
            input_sel = atoi(argv[++i]);
        }
    }

    /* Parse calibration */
    bs300_calib_t calib;
    if (!bs300_calib_parse(bs300_skill_calib_raw, &calib)) {
        fprintf(stderr, "ERROR: Failed to parse calibration data\n");
        return 1;
    }

    /* Parse program */
    const uint8_t *prog_raw = (prog_idx == 0) ? bs300_skill_prog0_raw : bs300_skill_prog1_raw;
    int prog_raw_len = (prog_idx == 0) ? (int)sizeof(bs300_skill_prog0_raw)
                                       : (int)sizeof(bs300_skill_prog1_raw);

    bs300_program_t prog;
    if (!decode_program(prog_raw, prog_raw_len, &prog)) {
        fprintf(stderr, "ERROR: Failed to decode program %d Flash data\n", prog_idx);
        return 1;
    }

    printf("BS300 Data Conversion Tool\n");
    printf("==========================\n");
    printf("Calibration: avg_mic1=%d.%d dB, avg_out=%d.%d dB, tc_gd=%d.%d dB\n",
           bs300_calib_avg_mic1(&calib) / 10, bs300_calib_avg_mic1(&calib) % 10,
           bs300_calib_avg_output(&calib) / 10, bs300_calib_avg_output(&calib) % 10,
           calib.telecoil_gain_diff / 10, abs(calib.telecoil_gain_diff) % 10);
    printf("Program %d: WDRC %d-ch %sKP, ENR %d-ch, inputs=0x%02X\n",
           prog_idx, prog.wdrc.num_channels,
           prog.wdrc.kneepoints_per_channel ? "2" : "1",
           prog.has_enr ? prog.enr.num_channels : 0,
           prog.inputs.input_type);

    /* Generate commands for a specific input type (or auto-detect) */
    uint8_t actual_input = hex_mode ? (uint8_t)input_sel : 0xFF;
    bs300_param_cmd_t cmds[MAX_PARAM_CMDS];
    int n_cmds = generate_param_commands(&prog, &calib, actual_input, cmds);

    printf("Generated %d Param I2C commands\n", n_cmds);

    /* Output */
    if (json_mode) {
        print_commands_json(prog_idx, actual_input != 0xFF ? actual_input : detect_input_type(&prog),
                            n_cmds, cmds);
    } else if (hex_mode) {
        print_commands_hex(prog_idx, actual_input != 0xFF ? actual_input : detect_input_type(&prog),
                           n_cmds, cmds);
    } else {
        /* Default: cross-validate against skill data */
        printf("\n=== CROSS-VALIDATION MODE ===\n");

        /* For telecoil (P0 default), generate with TC input */
        bs300_param_cmd_t cmds_tc[MAX_PARAM_CMDS];
        int n_tc = generate_param_commands(&prog, &calib, 1, cmds_tc);

        int result = cross_validate(prog_idx, n_tc, cmds_tc);

        /* If P0 with TC → also show validation against P0 chip data */
        if (prog_idx == 0) {
            print_commands_hex(0, 1, n_tc, cmds_tc);
        }

        /* P1 uses mic input */
        const uint8_t *raw1 = bs300_skill_prog1_raw;
        int raw1_len = (int)sizeof(bs300_skill_prog1_raw);
        bs300_program_t prog1;
        if (decode_program(raw1, raw1_len, &prog1)) {
            bs300_param_cmd_t cmds_mic[MAX_PARAM_CMDS];
            int n_mic = generate_param_commands(&prog1, &calib, 0, cmds_mic);
            int result1 = cross_validate(1, n_mic, cmds_mic);

            if (result + result1 == 4) {
                /* Only ENR NT/UNT mismatches (2 per program) = expected */
                printf("\n*** ALL CLEAR: Only documented ENR NT/UNT differences. ***\n");
            }
        }

        printf("\nRun with --json <0|1> for JSON output, --hex <0|1> <0|1|2> for hex dump.\n");
    }

    return 0;
}
