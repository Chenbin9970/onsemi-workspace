/**
 * BS300 Program Burn Data Read + Parse
 *
 * I2C read flow (uses bs300_startup building blocks):
 *   1. READ_START (0x80Y031) — tell chip which program to read
 *   2. Read 10 packets (0x800011 + pkt<<12) — 48B each → 480B total
 *
 * The raw 480-byte payload is then parsed via BitReader into structured fields.
 */

#include "bs300_program_read.h"
#include "bs300_startup.h"
#include <string.h>

/* ============================================================
 * Program Burn Read Flow
 * ============================================================ */

bool bs300_program_read(uint8_t program_index, uint8_t *data_out)
{
    if (program_index > 3) return false;

    /* Step 1: READ_START — tell chip to prepare Flash data for Program Y */
    if (!bs300_send_simple_cmd(0x800031 | (program_index << 12)))
        return false;

    /* Step 2: Read 10 packets (0x800011 ~ 0x809011) */
    for (uint8_t pkt = 0; pkt < BS300_PKT_COUNT; pkt++) {
        uint32_t cmd = 0x800011 | (pkt << 12);
        if (!bs300_read_packet(cmd,
                                data_out ? data_out + (pkt * BS300_PKT_SIZE)
                                         : NULL))
            return false;
        bs300_delay_ms(2);
    }

    return true;
}

/* ============================================================
 * BitReader (LSB-first, cross-byte)
 * ============================================================ */

typedef struct {
    const uint8_t *data;
    uint16_t       bit_pos;
    uint16_t       data_len;
} bs300_bitreader_t;

static void bs300_br_init(bs300_bitreader_t *br, const uint8_t *data,
                           uint16_t len)
{
    br->data = data;
    br->bit_pos = 0;
    br->data_len = len;
}

static uint32_t bs300_br_read(bs300_bitreader_t *br, uint8_t n_bits)
{
    uint32_t result = 0;
    for (uint8_t i = 0; i < n_bits; i++) {
        uint16_t byte_idx = br->bit_pos / 8;
        uint8_t  bit_idx  = br->bit_pos % 8;
        if (byte_idx >= br->data_len) return result;
        result |= ((uint32_t)((br->data[byte_idx] >> bit_idx) & 1) << i);
        br->bit_pos++;
    }
    return result;
}

static void bs300_br_skip(bs300_bitreader_t *br, uint8_t n_bits)
{
    br->bit_pos += n_bits;
}

/* ============================================================
 * Module Decode Functions
 * ============================================================ */

typedef enum {
    MOD_WDRC            = 0x12,
    MOD_VOLUME          = 0x07,
    MOD_INPUT_FRONT_MIC = 0x03,
    MOD_INPUT_REAR_MIC  = 0x04,
    MOD_INPUT_TELECOIL  = 0x05,
    MOD_INPUT_DAI       = 0x06,
    MOD_INPUT_MM_PLUS   = 0x17,
    MOD_INPUT_DDM2      = 0x1B,
    MOD_INPUT_DUAL_MIC  = 0x1E,
    MOD_DFBC            = 0x14,
    MOD_ENR             = 0x1C,
    MOD_NOISE_GEN2      = 0x21,
    MOD_ISS             = 0x1D,
    MOD_WNR             = 0x1F,
    MOD_ACCLIMATIZATION = 0x26,
    MOD_AGCO            = 0x23,
} bs300_module_cmd_t;

static bool _decode_wdrc_channel(bs300_bitreader_t *br,
                                  bs300_wdrc_channel_t *ch)
{
    ch->frequency_idx = (uint8_t)bs300_br_read(br, 6);
    ch->epd_at = (uint8_t)bs300_br_read(br, 7);
    ch->epd_rt = (uint8_t)bs300_br_read(br, 7);
    ch->epd_r  = (uint8_t)bs300_br_read(br, 7);
    if (bs300_br_read(br, 2) != 0x2) return false;
    ch->kp1_th = (uint8_t)bs300_br_read(br, 7);
    ch->kp2_th = (uint8_t)bs300_br_read(br, 7);
    if (bs300_br_read(br, 2) != 0x2) return false;
    ch->kp1_at = (uint8_t)bs300_br_read(br, 7);
    ch->kp2_at = (uint8_t)bs300_br_read(br, 7);
    if (bs300_br_read(br, 2) != 0x2) return false;
    ch->kp1_rt = (uint8_t)bs300_br_read(br, 7);
    ch->kp2_rt = (uint8_t)bs300_br_read(br, 7);
    if (bs300_br_read(br, 2) != 0x2) return false;
    ch->kp1_r = (uint8_t)bs300_br_read(br, 7);
    ch->kp2_r = (uint8_t)bs300_br_read(br, 7);
    ch->lmt_th = (uint8_t)bs300_br_read(br, 7);
    ch->lmt_at = (uint8_t)bs300_br_read(br, 7);
    ch->lmt_rt = (uint8_t)bs300_br_read(br, 7);
    ch->lmt_r  = (uint8_t)bs300_br_read(br, 7);
    return true;
}

static bool _decode_wdrc(const uint8_t *data, uint16_t len, bs300_wdrc_t *wdrc)
{
    bs300_bitreader_t br;
    bs300_br_init(&br, data, len);
    bs300_br_skip(&br, 1);
    wdrc->output_limiting_sel = (uint8_t)bs300_br_read(&br, 1);
    wdrc->kneepoints_per_channel = (uint8_t)bs300_br_read(&br, 1);
    bs300_br_skip(&br, 5);
    bs300_br_skip(&br, 1);
    for (uint8_t i = 0; i < BS300_WDRC_BANDS; i++) {
        wdrc->bin_gain[i] = (uint8_t)bs300_br_read(&br, 7);
    }
    wdrc->num_channels = (uint8_t)bs300_br_read(&br, 5);
    bs300_br_skip(&br, 1);
    if (wdrc->num_channels > BS300_WDRC_MAX_CHANNELS) return false;
    for (uint8_t i = 0; i < wdrc->num_channels; i++) {
        if (!_decode_wdrc_channel(&br, &wdrc->channels[i])) return false;
    }
    return true;
}

static void _decode_volume(const uint8_t *data, bs300_volume_t *vol)
{
    vol->beep_level     = data[0];
    vol->beep_frequency = (uint16_t)(data[1] | (data[2] << 8));
    vol->min_volume     = (int8_t)data[3];
    vol->max_volume     = (int8_t)data[4];
    vol->battery_flat_beep_level     = data[5];
    vol->battery_flat_beep_frequency = (uint16_t)(data[6] | (data[7] << 8));
}

static void _decode_inputs(uint8_t cmd_data, const uint8_t *data,
                            uint16_t len, bs300_inputs_t *inp)
{
    memset(inp, 0, sizeof(*inp));
    switch (cmd_data) {
    case MOD_INPUT_FRONT_MIC:  strcpy(inp->input_type, "front_mic");  break;
    case MOD_INPUT_REAR_MIC:   strcpy(inp->input_type, "rear_mic");   break;
    case MOD_INPUT_TELECOIL:   strcpy(inp->input_type, "telecoil");   break;
    case MOD_INPUT_DAI:        strcpy(inp->input_type, "dai");        break;
    case MOD_INPUT_DUAL_MIC:   strcpy(inp->input_type, "dual_mic");   break;
    case MOD_INPUT_MM_PLUS:
        strcpy(inp->input_type, "mm_plus");
        if (len >= 3) {
            inp->mic_mixing_ratio = data[0];
            inp->mm_type = data[1];
        }
        break;
    case MOD_INPUT_DDM2:
        strcpy(inp->input_type, "ddm2");
        if (len >= 6) {
            inp->omni_threshold  = data[1];
            inp->mode            = (data[2] >> 5) & 1;
            inp->cutoff_frequency = (uint32_t)data[3]
                                    | ((uint32_t)data[4] << 8)
                                    | ((uint32_t)data[5] << 16);
        }
        break;
    default:
        strcpy(inp->input_type, "unknown");
        break;
    }
}

static void _decode_dfbc(const uint8_t *data, bs300_dfbc_t *dfbc)
{
    dfbc->dfbc_mode = data[0];
}

static bool _decode_enr(const uint8_t *data, uint16_t len, bs300_enr_t *enr)
{
    bs300_bitreader_t br;
    bs300_br_init(&br, data, len);
    enr->nfsf = (uint8_t)bs300_br_read(&br, 4);
    enr->nhsf = (uint8_t)bs300_br_read(&br, 4);
    enr->nnsf = (uint8_t)bs300_br_read(&br, 4);
    uint8_t num_ch_low  = (uint8_t)bs300_br_read(&br, 4);
    uint8_t num_ch_high = (uint8_t)bs300_br_read(&br, 2);
    enr->num_channels = (num_ch_low | (num_ch_high << 4)) + 1;
    if (enr->num_channels > BS300_ENR_MAX_CHANNELS) return false;
    for (uint8_t i = 0; i < enr->num_channels; i++) {
        enr->channels[i].frequency_idx = (uint8_t)bs300_br_read(&br, 6);
        enr->channels[i].ma    = (uint8_t)bs300_br_read(&br, 5);
        enr->channels[i].snrth = (uint8_t)bs300_br_read(&br, 5);
        enr->channels[i].nt    = (uint8_t)bs300_br_read(&br, 6);
        enr->channels[i].unt   = (uint8_t)bs300_br_read(&br, 6);
        enr->channels[i].etr   = (uint8_t)bs300_br_read(&br, 7);
        enr->channels[i].nrr   = (uint8_t)bs300_br_read(&br, 4);
    }
    enr->snasf = (uint8_t)bs300_br_read(&br, 4);
    return true;
}

static void _decode_iss(const uint8_t *data, bs300_iss_t *iss)
{
    iss->iss_threshold = data[0];
}

static void _decode_wnr(const uint8_t *data, bs300_wnr_t *wnr)
{
    wnr->dual_mic_mode_sel          = data[0];
    wnr->suppression_strength_preset = data[1];
}

static void _decode_agco(const uint8_t *data, bs300_agco_t *agco)
{
    agco->attack_time  = (uint16_t)(data[0] | ((data[1] & 0x0F) << 8));
    agco->release_time = (uint16_t)(((data[1] & 0xF0) >> 4) | (data[2] << 4));
    agco->threshold    = data[3];
}

/* ============================================================
 * Program Burn Data Parser
 * ============================================================ */

bool bs300_program_parse(const uint8_t *raw, bs300_program_data_t *prog)
{
    if (!raw || !prog) return false;
    memset(prog, 0, sizeof(*prog));

    uint8_t  num_cmds = raw[3] - 1;
    uint16_t pos = 4;

    typedef struct {
        uint8_t cmd_data;
        uint8_t length_words;
    } cmd_entry_t;
    cmd_entry_t cmds[16];
    uint8_t cmd_count = 0;

    for (uint8_t i = 0; i < num_cmds; i++) {
        if (pos + 3 > BS300_TOTAL_DATA) return false;
        cmds[i].cmd_data     = raw[pos];
        cmds[i].length_words = raw[pos + 2];
        pos += 3;
        cmd_count++;
    }

    if (pos + 2 > BS300_TOTAL_DATA) return false;
    if (raw[pos] != 0xFB || raw[pos + 1] != 0x00) return false;
    pos += 2;

    for (uint8_t i = 0; i < cmd_count; i++) {
        uint16_t length_bytes = cmds[i].length_words * 3;
        if (pos + length_bytes > BS300_TOTAL_DATA) return false;

        switch (cmds[i].cmd_data) {
        case MOD_WDRC:
            if (!_decode_wdrc(raw + pos, length_bytes, &prog->wdrc))
                return false;
            break;
        case MOD_VOLUME:
            if (length_bytes >= 9)
                _decode_volume(raw + pos, &prog->volume);
            break;
        case MOD_INPUT_FRONT_MIC:
        case MOD_INPUT_REAR_MIC:
        case MOD_INPUT_TELECOIL:
        case MOD_INPUT_DAI:
        case MOD_INPUT_MM_PLUS:
        case MOD_INPUT_DDM2:
        case MOD_INPUT_DUAL_MIC:
            _decode_inputs(cmds[i].cmd_data, raw + pos, length_bytes,
                           &prog->inputs);
            break;
        case MOD_DFBC:
            prog->has_dfbc = true;
            _decode_dfbc(raw + pos, &prog->dfbc);
            break;
        case MOD_ENR:
            prog->has_enr = true;
            if (!_decode_enr(raw + pos, length_bytes, &prog->enr))
                return false;
            break;
        case MOD_ISS:
            prog->has_iss = true;
            _decode_iss(raw + pos, &prog->iss);
            break;
        case MOD_WNR:
            prog->has_wnr = true;
            _decode_wnr(raw + pos, &prog->wnr);
            break;
        case MOD_AGCO:
            prog->has_agco = true;
            _decode_agco(raw + pos, &prog->agco);
            break;
        default:
            break;
        }
        pos += length_bytes;
    }
    return true;
}

/* ============================================================
 * Convenience: Read + Parse in one call
 * ============================================================ */

bool bs300_program_read_and_parse(uint8_t program_index,
                                  bs300_program_data_t *prog)
{
    static uint8_t raw[BS300_TOTAL_DATA];

    if (!bs300_program_read(program_index, raw)) return false;
    return bs300_program_parse(raw, prog);
}
