#include "bs300_driver.h"
#include "system/includes.h"
#include "bs300_encode_tables.h"
#include "app_config.h"

/* ================================================================
 *  Bit-level reading helpers for Flash decode
 * ================================================================ */

typedef struct {
    const u8 *data;
    u16 byte_pos;
    u8  bit_pos;
} bit_reader_t;

static void br_init(bit_reader_t *br, const u8 *data)
{
    br->data = data;
    br->byte_pos = 0;
    br->bit_pos = 0;
}

static u32 br_read(bit_reader_t *br, u8 n_bits)
{
    u32 val = 0;
    u8 i;

    for (i = 0; i < n_bits; i++) {
        u8 byte_val = br->data[br->byte_pos];
        u8 bit = (byte_val >> br->bit_pos) & 1;
        val |= ((u32)bit << i);
        br->bit_pos++;
        if (br->bit_pos >= 8) {
            br->bit_pos = 0;
            br->byte_pos++;
        }
    }
    return val;
}

static void br_skip(bit_reader_t *br, u8 n_bits)
{
    u16 total = (u16)br->byte_pos * 8 + br->bit_pos + n_bits;
    br->byte_pos = total / 8;
    br->bit_pos = (u8)(total % 8);
}

/* ================================================================
 *  Module type lookup
 * ================================================================ */

typedef enum {
    MOD_WDRC = 1,
    MOD_VOLUME,
    MOD_INPUT,
    MOD_DFBC,
    MOD_ENR,
    MOD_ISS,
    MOD_WNR,
    MOD_AGCO,
    MOD_NOISE_GEN2,
    MOD_ACCLIM,
    MOD_UNKNOWN
} module_type_t;

static module_type_t get_module_type(u8 cmd_data)
{
    switch (cmd_data) {
    case 0x12: return MOD_WDRC;
    case 0x07: return MOD_VOLUME;
    case 0x03: case 0x04: case 0x05: case 0x06:
    case 0x17: case 0x1B: case 0x1E: return MOD_INPUT;
    case 0x14: return MOD_DFBC;
    case 0x1C: return MOD_ENR;
    case 0x1D: return MOD_ISS;
    case 0x1F: return MOD_WNR;
    case 0x23: return MOD_AGCO;
    case 0x21: return MOD_NOISE_GEN2;
    case 0x26: return MOD_ACCLIM;
    default:   return MOD_UNKNOWN;
    }
}

/* ================================================================
 *  WDRC Flash decode
 * ================================================================ */

static void decode_wdrc_channel(bit_reader_t *br, u8 ch_idx,
                                bs300_wdrc_t *wdrc)
{
    wdrc->freq_idx[ch_idx] = (u8)br_read(br, 6);
    wdrc->epd_at_idx[ch_idx] = (u8)br_read(br, 7);
    wdrc->epd_rt_idx[ch_idx] = (u8)br_read(br, 7);
    wdrc->epd_r_idx[ch_idx] = (u8)br_read(br, 7);
    br_skip(br, 2);
    wdrc->kp1_th_db[ch_idx] = (s8)br_read(br, 7);
    wdrc->kp2_th_db[ch_idx] = (s8)br_read(br, 7);
    br_skip(br, 2);
    wdrc->kp1_at_idx[ch_idx] = (u8)br_read(br, 7);
    wdrc->kp2_at_idx[ch_idx] = (u8)br_read(br, 7);
    br_skip(br, 2);
    wdrc->kp1_rt_idx[ch_idx] = (u8)br_read(br, 7);
    wdrc->kp2_rt_idx[ch_idx] = (u8)br_read(br, 7);
    br_skip(br, 2);
    wdrc->kp1_r_idx[ch_idx] = (u8)br_read(br, 7);
    wdrc->kp2_r_idx[ch_idx] = (u8)br_read(br, 7);
    wdrc->lmt_th_db[ch_idx] = (s8)br_read(br, 7);
    wdrc->lmt_at_idx[ch_idx] = (u8)br_read(br, 7);
    wdrc->lmt_rt_idx[ch_idx] = (u8)br_read(br, 7);
    wdrc->lmt_r_idx[ch_idx] = (u8)br_read(br, 7);
}

static int decode_wdrc_flash(const u8 *data, bs300_wdrc_t *wdrc)
{
    bit_reader_t br;
    u8 limiter;
    u8 kp_mode;
    u8 num_ch;
    u8 i;

    br_init(&br, data);

    br_skip(&br, 1);              /* B0 bit0: fixed 0b1 */
    limiter = (u8)br_read(&br, 1);
    kp_mode = (u8)br_read(&br, 1);
    br_skip(&br, 5);              /* B0 bits 7:3: 0 */

    br_skip(&br, 1);              /* B1 bit0: 0b1 marker */

    for (i = 0; i < 32; i++) {
        u8 raw_gain = (u8)br_read(&br, 7);
        wdrc->bin_gain[i] = (s8)((int)raw_gain - 27);
    }

    num_ch = (u8)br_read(&br, 5); /* B29[5:1] */
    br_skip(&br, 1);              /* B29[6]: reserved */

    wdrc->total_channels = num_ch;
    wdrc->kp_mode = (kp_mode == 0) ? 1 : 2;
    wdrc->limiter = limiter;

    wdrc->nsbc = 0;

    for (i = 0; i < num_ch; i++) {
        decode_wdrc_channel(&br, i, wdrc);
    }

    return 0;
}

/* ================================================================
 *  Volume/Beep Flash decode
 * ================================================================ */

static int decode_volume_flash(const u8 *data, bs300_modules_t *mod)
{
    mod->vol_enable = 1;
    mod->beep_level = data[0];
    mod->beep_freq_idx = data[1];
    mod->min_vol = (data[3] > 127) ? ((s8)data[3]) : (s8)data[3];
    mod->max_vol = (data[4] > 127) ? ((s8)data[4]) : (s8)data[4];
    mod->batt_beep_level = data[5];
    mod->batt_beep_freq_idx = data[6];
    return 0;
}

/* ================================================================
 *  Input selection mapping
 * ================================================================ */

static u8 cmd_data_to_input_selection(u8 cmd_data)
{
    switch (cmd_data) {
    case 0x03: return 0;  /* FrontMic */
    case 0x04: return 1;  /* RearMic */
    case 0x05: return 2;  /* Telecoil */
    case 0x06: return 3;  /* DAI */
    case 0x17: return 4;  /* MM Plus */
    case 0x1B: return 5;  /* DDM2 */
    case 0x1E: return 6;  /* Dual Mic */
    default:   return 0;
    }
}

/* ================================================================
 *  DFBC Flash decode
 * ================================================================ */

static int decode_dfbc_flash(const u8 *data, bs300_modules_t *mod)
{
    mod->dfbc_enable_mode = data[0];
    return 0;
}

/* ================================================================
 *  ENR Flash decode
 * ================================================================ */

static int decode_enr_flash(const u8 *data, bs300_enr_t *enr)
{
    bit_reader_t br;
    u8 nfsf;
    u8 nhsf;
    u8 nnsf;
    u8 num_ch_low;
    u8 num_ch_high;
    u8 num_ch;
    u8 snasf;
    u8 i;

    br_init(&br, data);

    nfsf = (u8)br_read(&br, 4);
    nhsf = (u8)br_read(&br, 4);
    nnsf = (u8)br_read(&br, 4);
    num_ch_low = (u8)br_read(&br, 4);
    num_ch_high = (u8)br_read(&br, 2);
    num_ch = num_ch_low | (num_ch_high << 4);

    enr->enable_num_ch = 0x80 | (num_ch + 1);
    enr->nfsf = (nfsf == 0) ? 1 : nfsf;
    enr->nhsf = (nhsf == 0) ? 1 : nhsf;
    enr->nnsf = (nnsf == 0) ? 1 : nnsf;

    for (i = 0; i < (num_ch + 1) && i < 16; i++) {
        enr->freq_idx[i] = (u8)br_read(&br, 6);
        enr->max_att_db[i] = (u8)br_read(&br, 5);
        enr->snr_th_db[i] = (u8)br_read(&br, 5);
        enr->noise_th_db[i] = (u8)br_read(&br, 6);
        enr->upper_noise_th_db[i] = (u8)br_read(&br, 6);
        enr->etr_x100[i] = (u8)br_read(&br, 7);
        enr->nrr_x10[i] = (u8)br_read(&br, 4);
    }

    snasf = (u8)br_read(&br, 4);
    enr->snasf = (snasf == 0) ? 1 : snasf;

    for (i = 0; i < 16; i++) {
        enr->sasf[i] = 1;
    }

    return 0;
}

/* ================================================================
 *  ISS Flash decode
 * ================================================================ */

static int decode_iss_flash(const u8 *data, bs300_modules_t *mod)
{
    mod->iss_enable = 1;
    mod->iss_threshold = data[0];
    return 0;
}

/* ================================================================
 *  WNR Flash decode
 * ================================================================ */

static int decode_wnr_flash(const u8 *data, bs300_modules_t *mod)
{
    mod->wnr_enable_dual = data[0] | 0x01;
    mod->wnr_preset = data[1];
    return 0;
}

/* ================================================================
 *  AGCO Flash decode
 * ================================================================ */

static int decode_agco_flash(const u8 *data, bs300_modules_t *mod)
{
    u16 attack;
    u16 release;

    attack = (u16)data[0] | ((u16)(data[1] & 0x0F) << 8);
    release = ((u16)(data[1] & 0xF0) >> 4) | ((u16)data[2] << 4);
    mod->agco_enable = 1;
    mod->agco_threshold_db = (s8)data[3];
    mod->agco_attack_01ms = attack;
    mod->agco_release_01ms = release;
    return 0;
}

/* ================================================================
 *  Main Flash → Struct conversion
 * ================================================================ */

int bs300_flash_to_struct(const u8 *flash_buf, bs300_prog_struct_t *out)
{
    u8 module_count;
    u8 i;
    u16 pos;
    int ret;

    if (flash_buf == NULL || out == NULL) {
        return -1;
    }
    {
        u16 idx;
        u8 *raw = (u8 *)out;
        for (idx = 0; idx < sizeof(bs300_prog_struct_t); idx++) {
            raw[idx] = 0x00;
        }
    }

    /* Default runtime values */
    out->modules.volume_level = 0;  /* default 0, user adjusts via bs300_set_volume */
    out->modules.eq_low = 0;
    out->modules.eq_mid = 0;
    out->modules.eq_high = 0;

    /* Validate header */
    if (flash_buf[1] != 0x80 || flash_buf[2] != 0x00) {
        bs300_debug("bs300: flash_to_struct invalid header\n");
        return -1;
    }
    module_count = flash_buf[3] - 1;
    if (module_count == 0 || module_count > 16) {
        bs300_debug("bs300: flash_to_struct bad module_count=%d\n",
                    module_count);
        return -1;
    }

    /* Parse module directory */
    pos = 4;
    for (i = 0; i < module_count; i++) {
        u8 cmd_data = flash_buf[pos];
        u8 length_words = flash_buf[pos + 2];
        (void)length_words;
        pos += 3;
        (void)cmd_data;
    }

    /* Skip FB 00 marker */
    pos += 2;

    /* Decode each module's data */
    for (i = 0; i < module_count; i++) {
        u8 cmd_data = flash_buf[4 + i * 3];
        u8 length_words = flash_buf[4 + i * 3 + 2];
        u16 length_bytes = (u16)length_words * 3;
        module_type_t mod_type = get_module_type(cmd_data);

        switch (mod_type) {
        case MOD_WDRC:
            ret = decode_wdrc_flash(flash_buf + pos, &out->wdrc);
            break;
        case MOD_VOLUME:
            ret = decode_volume_flash(flash_buf + pos, &out->modules);
            break;
        case MOD_INPUT:
            out->modules.input_selection =
                cmd_data_to_input_selection(cmd_data);
            if (out->modules.input_selection == 4) {
                out->modules.mm_plus_enable = 1;
            } else if (out->modules.input_selection == 5) {
                const u8 *ddm2 = flash_buf + pos;
                u8 byte2 = ddm2[2];

                out->modules.ddm2_enable = 1;
                out->modules.open_ear    = (byte2 >> 6) & 1;
                out->modules.adm_fdm     = (byte2 >> 5) & 1;
                out->modules.polar_pattern = byte2 & 0x07;
                out->modules.omni_threshold = ddm2[1];

                bs300_debug("bs300: [DDM2-Flash] raw: %02X %02X %02X %02X %02X %02X\n",
                            ddm2[0], ddm2[1], ddm2[2], ddm2[3], ddm2[4], ddm2[5]);
                bs300_debug("bs300: [DDM2-Flash] open=%d adm_fdm=%d polar=%d omni=%d cutoff=0x%06X\n",
                            out->modules.open_ear,
                            out->modules.adm_fdm,
                            out->modules.polar_pattern,
                            out->modules.omni_threshold,
                            (u32)(ddm2[3] | (ddm2[4] << 8) | (ddm2[5] << 16)));
            }
            ret = 0;
            break;
        case MOD_DFBC:
            ret = decode_dfbc_flash(flash_buf + pos, &out->modules);
            break;
        case MOD_ENR:
            ret = decode_enr_flash(flash_buf + pos, &out->enr);
            break;
        case MOD_ISS:
            ret = decode_iss_flash(flash_buf + pos, &out->modules);
            break;
        case MOD_WNR:
            ret = decode_wnr_flash(flash_buf + pos, &out->modules);
            break;
        case MOD_AGCO:
            ret = decode_agco_flash(flash_buf + pos, &out->modules);
            break;
        default:
            ret = 0;
            break;
        }
        if (ret < 0) {
            bs300_debug("bs300: flash_to_struct module=%d fail\n",
                        cmd_data);
            return ret;
        }
        pos += length_bytes;
    }

    bs300_debug("bs300: flash_to_struct ok, modules=%d, channels=%d\n",
                module_count, out->wdrc.total_channels);
    return 0;
}

/* ================================================================
 *  Lookup tables for Param I2C encoding
 * ================================================================ */

/* Frequency table: index → Hz, 32 entries, 250Hz step from idx 1 */
static const u16 freq_table[32] = {
    0, 125, 375, 625, 875, 1125, 1375, 1625,
    1875, 2125, 2375, 2625, 2875, 3125, 3375, 3625,
    3875, 4125, 4375, 4625, 4875, 5125, 5375, 5625,
    5875, 6125, 6375, 6625, 6875, 7125, 7375, 7625,
};

/* Beep frequency index → Hz mapping */
static const u16 beep_idx_to_hz[25] = {
    0, 250, 500, 750, 1000, 1250, 1500, 1750,
    2000, 2250, 2500, 2750, 3000, 3250, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/* WNR suppression strength offset: [band 0-31][preset 0-4] */
static const s8 wnr_ssp_offset[32][5] = {
    {  0, -16, -32, -48, -48 },
    {  0, -16, -32, -48, -48 },
    {  0, -16, -32, -48, -48 },
    { 10,  -6, -22, -36, -48 },
    { 10,  -6, -22, -36, -48 },
    { 10,  -6, -22, -36, -48 },
    {-10, -26, -42, -42, -42 },
    {-10, -26, -42, -42, -42 },
    {-10, -26, -42, -42, -42 },
    {-10, -26, -42, -42, -42 },
    {-10, -26, -42, -42, -42 },
    {-10, -26, -42, -42, -42 },
    {-10, -26, -42, -42, -42 },
    {-10, -26, -42, -42, -42 },
    {-10, -26, -42, -42, -42 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
    {-20, -36, -52, -52, -52 },
};

/* WNR single-mic detection offset: [band 0-2][preset 0-4] */
static const s8 wnr_data2_offset[3][5] = {
    {  0,  -8, -16, -26, -34 },
    { -8, -16, -24, -32, -40 },
    {-40, -50, -58, -68, -78 },
};

/* WDRC KP per-channel calibration offset (cross-validated with codegen Step 5) */
static const s8 wdrc_kp_cal_offset[16] = {
    0, 0, 0, 1, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

/* WDRC Lmt per-channel calibration offset (cross-validated with codegen Step 5) */
static const s8 wdrc_lmt_cal_offset[16] = {
    0, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0,
};

/* WNR preset index → SSP level mapping */
static const u8 wnr_preset_to_ssp[5] = { 0, 1, 3, 6, 12 };

/* ================================================================
 *  Math helpers (integer arithmetic, matching chip behavior)
 * ================================================================ */

static s32 clamp_s32(s32 v, s32 lo, s32 hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static u32 db_to_frac24(s32 n_tenths_db)
{
    /* ceil(val_db * 65536 / 6.02) as unsigned frac24.
     * n_tenths_db = round(val_db * 10).
     * scaling: (n/10) * 65536 / 6.02 = n * 327680 / 301 */
    return ((n_tenths_db * 327680 + 300) / 301) & 0xFFFFFF;
}

static u32 db_to_int24(s32 n_tenths_db)
{
    /* trunc(val_db * 65536 / 6.02) as signed int24. */
    s32 result;
    if (n_tenths_db >= 0) {
        result = (n_tenths_db * 327680) / 301;
    } else {
        result = -((-n_tenths_db * 327680) / 301);
    }
    return result & 0xFFFFFF;
}

#define INPUT_TYPE_MIC       0
#define INPUT_TYPE_TELECOIL  1
#define INPUT_TYPE_DAI       2

/* Input gain diff in tenth-dB (matches codegen input_gain_diff_db / 10.0).
 * Returns raw calibration value: telecoil_gain_diff or dai_gain_diff. */
static s32 get_input_gain_diff_tenth_db(u8 input_type, const bs300_calib_t *calib)
{
    if (input_type == INPUT_TYPE_TELECOIL) {
        return (s32)calib->telecoil_gain_diff;
    } else if (input_type == INPUT_TYPE_DAI) {
        return (s32)calib->dai_gain_diff;
    }
    return 0;
}

/* Apply input_gain_diff (tenth-dB) to a value computed in tenth-dB,
 * then truncate to integer dB (matching Python int() → C truncation toward zero).
 * numer_tenth_db: numerator in tenth-dB units.
 * igd: input_gain_diff in tenth-dB.
 * Returns: trunc((numer_tenth_db - igd) / 10). */
static s32 apply_igd_trunc(s32 numer_tenth_db, s32 igd)
{
    return (numer_tenth_db - igd) / 10;
}

/* Round-half-away-from-zero: (num + den/2) / den for num>=0, (num - den/2) / den for num<0.
 * Matches Python round() for values not at exact .5 boundary. */
static s32 round_div(s32 num, s32 den)
{
    if (num >= 0) {
        return (num + den / 2) / den;
    } else {
        return (num - den / 2) / den;
    }
}

/* ENR NT/UNT integer formula:
 * val = round(5.307 * (nt + 130 - mic1_cal - igd/10.0) - 371.2)
 * Integer: x10 = nt*10 + 1300 - mic1_cal*10 - igd
 *          val = round_div(5307 * x10 - 3712000, 10000) */
static s32 enr_nt_int(u8 nt_db, s32 mic1_cal, s32 igd)
{
    s32 x10 = (s32)nt_db * 10 + 1300 - mic1_cal * 10 - igd;
    s32 num = 5307 * x10 - 3712000;
    return round_div(num, 10000);
}

/* ================================================================
 *  Packing helpers for 48-byte data section
 * ================================================================ */

static void pack_bytes(u8 *data, u8 start, const u8 *values, u8 count)
{
    u8 i;
    for (i = 0; i < count; i++) {
        data[start + i] = values[i];
    }
}

static void pack_int12_2pw(u8 *data, u8 word_start, const s16 *values, u8 count)
{
    u8 i;
    for (i = 0; i < count; i++) {
        u8 wi = word_start + i / 2;
        u32 w = (u32)data[wi * 3] | ((u32)data[wi * 3 + 1] << 8)
              | ((u32)data[wi * 3 + 2] << 16);
        if (i % 2 == 0) {
            w = (w & 0xFFF000) | ((u32)values[i] & 0xFFF);
        } else {
            w = (w & 0x000FFF) | (((u32)values[i] & 0xFFF) << 12);
        }
        data[wi * 3]     = (u8)(w & 0xFF);
        data[wi * 3 + 1] = (u8)((w >> 8) & 0xFF);
        data[wi * 3 + 2] = (u8)((w >> 16) & 0xFF);
    }
}

static void pack_uint6_4pw(u8 *data, u8 word_start, const u8 *values, u8 count)
{
    u8 i;
    for (i = 0; i < count; i++) {
        u8 wi = word_start + i / 4;
        u8 shift = (u8)((3 - (i % 4)) * 6);
        u32 mask = (u32)0x3F << shift;
        u32 w = (u32)data[wi * 3] | ((u32)data[wi * 3 + 1] << 8)
              | ((u32)data[wi * 3 + 2] << 16);
        w = (w & ~mask) | (((u32)values[i] & 0x3F) << shift);
        data[wi * 3]     = (u8)(w & 0xFF);
        data[wi * 3 + 1] = (u8)((w >> 8) & 0xFF);
        data[wi * 3 + 2] = (u8)((w >> 16) & 0xFF);
    }
}

static void set_word(u8 *data, u8 n, u32 value)
{
    u16 off = (u16)n * 3;
    data[off]     = (u8)(value & 0xFF);
    data[off + 1] = (u8)((value >> 8) & 0xFF);
    data[off + 2] = (u8)((value >> 16) & 0xFF);
}

/* ================================================================
 *  Input type constants
 * ================================================================ */

static u8 get_input_type(u8 input_selection)
{
    switch (input_selection) {
    case 2: return INPUT_TYPE_TELECOIL;  /* Telecoil */
    case 3: return INPUT_TYPE_DAI;       /* DAI */
    default: return INPUT_TYPE_MIC;      /* FrontMic(0), RearMic(1), MM+(4), DDM2(5), DualMic(6) */
    }
}

/* ================================================================
 *  Calibration band lookup
 * ================================================================ */

static u8 freq_to_cal_band(u16 hz)
{
    u8 best = 0;
    s32 best_dist = 999999;
    u8 i;
    for (i = 1; i < 32; i++) {
        s32 d = (s32)hz - (s32)freq_table[i];
        if (d < 0) d = -d;
        if (d < best_dist) {
            best_dist = d;
            best = i;
        }
    }
    return best;
}

/* ================================================================
 *  Calibration data parser
 * ================================================================ */

int bs300_parse_calibration(const u8 *raw, bs300_calib_t *out)
{
    u8 i;

    if (raw == NULL || out == NULL) {
        return -1;
    }

    if (raw[0] != 3 || raw[2] != 9) {
        bs300_debug("bs300: calib invalid header: [0]=%d [2]=%d\n",
                    raw[0], raw[2]);
        return -1;
    }

    if (raw[20] != 0x02 || raw[21] != 0xFA || raw[22] != 0x00) {
        bs300_debug("bs300: calib mic1 header mismatch\n");
        return -1;
    }
    for (i = 0; i < 32; i++) {
        out->mic1_band[i] = raw[23 + i];
    }

    if (raw[55] != 0x01 || raw[56] != 0xFA || raw[57] != 0x00) {
        bs300_debug("bs300: calib output header mismatch\n");
        return -1;
    }
    for (i = 0; i < 32; i++) {
        out->output_band[i] = raw[58 + i];
    }

    out->mic2_gain_diff       = (s16)((u16)raw[91] | ((u16)raw[92] << 8));
    out->mic_delay            = (u16)((u16)raw[94] | ((u16)raw[95] << 8));
    out->telecoil_gain_diff   = (s16)((u16)raw[97] | ((u16)raw[98] << 8));
    out->dai_gain_diff        = (s16)((u16)raw[100] | ((u16)raw[101] << 8));
    out->fbc_bulk_delay       = (u16)((u16)raw[103] | ((u16)raw[104] << 8));

    bs300_debug("bs300: calib parsed: mic1[1]=%d out[1]=%d "
                "tc_gd=%d dai_gd=%d fbc_bd=%d\n",
                out->mic1_band[1], out->output_band[1],
                out->telecoil_gain_diff, out->dai_gain_diff,
                out->fbc_bulk_delay);
    return 0;
}

/* ================================================================
 *  ENR helpers: band counts and multi-band mic1 averaging
 * ================================================================ */

/* Derive per-channel calibration band count from ENR freq indices.
 * Channel i covers cal bands from freq_idx[i] to freq_idx[i+1]-1
 * (or 31 for the last channel). */
static void enr_compute_band_counts(const bs300_enr_t *enr, u8 *band_counts)
{
    u8 total_ch = enr->enable_num_ch & 0x3F;
    u8 i;
    for (i = 0; i < 16; i++) {
        if (i < total_ch) {
            u8 start = enr->freq_idx[i];
            u8 end = (i + 1 < total_ch) ? enr->freq_idx[i + 1] : 32;
            band_counts[i] = (end > start) ? (end - start) : 1;
        } else {
            band_counts[i] = 0;
        }
    }
}

/* Compute mic1_cal for one ENR channel using multi-band averaging.
 * Formula from chip: sum(mic1_band[fidx..fidx+cnt-1]) * 10 // cnt,
 * then 四舍五入 (round half up). */
static s32 enr_mic1_cal_avg(const bs300_calib_t *calib, u8 fidx, u8 cnt)
{
    s32 s = 0;
    u8 j;
    for (j = 0; j < cnt; j++) {
        s += (s32)calib->mic1_band[fidx + j];
    }
    {
        s32 t = s * 10 / (s32)cnt;
        return (t / 10) + ((t % 10) >= 5 ? 1 : 0);
    }
}

/* ================================================================
 *  WDRC Param Encoders (11 commands)
 *  Matches codegen.py Step 5 cross-validated implementations.
 * ================================================================ */

int bs300_encode_wdrc_general(const bs300_wdrc_t *wdrc, u8 *data)
{
    u8 nmbc;
    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);
    nmbc = wdrc->total_channels - wdrc->nsbc;
    set_word(data, 0, 0x000001);                    /* selection: enable */
    set_word(data, 1, wdrc->total_channels);
    set_word(data, 2, wdrc->nsbc);
    set_word(data, 3, nmbc);
    set_word(data, 4, (wdrc->kp_mode == 1) ? 2 : 3); /* 2=1KP, 3=2KP */
    set_word(data, 5, wdrc->limiter ? 1 : 0);
    return 0;
}

int bs300_encode_wdrc_freq_spacing(const bs300_wdrc_t *wdrc, u8 *data)
{
    u8 mbc_ch[16];
    u8 nmbc;
    u8 i, wi;

    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);
    nmbc = wdrc->total_channels - wdrc->nsbc;

    /* MBC_CHx = (bin_count - 1), minimum 1. Estimate bin distribution. */
    {
        u8 bins_per_ch = (nmbc > 0) ? (u8)(32 / nmbc) : 2;
        for (i = 0; i < 16; i++) {
            mbc_ch[i] = 1; /* NOBC default: 0b000001 */
        }
        for (i = 0; i < nmbc && i < 16; i++) {
            mbc_ch[i] = (bins_per_ch >= 2) ? (u8)(bins_per_ch - 1) : 1;
        }
    }
    pack_uint6_4pw(data, 0, mbc_ch, 16);
    for (wi = (u8)((16 + 3) / 4); wi < 16; wi++) {
        set_word(data, wi, 0x041041);
    }
    return 0;
}

int bs300_encode_wdrc_kp_threshold(const bs300_wdrc_t *wdrc,
                                    const bs300_calib_t *calib,
                                    u8 input_type, u8 *data)
{
    u8 values[32];
    u8 i, cnt;
    s32 igd;

    if (wdrc == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);
    igd = get_input_gain_diff_tenth_db(input_type, calib);
    cnt = 0;

    for (i = 0; i < wdrc->total_channels && i < 16; i++) {
        u8 fidx = wdrc->freq_idx[i];
        s32 mic1_cal;
        s32 numer_tenth;

        /* 2-band calibration averaging per WDRC channel */
        if (fidx < 31) {
            mic1_cal = ((s32)calib->mic1_band[fidx]
                      + (s32)calib->mic1_band[fidx + 1]) / 2;
        } else {
            mic1_cal = (s32)calib->mic1_band[fidx];
        }
        mic1_cal += (s32)wdrc_kp_cal_offset[i];

        /* encode: int(60 + th - mic1_cal - igd/10.0) = trunc((600 + th*10 - mic1_cal*10 - igd) / 10) */
        numer_tenth = 600 + (s32)wdrc->kp1_th_db[i] * 10 - mic1_cal * 10;
        values[cnt++] = (u8)clamp_s32(apply_igd_trunc(numer_tenth, igd), -128, 127);

        if (wdrc->kp_mode == 2) {
            numer_tenth = 600 + (s32)wdrc->kp2_th_db[i] * 10 - mic1_cal * 10;
            values[cnt++] = (u8)clamp_s32(apply_igd_trunc(numer_tenth, igd), -128, 127);
        }
    }
    pack_bytes(data, 0, values, cnt);
    return 0;
}

int bs300_encode_wdrc_attack_time(const bs300_wdrc_t *wdrc, u8 *data)
{
    u8 i;

    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    if (wdrc->kp_mode == 1) {
        u8 values[32];
        u8 cnt = 0;
        for (i = 0; i < wdrc->total_channels && i < 16; i++) {
            values[cnt++] = wdrc->epd_at_idx[i];
            values[cnt++] = wdrc->kp1_at_idx[i];
        }
        pack_bytes(data, 0, values, cnt);
    } else {
        for (i = 0; i < wdrc->total_channels && i < 16; i++) {
            u32 w = (u32)wdrc->epd_at_idx[i]
                  | ((u32)wdrc->kp1_at_idx[i] << 8)
                  | ((u32)wdrc->kp2_at_idx[i] << 16);
            set_word(data, i, w);
        }
    }
    return 0;
}

int bs300_encode_wdrc_release_time(const bs300_wdrc_t *wdrc, u8 *data)
{
    u8 i;

    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    if (wdrc->kp_mode == 1) {
        u8 values[32];
        u8 cnt = 0;
        for (i = 0; i < wdrc->total_channels && i < 16; i++) {
            values[cnt++] = wdrc->epd_rt_idx[i];
            values[cnt++] = wdrc->kp1_rt_idx[i];
        }
        pack_bytes(data, 0, values, cnt);
    } else {
        for (i = 0; i < wdrc->total_channels && i < 16; i++) {
            u32 w = (u32)wdrc->epd_rt_idx[i]
                  | ((u32)wdrc->kp1_rt_idx[i] << 8)
                  | ((u32)wdrc->kp2_rt_idx[i] << 16);
            set_word(data, i, w);
        }
    }
    return 0;
}

int bs300_encode_wdrc_ratio(const bs300_wdrc_t *wdrc, u8 *data)
{
    u8 i;

    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    if (wdrc->kp_mode == 1) {
        u8 values[32];
        u8 cnt = 0;
        for (i = 0; i < wdrc->total_channels && i < 16; i++) {
            values[cnt++] = wdrc->epd_r_idx[i];
            values[cnt++] = wdrc->kp1_r_idx[i];
        }
        pack_bytes(data, 0, values, cnt);
    } else {
        for (i = 0; i < wdrc->total_channels && i < 16; i++) {
            u32 w = (u32)wdrc->epd_r_idx[i]
                  | ((u32)wdrc->kp1_r_idx[i] << 8)
                  | ((u32)wdrc->kp2_r_idx[i] << 16);
            set_word(data, i, w);
        }
    }
    return 0;
}

static s8 get_eq_gain_for_band(const bs300_modules_t *mod, u8 band)
{
    u16 hz;

    if (mod == NULL || band >= 32) return 0;
    if (band == 0) return mod->eq_low;
    hz = freq_table[band];
    if (hz < 500)       return mod->eq_low;
    else if (hz <= 2000) return mod->eq_mid;
    else                return mod->eq_high;
}

int bs300_encode_wdrc_bin_gain(const bs300_wdrc_t *wdrc,
                                const bs300_calib_t *calib,
                                const bs300_modules_t *mod,
                                u8 input_type, u8 *data)
{
    u8 values[32];
    u8 i;
    s32 igd;
    s32 vol_gain;

    if (wdrc == NULL || calib == NULL || mod == NULL || data == NULL) return -1;
    memset(data, 0, 48);
    igd = get_input_gain_diff_tenth_db(input_type, calib);
    vol_gain = ((s32)mod->volume_level - 9) * 3;  // 0→-27dB ... 9→0dB
    printf("[BS300] encode bin_gain, volume_level=%d (vol_gain=%ddB), eq_low=%d eq_mid=%d eq_high=%d, input_type=%d igd=%d\n",
           mod->volume_level, vol_gain, mod->eq_low, mod->eq_mid, mod->eq_high, input_type, (int)igd);

    printf("[BS300] bin_gain raw[0..31]: ");
    put_buf((u8 *)wdrc->bin_gain, 32);
    printf("[BS300] out_cal  [0..31]: ");
    put_buf((u8 *)calib->output_band, 32);
    printf("[BS300] mic1_cal [0..31]: ");
    put_buf((u8 *)calib->mic1_band, 32);

    /* Formula: int(baseline + vol*5 + eq - (out_cal - mic1_cal) + igd/10.0) */
    for (i = 0; i < 32; i++) {
        s32 gain_cal = (s32)calib->output_band[i] - (s32)calib->mic1_band[i];
        s32 baseline = (s32)wdrc->bin_gain[i] + vol_gain
                      + (s32)get_eq_gain_for_band(mod, i);
        s32 numer_tenth = baseline * 10 - gain_cal * 10;
        values[i] = (u8)clamp_s32(apply_igd_trunc(numer_tenth, -igd), -128, 127);
    }
    pack_bytes(data, 0, values, 32);

    printf("[BS300] values[0..31]: ");
    put_buf(values, 32);
    printf("[BS300] iic data[0..47]: ");
    put_buf(data, 48);
    return 0;
}

int bs300_encode_wdrc_lmt_threshold(const bs300_wdrc_t *wdrc,
                                     const bs300_calib_t *calib, u8 *data)
{
    u8 i;

    if (wdrc == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    /* Formula: int(60 + th - avg_output_cal_per_ch)
     * avg = (output_band[fidx] + output_band[fidx+1])/2 + cal_offset */
    for (i = 0; i < wdrc->total_channels && i < 16; i++) {
        u8 fidx = wdrc->freq_idx[i];
        s32 out_cal;
        s32 encoded;
        if (fidx < 31) {
            out_cal = ((s32)calib->output_band[fidx]
                     + (s32)calib->output_band[fidx + 1]) / 2;
        } else {
            out_cal = (s32)calib->output_band[fidx];
        }
        out_cal += (s32)wdrc_lmt_cal_offset[i];
        encoded = 60 + (s32)wdrc->lmt_th_db[i] - out_cal;
        data[i] = (u8)clamp_s32(encoded, -128, 127);
    }
    return 0;
}

int bs300_encode_wdrc_lmt_attack(const bs300_wdrc_t *wdrc, u8 *data)
{
    u8 i;
    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);
    for (i = 0; i < wdrc->total_channels && i < 16; i++) {
        data[i] = wdrc->lmt_at_idx[i];
    }
    return 0;
}

int bs300_encode_wdrc_lmt_release(const bs300_wdrc_t *wdrc, u8 *data)
{
    u8 i;
    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);
    for (i = 0; i < wdrc->total_channels && i < 16; i++) {
        data[i] = wdrc->lmt_rt_idx[i];
    }
    return 0;
}

int bs300_encode_wdrc_lmt_ratio(const bs300_wdrc_t *wdrc, u8 *data)
{
    u8 i;
    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);
    for (i = 0; i < wdrc->total_channels && i < 16; i++) {
        data[i] = wdrc->lmt_r_idx[i];
    }
    return 0;
}

/* ================================================================
 *  ENR Param Encoders (10 commands)
 *  Matches codegen.py Step 5 cross-validated implementations.
 * ================================================================ */

int bs300_encode_enr_general(const bs300_enr_t *enr, u8 *data)
{
    u8 ena, total_ch, sbc, mbc;
    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    ena = (enr->enable_num_ch & 0x80) ? 1 : 0;
    total_ch = enr->enable_num_ch & 0x3F;
    sbc = 2;
    mbc = total_ch - sbc;

    set_word(data, 0, ena);
    set_word(data, 1, total_ch);
    set_word(data, 2, sbc);
    set_word(data, 3, mbc);
    return 0;
}

int bs300_encode_enr_freq_spacing(const bs300_enr_t *enr, u8 *data)
{
    u8 band_counts[16];
    u8 total_ch;
    u8 i;

    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    enr_compute_band_counts(enr, band_counts);
    pack_uint6_4pw(data, 0, band_counts, 16);
    return 0;
}

int bs300_encode_enr_snr_threshold(const bs300_enr_t *enr, u8 *data)
{
    s16 encoded[16];
    u8 i, total_ch;

    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    /* SNRT_CHx = floor(32/6.02 * value), matched by integer arithmetic:
     * int(v * 32.0 / 6.02) = v * 1600 / 301 (truncation toward zero) */
    for (i = 0; i < total_ch && i < 16; i++) {
        encoded[i] = (s16)clamp_s32((s32)enr->snr_th_db[i] * 1600 / 301,
                                    0, 4095);
    }
    for (i = total_ch; i < 16; i++) {
        encoded[i] = 0;
    }
    pack_int12_2pw(data, 0, encoded, 16);
    return 0;
}

int bs300_encode_enr_max_att(const bs300_enr_t *enr, u8 *data)
{
    s16 encoded[16];
    u8 i, total_ch;

    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    /* MAR_CHx = floor((max_att / max(snr_th, 1)) * 256) */
    for (i = 0; i < total_ch && i < 16; i++) {
        s32 st = (enr->snr_th_db[i] > 0) ? (s32)enr->snr_th_db[i] : 1;
        encoded[i] = (s16)clamp_s32((s32)enr->max_att_db[i] * 256 / st,
                                    0, 4095);
    }
    for (i = total_ch; i < 16; i++) {
        encoded[i] = 0;
    }
    pack_int12_2pw(data, 0, encoded, 16);
    return 0;
}

int bs300_encode_enr_noise_th(const bs300_enr_t *enr,
                               const bs300_calib_t *calib,
                               u8 input_type, u8 *data)
{
    s16 encoded[16];
    u8 band_counts[16];
    u8 total_ch;
    s32 igd;
    u8 i;

    if (enr == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    igd = get_input_gain_diff_tenth_db(input_type, calib);
    enr_compute_band_counts(enr, band_counts);

    /* NT_CHx = round(5.307 * (x + 130 - mic1Cal - input_gain) - 371.2)
     * mic1Cal = 四舍五入(sum(mic1_band[fidx..fidx+cnt-1]) / cnt) */
    for (i = 0; i < total_ch && i < 16; i++) {
        u8 fidx = enr->freq_idx[i];
        u8 cnt = band_counts[i];
        s32 mic1_cal;
        s32 val;

        if (cnt > 0 && fidx + cnt <= 32) {
            mic1_cal = enr_mic1_cal_avg(calib, fidx, cnt);
        } else {
            mic1_cal = (s32)calib->mic1_band[fidx < 32 ? fidx : 1];
        }

        /* Integer formula: val = round(5.307 * (nt + 130 - mic1_cal - igd/10.0) - 371.2)
         * matches codegen.py cross-validated output byte-for-byte. */
        val = enr_nt_int(enr->noise_th_db[i], mic1_cal, igd);
        encoded[i] = (s16)clamp_s32(val, -2048, 2047);
    }
    for (i = total_ch; i < 16; i++) {
        encoded[i] = 0;
    }
    pack_int12_2pw(data, 0, encoded, 16);
    return 0;
}

int bs300_encode_enr_upper_noise_th(const bs300_enr_t *enr,
                                     const bs300_calib_t *calib,
                                     u8 input_type, u8 *data)
{
    s16 encoded[16];
    u8 band_counts[16];
    u8 total_ch;
    s32 igd;
    u8 i;

    if (enr == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    igd = get_input_gain_diff_tenth_db(input_type, calib);
    enr_compute_band_counts(enr, band_counts);

    /* Same formula as Noise Threshold but uses upper_noise_th_db */
    for (i = 0; i < total_ch && i < 16; i++) {
        u8 fidx = enr->freq_idx[i];
        u8 cnt = band_counts[i];
        s32 mic1_cal;
        s32 val;

        if (cnt > 0 && fidx + cnt <= 32) {
            mic1_cal = enr_mic1_cal_avg(calib, fidx, cnt);
        } else {
            mic1_cal = (s32)calib->mic1_band[fidx < 32 ? fidx : 1];
        }

        val = enr_nt_int(enr->upper_noise_th_db[i], mic1_cal, igd);
        encoded[i] = (s16)clamp_s32(val, -2048, 2047);
    }
    for (i = total_ch; i < 16; i++) {
        encoded[i] = 0;
    }
    pack_int12_2pw(data, 0, encoded, 16);
    return 0;
}

int bs300_encode_enr_smoothing(const bs300_enr_t *enr, u8 *data)
{
    u8 i;

    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    /* Fixed values for first 6 words (from codegen) */
    set_word(data, 0, 0x200000);
    set_word(data, 1, 0x600000);
    set_word(data, 2, 0x100000);
    set_word(data, 3, 0x700000);
    set_word(data, 4, 0x020000);
    set_word(data, 5, 0x7E0000);

    /* Smoothing factors: nhsf, nfsf, nnsf, snasf */
    {
        u8 sf_vals[4];
        u8 sf_keys[4];
        sf_vals[0] = enr->nhsf;   sf_keys[0] = 0;
        sf_vals[1] = enr->nfsf;   sf_keys[1] = 1;
        sf_vals[2] = enr->nnsf;   sf_keys[2] = 2;
        sf_vals[3] = 4;  /* chip overrides snasf to 4 regardless of stored value */

        for (i = 0; i < 4; i++) {
            u8 val = sf_vals[i];
            u32 d1, d2;
            u8 w1, w2;
            if (val >= 2) {
                d1 = (u32)1 << (23 - val);
                d2 = 0x7FFFFF - d1 + 1;
            } else {
                d1 = 0x7FFFFF;
                d2 = 0x000001;
            }
            switch (i) {
            case 0: w1 = 6;  w2 = 7;  break;
            case 1: w1 = 8;  w2 = 9;  break;
            case 2: w1 = 10; w2 = 11; break;
            case 3: w1 = 13; w2 = 14; break;
            default: continue;
            }
            set_word(data, w1, d1);
            set_word(data, w2, d2);
        }
    }
    set_word(data, 12, 0x004000);
    return 0;
}

int bs300_encode_enr_etr(const bs300_enr_t *enr, u8 *data)
{
    u8 i, total_ch;

    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    /* coded = (s64)2524971008 * (etr_x100 - 100) / (1600 * etr_x100 * ma)
     * 301 * 0x800000 = 2524971008, fits u32. Product needs s64.
     * Verified byte-for-byte against codegen.py float output. */
    for (i = 0; i < total_ch && i < 16; i++) {
        s32 etr_x100 = (s32)enr->etr_x100[i];
        s32 ma = (s32)(enr->max_att_db[i] > 0 ? enr->max_att_db[i] : 1);
        s64 num = 2524971008ULL * (s64)(etr_x100 - 100);
        s32 den = 1600 * etr_x100 * ma;
        s32 coded = (s32)(num / (s64)den);
        set_word(data, i, (u32)(coded & 0xFFFFFF));
    }
    return 0;
}

int bs300_encode_enr_nrr(const bs300_enr_t *enr, u8 *data)
{
    u8 i, total_ch;

    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    /* coded = (s64)2524970707 * nrr_x10 / (16000 * ma)
     * 301 * 0x7FFFFF = 2524970707, fits u32. Product needs s64.
     * Verified byte-for-byte against codegen.py float output. */
    for (i = 0; i < total_ch && i < 16; i++) {
        s32 nrr_x10 = (s32)enr->nrr_x10[i];
        s32 ma = (s32)(enr->max_att_db[i] > 0 ? enr->max_att_db[i] : 1);
        s64 num = 2524970707ULL * (s64)nrr_x10;
        s32 den = 16000 * ma;
        s32 coded = (s32)(num / (s64)den);
        set_word(data, i, (u32)(coded & 0xFFFFFF));
    }
    return 0;
}

int bs300_encode_enr_sasf(const bs300_enr_t *enr, u8 *data)
{
    u8 i, total_ch;

    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    /* SASF: if sv >= 2, data1 = 1 << (23 - sv), else data1 = 0x7FFFFF.
     * Only data1 is sent per codegen (data2 is computed but not stored). */
    for (i = 0; i < total_ch && i < 16; i++) {
        u8 sv = enr->sasf[i];
        u32 d1;
        if (sv >= 2) {
            d1 = (u32)1 << (23 - sv);
        } else {
            d1 = 0x7FFFFF;
        }
        set_word(data, i, d1);
    }
    return 0;
}

/* ================================================================
 *  Volume/Beep/Input Param Encoder
 *  Matches codegen.py encode_volume_beep_param.
 * ================================================================ */

int bs300_encode_volume_beep(const bs300_modules_t *mod,
                              const bs300_calib_t *calib, u8 *data)
{
    u16 beep_hz, batt_hz;
    u8 beep_band, batt_band;
    s32 outcal_beep, outcal_batt;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    /* Beep level: frac24 = 0x7FFFFF * 10^((beep_level - outCal) / 20)
     * Use lookup table: index = (beep_level - outcal_beep) + 255 */
    beep_hz = (mod->beep_freq_idx < 25)
              ? beep_idx_to_hz[mod->beep_freq_idx] : 1000;
    beep_band = freq_to_cal_band(beep_hz);
    outcal_beep = (s32)calib->output_band[beep_band];
    {
        s32 x = (s32)mod->beep_level - outcal_beep;
        s32 idx = x + 255;
        if (idx < 0) idx = 0;
        if (idx >= BS300_BEEP_TABLE_SIZE) idx = BS300_BEEP_TABLE_SIZE - 1;
        set_word(data, 0, bs300_beep_frac24_table[idx]);
    }

    set_word(data, 1, beep_hz);
    set_word(data, 2, db_to_int24((s32)mod->min_vol * 10));
    set_word(data, 3, db_to_int24((s32)mod->max_vol * 10));
    /* Translate struct encoding → protocol encoding for input_selection:
     * struct: 0=FrontMic 1=RearMic 2=Telecoil 3=DAI 4=MM+ 5=DDM2 6=DualMic
     * proto:  0=FrontMic 1=Telecoil 2=DAI 3=RearMic 4=DDM2 5=MM+Tel 6=MM+DAI */
    {
        static const u8 s_input_struct_to_proto[7] =
            { 0, 3, 1, 2, 5, 4, 6 };
        u8 sel = mod->input_selection;
        if (sel < 7) sel = s_input_struct_to_proto[sel];
        set_word(data, 4, sel);
    }

    /* Battery flat beep (same lookup table logic) */
    batt_hz = (mod->batt_beep_freq_idx < 25)
              ? beep_idx_to_hz[mod->batt_beep_freq_idx] : 1000;
    batt_band = freq_to_cal_band(batt_hz);
    set_word(data, 5, batt_hz);
    outcal_batt = (s32)calib->output_band[batt_band];
    {
        s32 x = (s32)mod->batt_beep_level - outcal_batt;
        s32 idx = x + 255;
        if (idx < 0) idx = 0;
        if (idx >= BS300_BEEP_TABLE_SIZE) idx = BS300_BEEP_TABLE_SIZE - 1;
        set_word(data, 6, bs300_beep_frac24_table[idx]);
    }

    return 0;
}

/* ================================================================
 *  DFBC Param Encoder
 *  Matches codegen.py encode_dfbc_param.
 * ================================================================ */

int bs300_encode_dfbc(const bs300_modules_t *mod,
                       const bs300_calib_t *calib, u8 *data)
{
    u32 delay_n;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    set_word(data, 0, mod->dfbc_enable_mode & 0x0F);
    /* delay_n = round(fbc_bulk_delay_us / 62.5) = round(bd * 10 / 625) */
    delay_n = (u32)(((u32)calib->fbc_bulk_delay * 10 + 312) / 625);
    if (delay_n > 524) delay_n = 524;
    set_word(data, 1, delay_n);
    return 0;
}

/* ================================================================
 *  ISS Param Encoder
 *  Matches codegen.py encode_iss_param.
 * ================================================================ */

int bs300_encode_iss(const bs300_modules_t *mod,
                      const bs300_calib_t *calib,
                      u8 input_type, u8 *data)
{
    s32 all_mic1;
    s32 mic1_cal;
    s32 igd;
    u8 i;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    set_word(data, 0, mod->iss_enable ? 1 : 0);

    if (!mod->iss_enable) return 0;

    /* mic1_cal = round(sum of all 32 mic1 bands / 32) */
    all_mic1 = 0;
    for (i = 0; i < 32; i++) {
        all_mic1 += (s32)calib->mic1_band[i];
    }
    mic1_cal = (all_mic1 + 16) / 32;

    igd = get_input_gain_diff_tenth_db(input_type, calib);

    /* ISS frac48 = round(1/10^exponent * 2^47), exponent = (-3-thr+mic1_cal+igd_dB)/10.
     * Table covers exponent_tenth = [0, 100], BS300_ISS_TABLE_OFFSET=0. */
    {
        s32 exp_tenth_numer = (-30 - (s32)mod->iss_threshold * 10
                                + mic1_cal * 10 + igd);
        s32 exp_tenth = round_div(exp_tenth_numer, 10);
        s32 idx = exp_tenth - BS300_ISS_TABLE_OFFSET;
        if (idx < 0) idx = 0;
        if (idx >= BS300_ISS_TABLE_SIZE) idx = BS300_ISS_TABLE_SIZE - 1;
        set_word(data, 1, bs300_iss_frac48_table[idx][0]);
        set_word(data, 2, bs300_iss_frac48_table[idx][1]);
    }
    return 0;
}

/* ================================================================
 *  WNR Param Encoders (4 commands)
 *  Matches codegen.py Step 5 cross-validated implementations.
 * ================================================================ */

int bs300_encode_wnr_setup(const bs300_modules_t *mod,
                            const bs300_calib_t *calib, u8 *data)
{
    s32 all_mic1, avg_ceil;
    u8 ssp;
    u8 i;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    /* word 0: selection (bit0=enable, bit1=dual_mic) */
    set_word(data, 0, mod->wnr_enable_dual & 0x03);

    /* word 1: detection level = round((75 - ceil(avg_mic1)) * (65536/6.02/8))
     * Integer: round((75 - avg_ceil) * 409600 / 301) */
    all_mic1 = 0;
    for (i = 0; i < 32; i++) {
        all_mic1 += (s32)calib->mic1_band[i];
    }
    avg_ceil = (all_mic1 + 31) / 32; /* ceil */
    {
        s32 diff = 75 - (s32)avg_ceil;
        s32 detect_val = round_div(diff * 409600, 301);
        set_word(data, 1, (u32)(detect_val & 0xFFFFFF));
    }

    /* word 2: mic2_cal = 0x800000 * 10^(mic2_gain_diff_tenth_db / 200) */
    {
        s32 idx = (s32)calib->mic2_gain_diff + BS300_MIC2_CAL_TABLE_OFFSET;
        if (idx < 0) idx = 0;
        if (idx >= BS300_MIC2_CAL_TABLE_SIZE) idx = BS300_MIC2_CAL_TABLE_SIZE - 1;
        set_word(data, 2, bs300_mic2_cal_frac24_table[idx]);
    }

    /* word 3: suppression strength preset */
    ssp = (mod->wnr_preset < 5) ? wnr_preset_to_ssp[mod->wnr_preset] : 12;
    set_word(data, 3, (ssp >= 12) ? 0x000006 : 0x000003);

    /* words 4-6: fixed */
    set_word(data, 4, 0x001543);
    set_word(data, 5, 0x2aaaab);
    set_word(data, 6, 0x200000);
    return 0;
}

int bs300_encode_wnr_band_0_15(const bs300_modules_t *mod,
                                const bs300_calib_t *calib,
                                u8 input_type, u8 *data)
{
    s32 igd;
    u8 ssp;
    u8 i;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    igd = get_input_gain_diff_tenth_db(input_type, calib);
    ssp = 0;  /* chip uses SSP level 0 for band data offsets */

    /* band_N_data = 0x2A9764 - db_to_frac24((mic1 + igd/10) * 2 - offset)
     * n_tenth_db = mic1*20 + igd*2 - offset*10 (exact integer) */
    for (i = 0; i < 16; i++) {
        s32 offset = (s32)wnr_ssp_offset[i][ssp];
        s32 n_tenth = (s32)calib->mic1_band[i] * 20 + igd * 2 - offset * 10;
        u32 frac = db_to_frac24(n_tenth);
        u32 result = 0x2A9764 - frac;
        set_word(data, i, result & 0xFFFFFF);
    }
    return 0;
}

int bs300_encode_wnr_band_16_31(const bs300_modules_t *mod,
                                 const bs300_calib_t *calib,
                                 u8 input_type, u8 *data)
{
    s32 igd;
    u8 ssp;
    u8 i;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    igd = get_input_gain_diff_tenth_db(input_type, calib);
    ssp = 0;  /* chip uses SSP level 0 for band data offsets */

    for (i = 0; i < 16; i++) {
        u8 band = 16 + i;
        s32 offset = (s32)wnr_ssp_offset[band][ssp];
        s32 n_tenth = (s32)calib->mic1_band[band] * 20 + igd * 2 - offset * 10;
        u32 frac = db_to_frac24(n_tenth);
        u32 result = 0x2A9764 - frac;
        set_word(data, i, result & 0xFFFFFF);
    }
    return 0;
}

int bs300_encode_wnr_single_mic(const bs300_modules_t *mod,
                                 const bs300_calib_t *calib,
                                 u8 input_type, u8 *data)
{
    s32 igd;
    u8 ssp;
    u8 i;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    igd = get_input_gain_diff_tenth_db(input_type, calib);
    ssp = 0;  /* chip uses SSP level 0 for band data offsets */

    /* Single mic detection: bands 0-2, data2 formula:
     * data2 = 3292041 - db_to_frac24((mic1 + igd/10) * 2 - offset) */
    for (i = 0; i < 3; i++) {
        s32 offset = (s32)wnr_data2_offset[i][ssp];
        s32 n_tenth = (s32)calib->mic1_band[i] * 20 + igd * 2 - offset * 10;
        u32 frac = db_to_frac24(n_tenth);
        u32 result = 3292041 - frac;
        set_word(data, i, result & 0xFFFFFF);
    }
    return 0;
}

/* ================================================================
 *  AGCO Param Encoder
 *  Matches codegen.py encode_agco_param.
 * ================================================================ */

int bs300_encode_agco(const bs300_modules_t *mod, u8 *data)
{
    if (mod == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    set_word(data, 0, mod->agco_enable ? 1 : 0);

    if (!mod->agco_enable) return 0;

    /* Threshold: 0xFA0000 - ceil(|th_dB| * 65536 / 6.02)
     * db_to_frac24 expects tenth-dB: th_tenth_dB = |th_dB| * 10 */
    {
        s32 abs_th = (s32)mod->agco_threshold_db;
        if (abs_th < 0) abs_th = -abs_th;
        set_word(data, 1, (u32)(0xFA0000U - db_to_frac24(abs_th * 10)));
    }

    /* Attack: (1 - exp(-10/atk_01ms)) * 0x7FFFFF, lookup table */
    {
        u16 atk = mod->agco_attack_01ms;
        if (atk >= BS300_AGCO_EXP_TABLE_SIZE) atk = BS300_AGCO_EXP_TABLE_SIZE - 1;
        set_word(data, 2, bs300_agco_exp_table[atk]);
    }

    /* Release: same formula */
    {
        u16 rel = mod->agco_release_01ms;
        if (rel >= BS300_AGCO_EXP_TABLE_SIZE) rel = BS300_AGCO_EXP_TABLE_SIZE - 1;
        set_word(data, 3, bs300_agco_exp_table[rel]);
    }

    return 0;
}

/* ================================================================
 *  Input Source Param Encoders
 *  Matches codegen.py encode_mm_plus_param, encode_ddm2_param,
 *  encode_tc_dai_gain_diff_param.
 * ================================================================ */

int bs300_encode_mm_plus(const bs300_modules_t *mod,
                          const bs300_calib_t *calib,
                          u8 input_type, u8 *data)
{
    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    set_word(data, 0, mod->mm_plus_enable ? 1 : 0);

    if (!mod->mm_plus_enable) return 0;

    /* Data = 524288 * 10^((MixRatio - inputGainDiff_dB) / 20)
     * = 524288 * 10^(x/200) where x = mix_ratio*10 - igd_tenth_db.
     * Use lookup table: index = (mix_ratio * 10 - igd_tenth_db) + 500. */
    {
        s32 igd = get_input_gain_diff_tenth_db(input_type, calib);
        s32 x = (s32)mod->mix_ratio * 10 - igd;
        s32 idx = x + BS300_MM_PLUS_TABLE_OFFSET;
        if (idx < 0) idx = 0;
        if (idx >= BS300_MM_PLUS_TABLE_SIZE) idx = BS300_MM_PLUS_TABLE_SIZE - 1;
        set_word(data, 1, bs300_mm_plus_frac24_table[idx]);
    }
    return 0;
}

/* Omni threshold frac48 lookup: index = (avg_mic1 - omni_mt) diff */
#define BS300_OMNI_TABLE_SIZE  121
static const u32 bs300_omni_frac48_l[BS300_OMNI_TABLE_SIZE] = {
    0x000000,0x000000,0x000000,0x000000,0x000000,0x000000,0x000000,0x000000,0x000000,0x000000,
    0x000000,0x000000,0x000000,0xD683FF,0x248D6A,0x9E8268,0x1F3DC8,0xEDDAC5,0xC350DD,0x18C76E,
    0xFD6F87,0x3EBA05,0xCB3030,0xD06CF4,0xE1147C,0x036CA6,0x5957C8,0x7BD2B6,0xF08143,0x89E688,
    0x392EE6,0xFFEB5F,0x66290D,0x354077,0x4C1843,0x3D1D77,0x1492B9,0x30595C,0x805B2C,0xDE1F0F,
    0x6F7BF4,0x3F8048,0x646F59,0x2C7F4D,0xE6A3FC,0xF2A573,0xD52FD7,0x1A634B,0xDC6A3D,0xCC5920,
    0xA28B0E,0xE1320A,0xD83265,0xDCDB26,0xAAD07C,0xE5AF11,0xB4AD1C,0x72E0B1,0x6FEC1D,0xBDB0D2,
    0x085B09,0x76A541,0x90A2DC,0x2BBA26,0x5ABCB9,0x614558,0xA9AF0F,0xBD1CAD,0x3D23FE,0xDEC65F,
    0x667224,0xA4D658,0x745D89,0xB72D4D,0x558F32,0x3CAD69,0x5D91EE,0xAC5A89,0x1F96C3,0xAFC731,
    0x56F733,0x106BC7,0xD86300,0xABE0CD,0x888646,0x6C7159,0x56231D,0x446B6B,0x3658AC,0x2B2AF9,
    0x2249E6,0x1B3C5F,0x15A239,0x112F11,0x0DA639,0x0AD77F,0x089C9A,0x06D723,0x056EF2,0x0450D7,
    0x036D96,0x02B913,0x0229B1,0x01B7CE,0x015D57,0x01157C,0x00DC68,0x00AF12,0x008B10,0x006E75,
    0x0057BD,0x0045B1,0x00375B,0x002BF8,0x0022ED,0x001BBD,0x001609,0x001180,0x000DE7,0x000B0B,
    0x0008C5,
};

int bs300_encode_ddm2(const bs300_modules_t *mod,
                       const bs300_calib_t *calib, u8 *data)
{
    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    set_word(data, 0, mod->ddm2_enable ? 1 : 0);
    if (!mod->ddm2_enable) return 0;

    /* polar_pattern uint3 → frac24 */
    {
        static const u32 polar_frac24[8] = {
            0x000000,  /* 0: Bi-directional    */
            0x200000,  /* 1: Hyper-cardioid    */
            0x300000,  /* 2: Super-cardioid    */
            0x400000,  /* 3: Cardioid           */
            0x7FFFFF,  /* 4: Omni-directional  */
            0x000000,  /* 5: (unused)          */
            0x000000,  /* 6: (unused)          */
            0x000000,  /* 7: (unused)          */
        };
        u8 idx = mod->polar_pattern & 0x07;
        u32 pval = polar_frac24[idx];
        if (mod->adm_fdm) { pval = 0x7FFFFF; }  /* ADM → Omni */
        set_word(data, 2, pval);
    }

    set_word(data, 1, mod->open_ear);
    set_word(data, 3, mod->adm_fdm);

    /* mic2_dly_data = mic_delay_raw * 8388607 / 1250 (truncation toward zero)
     * 0.008 * 0.1 * 0x7FFFFF = 8388607/1250 ≈ 6710.8856 */
    {
        s64 num = (s64)calib->mic_delay * 8388607ULL;
        s32 dly_val = (s32)(num / 1250ULL);
        set_word(data, 4, (u32)(dly_val & 0xFFFFFF));
    }

    /* mic2_cal_data = 0.5 * 0x7FFFFF * 10^(mic2_gain_diff_tenth_db / 200)
     * = 4194303 * 10^(x/200). Use mic2_cal table / 2. */
    {
        s32 idx = (s32)calib->mic2_gain_diff + BS300_MIC2_CAL_TABLE_OFFSET;
        if (idx < 0) idx = 0;
        if (idx >= BS300_MIC2_CAL_TABLE_SIZE) idx = BS300_MIC2_CAL_TABLE_SIZE - 1;
        set_word(data, 5, (bs300_mic2_cal_frac24_table[idx] / 2) & 0xFFFFFF);
    }

    /* Omni threshold (frac48): table indexed by diff = avg_mic1 - omni_mt
     * omni_mt = (omni_threshold_I2C - 2) / 4 + 40, avg_mic1 = sum(mic1_band[1:31])/31 */
    if (mod->omni_threshold >= 40 && mod->omni_threshold <= 100) {
        s32 sum_mic1 = 0;
        s32 diff_tenth;      /* (avg_mic1 - omni_mt) * 10              */
        s32 idx;
        s32 omni_mt;
        for (u8 i = 1; i < 32; i++) {
            sum_mic1 += (s32)calib->mic1_band[i];
        }
        diff_tenth = (sum_mic1 * 10) / 31;        /* avg_mic1 × 10     */
        omni_mt    = ((s32)mod->omni_threshold - 2) / 4 * 10 + 400;
        diff_tenth -= omni_mt;
        idx = (diff_tenth + 5) / 10;               /* round to nearest  */
        if (idx >= 0 && idx < BS300_OMNI_TABLE_SIZE) {
            set_word(data, 6, 0);                  /* W6=0 for all practical diff */
            set_word(data, 7, bs300_omni_frac48_l[idx]);
        }
    }

    /* Fixed coefficients */
    set_word(data, 8, 0x7F8000);
    set_word(data, 9, 0x7801FE);
    set_word(data, 11, 0x0079B9);
    set_word(data, 12, 0x0079B9);
    return 0;
}

int bs300_encode_tc_dai(const bs300_calib_t *calib,
                         u8 input_type, u8 *data)
{
    s32 gain_raw;

    if (calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    if (input_type == INPUT_TYPE_TELECOIL) {
        gain_raw = (s32)calib->telecoil_gain_diff;
    } else if (input_type == INPUT_TYPE_DAI) {
        gain_raw = (s32)calib->dai_gain_diff;
    } else {
        return 0;
    }

    /* data = (gain_dB * 2) * (65536 / 6.02)
     * gain_dB = gain_raw / 10.0 → data = gain_raw * 655360 / 301 */
    {
        s32 val = gain_raw * 655360 / 301;
        set_word(data, 0, (u32)(val & 0xFFFFFF));
    }
    return 0;
}

/* ================================================================
 *  Input Tone Generator (0x8001E2)
 *  itg_level reuses bs300_beep_frac24_table (same formula).
 * ================================================================ */

int bs300_encode_itg(u8 level_db, u16 freq_hz, u8 enable,
                     const bs300_calib_t *calib, u8 *data)
{
    u8 cal_band;
    u32 freq_idx;
    s32 x;
    s32 idx;

    if (calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    if (!enable) return 0;

    freq_idx = freq_hz / 250;
    if (freq_idx < 1) freq_idx = 1;
    if (freq_idx > 0x1F) freq_idx = 0x1F;
    set_word(data, 1, freq_idx);

    cal_band = freq_to_cal_band(freq_hz);
    x = (s32)level_db - (s32)calib->mic1_band[cal_band];
    idx = x + BS300_BEEP_TABLE_OFFSET;
    if (idx < 0) idx = 0;
    if (idx >= BS300_BEEP_TABLE_SIZE) idx = BS300_BEEP_TABLE_SIZE - 1;
    set_word(data, 0, bs300_beep_frac24_table[idx]);

    set_word(data, 2, 0x000001);

    return 0;
}

int bs300_itg_write(soft_iic_dev iic, u8 level_db, u16 freq_hz,
                    const bs300_calib_t *calib)
{
    u8 data[48];
    int ret;

    if (calib == NULL) return -1;

    ret = bs300_encode_itg(level_db, freq_hz, 1, calib, data);
    if (ret < 0) return ret;

    ret = bs300_param_write_packet(iic, 0x8001E2, data);
    if (ret < 0) {
        bs300_debug("bs300: itg_write fail ret=%d\n", ret);
    }
    return ret;
}

int bs300_itg_clear(soft_iic_dev iic)
{
    u8 data[48];

    memset(data, 0, 48);
    return bs300_param_write_packet(iic, 0x8001E2, data);
}

/* ================================================================
 *  Pure-tone Audiometry
 *  Enter:  Mute → disable all non-WDRC → write WDRC test params → Active
 *  Exit:   Mute → ITG Clear → full resync from VM → Active
 * ================================================================ */

int bs300_audiometry_enter(soft_iic_dev iic)
{
    u8 data[48];
    int ret;
    u8 i;

    ret = bs300_mute(iic);
    if (ret < 0) return ret;

    /* Disable ENR (0x8000C2): first byte = 0 disables all ENR processing */
    memset(data, 0, 48);
    data[0] = 0x00;
    ret = bs300_param_write_packet(iic, 0x8000C2, data);
    if (ret < 0) return ret;

    /* Disable DDM2 (0x800022): word0=0 → ddm2_enable=0 */
    memset(data, 0, 48);
    ret = bs300_param_write_packet(iic, 0x800022, data);
    if (ret < 0) return ret;

    /* Disable MM+ (0x800062): word0=0 → mm_plus_enable=0 */
    memset(data, 0, 48);
    ret = bs300_param_write_packet(iic, 0x800062, data);
    if (ret < 0) return ret;

    /* Disable DFBC (0x800052): word0=0 → dfbc_enable_mode bit7=0 */
    memset(data, 0, 48);
    ret = bs300_param_write_packet(iic, 0x800052, data);
    if (ret < 0) return ret;

    /* Disable NoiseGen2 (0x800172): all zeros */
    memset(data, 0, 48);
    ret = bs300_param_write_packet(iic, 0x800172, data);
    if (ret < 0) return ret;

    /* Disable ISS (0x8001B2): word0=0 → iss_enable=0 */
    memset(data, 0, 48);
    ret = bs300_param_write_packet(iic, 0x8001B2, data);
    if (ret < 0) return ret;

    /* Disable WNR (0x8001C2): word0=0 → wnr_enable_dual bit0=0 */
    memset(data, 0, 48);
    ret = bs300_param_write_packet(iic, 0x8001C2, data);
    if (ret < 0) return ret;

    /* Disable AGCO (0x800382): word0=0 → agco_enable=0 */
    memset(data, 0, 48);
    ret = bs300_param_write_packet(iic, 0x800382, data);
    if (ret < 0) return ret;

    /* ================================================================
     *  WDRC test params (from b300_TESTWDRC_SET reference)
     * ================================================================ */

    /* WDRC General (0x8000B2): enable=1, ch config */
    memset(data, 0, 48);
    data[0]  = 0x01;
    data[3]  = 0x10;
    data[9]  = 0x10;
    data[12] = 0x03;
    ret = bs300_param_write_packet(iic, 0x8000B2, data);
    if (ret < 0) return ret;

    /* WDRC Freq Spacing (0x8010B2): 16 channels × {0x41,0x10,0x04} */
    memset(data, 0, 48);
    for (i = 0; i < 16; i++) {
        data[i * 3 + 0] = 0x41;
        data[i * 3 + 1] = 0x10;
        data[i * 3 + 2] = 0x04;
    }
    ret = bs300_param_write_packet(iic, 0x8010B2, data);
    if (ret < 0) return ret;

    /* WDRC KP Threshold (0x8020B2): 32 per-band thresholds */
    memset(data, 0, 48);
    {
        static const u8 kp_th[32] = {
            0xA5,0xA5,0xA6,0xA6,0xA6,0xA6,0xA5,0xA5,
            0xA5,0xA5,0xA2,0xA2,0xA1,0xA1,0xA5,0xA5,
            0xA4,0xA4,0xA6,0xA6,0xA4,0xA4,0xA4,0xA4,
            0xA5,0xA5,0xA3,0xA3,0xA3,0xA3,0xA3,0xA3,
        };
        for (i = 0; i < 32; i++) data[i] = kp_th[i];
    }
    ret = bs300_param_write_packet(iic, 0x8020B2, data);
    if (ret < 0) return ret;

    /* WDRC Ratio (0x8050B2): all 0x20, last byte = 20 */
    memset(data, 0x20, 47);
    data[47] = 20;
    ret = bs300_param_write_packet(iic, 0x8050B2, data);
    if (ret < 0) return ret;

    /* WDRC Bin Gain (0x8060B2): 32 per-band gain values */
    memset(data, 0, 48);
    {
        static const u8 bin_gain[32] = {
            0xFB,0x09,0x09,0x09,0x09,0x0D,0xFF,0x0A,
            0x08,0x0B,0xFE,0x0F,0x0B,0x0A,0xF9,0x08,
            0x0E,0x0D,0x04,0x09,0x0B,0x0A,0x0A,0x15,
            0x0F,0x15,0x0F,0x0F,0x0F,0x0F,0x0F,0x0F,
        };
        for (i = 0; i < 32; i++) data[i] = bin_gain[i];
    }
    ret = bs300_param_write_packet(iic, 0x8060B2, data);
    if (ret < 0) return ret;

    ret = bs300_active(iic);
    if (ret < 0) return ret;

    soft_iic_set_delay(iic, 500);
    return 0;
}

int bs300_audiometry_exit(soft_iic_dev iic)
{
    u8 prog_idx;
    bs300_prog_struct_t prog;
    int ret;

    ret = bs300_mute(iic);
    if (ret < 0) return ret;

    bs300_itg_clear(iic);

    ret = bs300_vm_read_active_prog(&prog_idx);
    if (ret < 0) return ret;

    ret = bs300_vm_load_struct(prog_idx, &prog);
    if (ret < 0) return ret;

    ret = bs300_sync_program(iic, &prog);
    if (ret < 0) return ret;

    ret = bs300_active(iic);
    if (ret < 0) return ret;

    soft_iic_set_delay(iic, 500);
    return 0;
}

/* ================================================================
 *  Main sync entry point
 * ================================================================ */

static int bs300_sync_program_inner(soft_iic_dev iic, bs300_prog_struct_t *prog,
                                     bs300_sync_session_t *session);

int bs300_sync_program(soft_iic_dev iic, bs300_prog_struct_t *prog)
{
    return bs300_sync_program_inner(iic, prog, NULL);
}

int bs300_sync_program_start(bs300_sync_session_t *s, bs300_prog_struct_t *prog)
{
    if (s == NULL || prog == NULL) return -1;
    return bs300_sync_program_inner(s->iic, prog, s);
}

/* ================================================================
 *  Hardcoded I2C trace sync (original SEND_FIXED block)
 * ================================================================ */
static int bs300_sync_program_hardcoded(soft_iic_dev iic,
                                        bs300_prog_struct_t *prog,
                                        const bs300_calib_t *calib,
                                        u8 input_type, u8 ch_cnt,
                                        bs300_sync_session_t *session)
{
    u8 data[48];
    int sent = 0;
    int fail = 0;
    int ret;

    /* All commands hardcoded from I2C trace */
#define SEND_FIXED(cmd, ...) do { \
    static const u8 _d[48] = { __VA_ARGS__ }; \
    memcpy(data, _d, 48); \
    if (session) { bs300_session_append(session, cmd, data); } \
    else { ret = bs300_param_write_packet(iic, cmd, data); \
           if (ret == 0) sent++; else fail++; } \
} while(0)

    SEND_CMD(0x800022, bs300_encode_ddm2(&prog->modules, &calib, data)); /* DDM2 */
    SEND_FIXED(0x800062);                                            /* MM+ */
    SEND_FIXED(0x800052, 0x0F,0x00,0x00,0x01,0x00,0x00);            /* DFBC */

    /* === ENR === */
    if (prog->enr.enable_num_ch & 0x80) {
        SEND_FIXED(0x8000C2, 0x01,0x00,0x00,0x10,0x00,0x00,0x02,0x00,0x00,0x0E,0x00,0x00);
        SEND_FIXED(0x8010C2, 0x82,0x10,0x04,0x82,0x20,0x08,0x82,0x20,0x08,0x84,0x20,0x08);
        SEND_FIXED(0x8020C2, 0x35,0x50,0x03,0x35,0x50,0x03,0x35,0x50,0x03,0x35,0x50,0x03,0x35,0x50,0x03,0x35,0x50,0x03,0x35,0x50,0x03,0x35,0x50,0x03);
        SEND_FIXED(0x8030C2, 0xE6,0x60,0x0E,0xE6,0x60,0x0E,0xE6,0x60,0x0E,0xE6,0x60,0x0E,0xE6,0x60,0x0E,0xE6,0x60,0x0E,0xE6,0x60,0x0E,0xE6,0x60,0x0E);
        SEND_FIXED(0x8040C2, 0x43,0x3E,0xE4,0x5D,0x8E,0xE5,0x52,0xDE,0xE4,0x58,0x8E,0xE4,0x4D,0x8E,0xE4,0x48,0x2E,0xE5,0x82,0x8E,0xE6,0x82,0x2E,0xE8);
        SEND_FIXED(0x8050C2, 0x81,0x1F,0xF8,0x9B,0x6F,0xF9,0x91,0xCF,0xF8,0x96,0x6F,0xF8,0x8C,0x6F,0xF8,0x86,0x1F,0xF9,0xC1,0x6F,0xFA,0xC1,0x1F,0xFC);
        SEND_FIXED(0x8060C2, 0x00,0x00,0x20,0x00,0x00,0x60,0x00,0x00,0x10,0x00,0x00,0x70,0x00,0x00,0x02,0x00,0x00,0x7E,0x00,0x04,0x00,0x00,0xFC,0x7F,0x00,0x00,0x02,0x00,0x00,0x7E,0x00,0x40,0x00,0x00,0xC0,0x7F,0x00,0x40,0x00,0x00,0x00,0x08,0x00,0x00,0x78,0x00,0x00,0x00);
        SEND_FIXED(0x8070C2, 0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE,0x75,0xDA,0xFE);
        SEND_FIXED(0x8080C2, 0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00,0x7B,0xCD,0x00);
    } else {
        SEND_FIXED(0x8000C2, 0x00);
    }

    SEND_FIXED(0x800172);                                            /* NoiseGen2 */
    SEND_FIXED(0x804272);                                            /* TC/DAI */
    SEND_FIXED(0x8001B2, 0x01,0x00,0x00,0x78,0x3C,0x12,0x15);      /* ISS */

    /* === WNR === */
    prog->modules.wnr_enable_dual |= 0x01;
    SEND_FIXED(0x8001C2, 0x01,0x00,0x00,0xA5,0x76,0xFE,0x4C,0x9E,0x8F,0x03,0x00,0x00,0x43,0x15,0x00,0xAB,0xAA,0x2A,0x00,0x00,0x20,0x00);
    SEND_FIXED(0x8011C2, 0xB7,0x6D,0xF7,0xB7,0x6D,0xF7,0xF7,0x16,0xF9,0xB8,0xC8,0xF0,0xC4,0x1D,0xF1,0xC4,0x1D,0xF1,0xB8,0xC8,0xF0,0x91,0xC9,0xEF,0xC4,0x1D,0xF1,0x91,0xC9,0xEF,0xB8,0xC8,0xF0,0x78,0x1F,0xEF,0xD1,0x72,0xF1,0xAB,0x73,0xF0,0x91,0xC9,0xEF,0xB8,0xC8,0xF0);
    SEND_FIXED(0x8411C2, 0xC4,0x1D,0xF1,0xC4,0x1D,0xF1,0xDE,0xC7,0xF1,0xD1,0x72,0xF1,0x91,0xC9,0xEF,0xDE,0xC7,0xF1,0xDE,0xC7,0xF1,0x2B,0xC6,0xF3,0x44,0x70,0xF4,0x44,0x70,0xF4,0x44,0x70,0xF4,0x44,0x70,0xF4,0x44,0x70,0xF4,0x44,0x70,0xF4,0x44,0x70,0xF4,0x44,0x70,0xF4);
    SEND_FIXED(0x8021C2, 0x91,0x6E,0xF6,0x04,0x6C,0xF9,0x1D,0x16,0xFA);

    SEND_FIXED(0x800382, 0x01,0x00,0x00,0x54,0xD6,0xF8,0xFA,0x32,0x2A,0xFD,0x3B,0x0A);  /* AGCO */
    SEND_FIXED(0x800081, 0x3C,0x09,0x00,0x04,0x00,0x00,0xE1,0xD8,0xFB,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x3C,0x09,0x00);  /* Vol/Beep */

    /* === WDRC === */
    SEND_FIXED(0x8000B2, 0x01,0x00,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x00,0x03);
    SEND_FIXED(0x8010B2, 0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04,0x41,0x10,0x04);
    SEND_FIXED(0x8020B2, 0xD4,0xED,0xD8,0xF1,0xD9,0xF2,0xD6,0xEF,0xD7,0xF0,0xD5,0xEE,0xD8,0xF1,0xD5,0xEE,0xD6,0xEF,0xD7,0xF0,0xD5,0xEE,0xDB,0xF4,0xE0,0xF9,0xE0,0xF9,0xE0,0xF9,0xE0,0xF9);
    SEND_FIXED(0x8030B2, 0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F,0x23,0x0F,0x0F);
    SEND_FIXED(0x8040B2, 0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D,0x39,0x2D,0x2D);
    SEND_FIXED(0x8050B2, 0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29,0x10,0x31,0x29);
    SEND_FIXED(0x8060B2, 0x06,0x10,0x12,0x1F,0x21,0x29,0x25,0x2C,0x25,0x2C,0x26,0x23,0x1D,0x18,0x18,0x1C,0x1C,0x20,0x1C,0x20,0x21,0x1F,0x1D,0x17,0x12,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C);
    SEND_FIXED(0x8070B2, 0x0C,0x14,0x1A,0x18,0x19,0x14,0x0C,0x0B,0x13,0x17,0x1A,0x1F,0x1F,0x1A,0x15,0x01);
    SEND_FIXED(0x8080B2, 0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05);
    SEND_FIXED(0x8090B2, 0x23,0x23,0x23,0x23,0x23,0x23,0x23,0x23,0x23,0x23,0x23,0x23,0x23,0x23,0x23,0x23);
    SEND_FIXED(0x80A0B2, 0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F,0x7F);

#undef SEND_FIXED

    bs300_debug("bs300: sync_hardcoded done, sent=%d fail=%d\n", sent, fail);
    return (fail > 0) ? -1 : 0;
}

/* ================================================================
 *  Dynamic encode sync (uses C encode functions from codegen)
 * ================================================================ */
static int bs300_sync_program_dynamic(soft_iic_dev iic,
                                       bs300_prog_struct_t *prog,
                                       const bs300_calib_t *calib,
                                       u8 input_type, u8 ch_cnt,
                                       bs300_sync_session_t *session)
{
    u8 data[48];
    int sent = 0;
    int fail = 0;
    int ret;

#define SEND_CMD(cmd, fn) do { \
    memset(data, 0, 48); \
    ret = fn; \
    if (ret == 0) { \
        if (session) { \
            bs300_session_append(session, cmd, data); \
        } else { \
            ret = bs300_param_write_packet(iic, cmd, data); \
            if (ret == 0) { sent++; } else { fail++; \
                bs300_debug("bs300: sync FAIL cmd=0x%06X ret=%d\n", cmd, ret); \
            } \
        } \
    } \
} while(0)

    prog->modules.wnr_enable_dual |= 0x01;

    /* Input source */
    SEND_CMD(0x800022, bs300_encode_ddm2(&prog->modules, calib, data));
    SEND_CMD(0x800062, bs300_encode_mm_plus(&prog->modules, calib, input_type, data));
    SEND_CMD(0x800052, bs300_encode_dfbc(&prog->modules, calib, data));

    /* === ENR === */
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
        data[0] = 0x00;
        if (session) {
            bs300_session_append(session, 0x8000C2, data);
        } else {
            ret = bs300_param_write_packet(iic, 0x8000C2, data);
            if (ret == 0) sent++; else fail++;
        }
    }

    /* NoiseGen2 has no encode function - keep as fixed */
    memset(data, 0, 48);
    if (session) {
        bs300_session_append(session, 0x800172, data);
    } else {
        ret = bs300_param_write_packet(iic, 0x800172, data);
        if (ret == 0) sent++; else fail++;
    }
    SEND_CMD(0x804272, bs300_encode_tc_dai(calib, input_type, data));
    SEND_CMD(0x8001B2, bs300_encode_iss(&prog->modules, calib, input_type, data));

    /* === WNR === */
    SEND_CMD(0x8001C2, bs300_encode_wnr_setup(&prog->modules, calib, data));
    SEND_CMD(0x8011C2, bs300_encode_wnr_band_0_15(&prog->modules, calib, input_type, data));
    SEND_CMD(0x8411C2, bs300_encode_wnr_band_16_31(&prog->modules, calib, input_type, data));
    SEND_CMD(0x8021C2, bs300_encode_wnr_single_mic(&prog->modules, calib, input_type, data));

    SEND_CMD(0x800382, bs300_encode_agco(&prog->modules, data));
    SEND_CMD(0x800081, bs300_encode_volume_beep(&prog->modules, calib, data));

    /* === WDRC === */
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

    bs300_debug("bs300: sync_dynamic done, sent=%d fail=%d\n", sent, fail);
    return (fail > 0) ? -1 : 0;
}

/* ================================================================
 *  Inner sync dispatcher
 * ================================================================ */
static int bs300_sync_program_inner(soft_iic_dev iic, bs300_prog_struct_t *prog,
                                     bs300_sync_session_t *session)
{
    u8 calib_raw[144];
    bs300_calib_t calib;
    u8 input_type;
    u8 ch_cnt;
    int ret;

    if (prog == NULL) return -1;

    /* Load calibration from VM */
    ret = bs300_calib_vm_load(calib_raw);
    if (ret < 0) {
        bs300_debug("bs300: sync calib_vm_load fail, skip sync\n");
        return -1;
    }
    ret = bs300_parse_calibration(calib_raw, &calib);
    if (ret < 0) {
        bs300_debug("bs300: sync calib parse fail\n");
        return -1;
    }

    input_type = get_input_type(prog->modules.input_selection);
    ch_cnt = prog->wdrc.total_channels;

    {
        u8 ap;
        if (bs300_vm_read_active_prog(&ap) >= 0) {
            printf("[BS300] sync RAM prog=%d\n", ap);
        }
    }

    bs300_debug("bs300: sync_program start, ch=%d kp_mode=%d lim=%d "
                "enr_en=0x%02x input=%d input_type=%d dynamic=%d\n",
                ch_cnt, prog->wdrc.kp_mode, prog->wdrc.limiter,
                prog->enr.enable_num_ch, prog->modules.input_selection,
                input_type, BS300_SYNC_USE_DYNAMIC);

#if BS300_SYNC_USE_DYNAMIC
    return bs300_sync_program_dynamic(iic, prog, &calib, input_type,
                                       ch_cnt, session);
#else
    return bs300_sync_program_hardcoded(iic, prog, &calib, input_type,
                                         ch_cnt, session);
#endif
}

/* ================================================================
 *  Program switch with incremental sync
 * ================================================================ */

/* ================================================================
 *  Per-module incremental diff helpers (shared by switch_program
 *  and param_modify). Each helper compares two struct snapshots
 *  and sends only the commands whose inputs have changed.
 * ================================================================ */

static void switch_diff_wdrc(soft_iic_dev iic,
                              const bs300_wdrc_t *ow, const bs300_wdrc_t *nw,
                              const bs300_modules_t *om, const bs300_modules_t *nm,
                              const bs300_calib_t *calib,
                              u8 new_it, int igd_changed,
                              bs300_sync_session_t *session,
                              int *sent, int *fail, u8 *data)
{
    int ret;
#define SEND_IF_DIRTY(fn_call, cmd) do { \
    memset(data, 0, 48); \
    ret = (fn_call); \
    if (ret == 0) { \
        if (session) { \
            bs300_session_append(session, cmd, data); \
        } else { \
            ret = bs300_param_write_packet(iic, cmd, data); \
            if (ret == 0) { (*sent)++; } else { (*fail)++; \
                bs300_debug("bs300: switch FAIL cmd=0x%06X ret=%d\n", cmd, ret); \
            } \
        } \
    } \
} while(0)

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

    if (hdr_changed) {
        SEND_IF_DIRTY(bs300_encode_wdrc_general(nw, data), 0x8000B2);
    }
    if (hdr_changed || freq_changed) {
        SEND_IF_DIRTY(bs300_encode_wdrc_freq_spacing(nw, data), 0x8010B2);
    }
    if (hdr_changed || freq_changed || kp1_th_changed
        || kp2_th_changed || igd_changed) {
        SEND_IF_DIRTY(bs300_encode_wdrc_kp_threshold(nw, calib,
                                                       new_it, data),
                      0x8020B2);
    }
    if (hdr_changed || at_changed) {
        SEND_IF_DIRTY(bs300_encode_wdrc_attack_time(nw, data), 0x8030B2);
    }
    if (hdr_changed || rt_changed) {
        SEND_IF_DIRTY(bs300_encode_wdrc_release_time(nw, data), 0x8040B2);
    }
    if (hdr_changed || ratio_changed) {
        SEND_IF_DIRTY(bs300_encode_wdrc_ratio(nw, data), 0x8050B2);
    }
    if (bg_changed || vol_eq_changed || igd_changed) {
        SEND_IF_DIRTY(bs300_encode_wdrc_bin_gain(nw, calib, nm,
                                                   new_it, data),
                      0x8060B2);
    }
    if (freq_changed || lmt_th_changed) {
        SEND_IF_DIRTY(bs300_encode_wdrc_lmt_threshold(nw, calib, data),
                      0x8070B2);
    }

    if (ow->limiter == 0 && nw->limiter == 0) {
        /* both disabled, skip */
    } else if (ow->limiter == 0 && nw->limiter == 1) {
        SEND_IF_DIRTY(bs300_encode_wdrc_lmt_threshold(nw, calib, data),
                      0x8070B2);
        SEND_IF_DIRTY(bs300_encode_wdrc_lmt_attack(nw, data), 0x8080B2);
        SEND_IF_DIRTY(bs300_encode_wdrc_lmt_release(nw, data), 0x8090B2);
        SEND_IF_DIRTY(bs300_encode_wdrc_lmt_ratio(nw, data), 0x80A0B2);
    } else if (ow->limiter == 1 && nw->limiter == 0) {
        memset(data, 0, 48);
        if (session) {
            bs300_session_append(session, 0x8070B2, data);
            bs300_session_append(session, 0x8080B2, data);
            bs300_session_append(session, 0x8090B2, data);
            bs300_session_append(session, 0x80A0B2, data);
        } else {
            ret = bs300_param_write_packet(iic, 0x8070B2, data);
            if (ret == 0) (*sent)++; else (*fail)++;
            ret = bs300_param_write_packet(iic, 0x8080B2, data);
            if (ret == 0) (*sent)++; else (*fail)++;
            ret = bs300_param_write_packet(iic, 0x8090B2, data);
            if (ret == 0) (*sent)++; else (*fail)++;
            ret = bs300_param_write_packet(iic, 0x80A0B2, data);
            if (ret == 0) (*sent)++; else (*fail)++;
        }
    } else {
        if (freq_changed || lmt_th_changed) {
            SEND_IF_DIRTY(bs300_encode_wdrc_lmt_threshold(nw, calib, data),
                          0x8070B2);
        }
        if (lmt_at_changed) {
            SEND_IF_DIRTY(bs300_encode_wdrc_lmt_attack(nw, data), 0x8080B2);
        }
        if (lmt_rt_changed) {
            SEND_IF_DIRTY(bs300_encode_wdrc_lmt_release(nw, data), 0x8090B2);
        }
        if (lmt_r_changed) {
            SEND_IF_DIRTY(bs300_encode_wdrc_lmt_ratio(nw, data), 0x80A0B2);
        }
    }
#undef SEND_IF_DIRTY
}

static void switch_diff_vol_beep(soft_iic_dev iic,
                                  const bs300_modules_t *om, const bs300_modules_t *nm,
                                  const bs300_calib_t *calib,
                                  int igd_changed,
                                  bs300_sync_session_t *session,
                                  int *sent, int *fail, u8 *data)
{
    int ret;
#define SEND_IF_DIRTY(fn_call, cmd) do { \
    memset(data, 0, 48); \
    ret = (fn_call); \
    if (ret == 0) { \
        if (session) { \
            bs300_session_append(session, cmd, data); \
        } else { \
            ret = bs300_param_write_packet(iic, cmd, data); \
            if (ret == 0) { (*sent)++; } else { (*fail)++; \
                bs300_debug("bs300: switch FAIL cmd=0x%06X ret=%d\n", cmd, ret); \
            } \
        } \
    } \
} while(0)

    if (om->vol_enable == 0 && nm->vol_enable == 0) {
    } else if (om->vol_enable == 0 && nm->vol_enable == 1) {
        SEND_IF_DIRTY(bs300_encode_volume_beep(nm, calib, data), 0x800081);
    } else if (om->vol_enable == 1 && nm->vol_enable == 0) {
        memset(data, 0, 48);
        set_word(data, 0, 0);
        if (session) {
            bs300_session_append(session, 0x800081, data);
        } else {
            ret = bs300_param_write_packet(iic, 0x800081, data);
            if (ret == 0) (*sent)++; else (*fail)++;
        }
    } else {
        int vol_changed = (om->beep_level != nm->beep_level)
                       || (om->beep_freq_idx != nm->beep_freq_idx)
                       || (om->min_vol != nm->min_vol)
                       || (om->max_vol != nm->max_vol)
                       || (om->input_selection != nm->input_selection)
                       || (om->batt_beep_level != nm->batt_beep_level)
                       || (om->batt_beep_freq_idx != nm->batt_beep_freq_idx);
        if (vol_changed || igd_changed) {
            SEND_IF_DIRTY(bs300_encode_volume_beep(nm, calib, data), 0x800081);
        }
    }
#undef SEND_IF_DIRTY
}

static void switch_diff_enr(soft_iic_dev iic,
                             const bs300_enr_t *oe, const bs300_enr_t *ne,
                             const bs300_calib_t *calib,
                             u8 new_it, int igd_changed,
                             bs300_sync_session_t *session,
                             int *sent, int *fail, u8 *data)
{
    int ret;
#define SEND_IF_DIRTY(fn_call, cmd) do { \
    memset(data, 0, 48); \
    ret = (fn_call); \
    if (ret == 0) { \
        if (session) { \
            bs300_session_append(session, cmd, data); \
        } else { \
            ret = bs300_param_write_packet(iic, cmd, data); \
            if (ret == 0) { (*sent)++; } else { (*fail)++; \
                bs300_debug("bs300: switch FAIL cmd=0x%06X ret=%d\n", cmd, ret); \
            } \
        } \
    } \
} while(0)

    u8 oe_ena = (oe->enable_num_ch & 0x80) ? 1 : 0;
    u8 ne_ena = (ne->enable_num_ch & 0x80) ? 1 : 0;

    if (oe_ena == 0 && ne_ena == 0) {
    } else if (oe_ena == 0 && ne_ena == 1) {
        SEND_IF_DIRTY(bs300_encode_enr_general(ne, data), 0x8000C2);
        SEND_IF_DIRTY(bs300_encode_enr_freq_spacing(ne, data), 0x8010C2);
        SEND_IF_DIRTY(bs300_encode_enr_snr_threshold(ne, data), 0x8020C2);
        SEND_IF_DIRTY(bs300_encode_enr_max_att(ne, data), 0x8030C2);
        SEND_IF_DIRTY(bs300_encode_enr_noise_th(ne, calib, new_it, data),
                      0x8040C2);
        SEND_IF_DIRTY(bs300_encode_enr_upper_noise_th(ne, calib, new_it, data),
                      0x8050C2);
        SEND_IF_DIRTY(bs300_encode_enr_smoothing(ne, data), 0x8060C2);
        SEND_IF_DIRTY(bs300_encode_enr_etr(ne, data), 0x8070C2);
        SEND_IF_DIRTY(bs300_encode_enr_nrr(ne, data), 0x8080C2);
    } else if (oe_ena == 1 && ne_ena == 0) {
        memset(data, 0, 48);
        set_word(data, 0, 0);
        if (session) {
            bs300_session_append(session, 0x8000C2, data);
        } else {
            ret = bs300_param_write_packet(iic, 0x8000C2, data);
            if (ret == 0) (*sent)++; else (*fail)++;
        }
    } else {
        int enr_hdr_changed = (oe->enable_num_ch != ne->enable_num_ch);
        int enr_freq_changed = (memcmp(oe->freq_idx, ne->freq_idx, 16) != 0);
        int snr_changed = (memcmp(oe->snr_th_db, ne->snr_th_db, 16) != 0);
        int ma_changed  = (memcmp(oe->max_att_db, ne->max_att_db, 16) != 0);
        int nt_changed  = (memcmp(oe->noise_th_db, ne->noise_th_db, 16) != 0);
        int unt_changed = (memcmp(oe->upper_noise_th_db,
                                  ne->upper_noise_th_db, 16) != 0);
        int sf_changed  = (oe->nfsf != ne->nfsf)
                       || (oe->nhsf != ne->nhsf)
                       || (oe->nnsf != ne->nnsf)
                       || (oe->snasf != ne->snasf);
        int etr_changed = (memcmp(oe->etr_x100, ne->etr_x100, 16) != 0);
        int nrr_changed = (memcmp(oe->nrr_x10, ne->nrr_x10, 16) != 0);
        int sasf_changed = (memcmp(oe->sasf, ne->sasf, 16) != 0);

        if (enr_hdr_changed) {
            SEND_IF_DIRTY(bs300_encode_enr_general(ne, data), 0x8000C2);
        }
        if (enr_hdr_changed || enr_freq_changed) {
            SEND_IF_DIRTY(bs300_encode_enr_freq_spacing(ne, data), 0x8010C2);
        }
        if (snr_changed) {
            SEND_IF_DIRTY(bs300_encode_enr_snr_threshold(ne, data), 0x8020C2);
        }
        if (snr_changed || ma_changed) {
            SEND_IF_DIRTY(bs300_encode_enr_max_att(ne, data), 0x8030C2);
        }
        if (enr_freq_changed || nt_changed || igd_changed) {
            SEND_IF_DIRTY(bs300_encode_enr_noise_th(ne, calib, new_it, data),
                          0x8040C2);
        }
        if (enr_freq_changed || unt_changed || igd_changed) {
            SEND_IF_DIRTY(bs300_encode_enr_upper_noise_th(ne, calib, new_it,
                                                            data), 0x8050C2);
        }
        if (sf_changed) {
            SEND_IF_DIRTY(bs300_encode_enr_smoothing(ne, data), 0x8060C2);
        }
        if (snr_changed || ma_changed || etr_changed) {
            SEND_IF_DIRTY(bs300_encode_enr_etr(ne, data), 0x8070C2);
        }
        if (snr_changed || ma_changed || nrr_changed) {
            SEND_IF_DIRTY(bs300_encode_enr_nrr(ne, data), 0x8080C2);
        }
        if (sasf_changed) {
            }
    }
#undef SEND_IF_DIRTY
}

static void switch_diff_pre_enr(soft_iic_dev iic,
                                 const bs300_modules_t *om, bs300_modules_t *nm,
                                 const bs300_calib_t *calib,
                                 u8 new_it, int igd_changed,
                                 bs300_sync_session_t *session,
                                 int *sent, int *fail, u8 *data)
{
    int ret;
#define SEND_IF_DIRTY(fn_call, cmd) do { \
    memset(data, 0, 48); \
    ret = (fn_call); \
    if (ret == 0) { \
        if (session) { \
            bs300_session_append(session, cmd, data); \
        } else { \
            ret = bs300_param_write_packet(iic, cmd, data); \
            if (ret == 0) { (*sent)++; } else { (*fail)++; \
                bs300_debug("bs300: switch FAIL cmd=0x%06X ret=%d\n", cmd, ret); \
            } \
        } \
    } \
} while(0)

    /* === DDM2 (before MM+ per HW sequence) === */
    {
        u8 o_dd = (om->input_selection == 5) ? 1 : 0;
        u8 n_dd = (nm->input_selection == 5) ? 1 : 0;
        if (o_dd == 0 && n_dd == 0) {
        } else if (o_dd == 0 && n_dd == 1) {
            SEND_IF_DIRTY(bs300_encode_ddm2(nm, calib, data), 0x800022);
        } else if (o_dd == 1 && n_dd == 0) {
            memset(data, 0, 48);
            set_word(data, 0, 0);
            if (session) {
                bs300_session_append(session, 0x800022, data);
            } else {
                ret = bs300_param_write_packet(iic, 0x800022, data);
                if (ret == 0) (*sent)++; else (*fail)++;
            }
        } else {
            int ddm2_changed = (om->open_ear != nm->open_ear)
                            || (om->polar_pattern != nm->polar_pattern)
                            || (om->adm_fdm != nm->adm_fdm);
            if (ddm2_changed) {
                SEND_IF_DIRTY(bs300_encode_ddm2(nm, calib, data), 0x800022);
            }
        }
    }

    /* === MM Plus === */
    {
        u8 o_mm = (om->input_selection == 4) ? 1 : 0;
        u8 n_mm = (nm->input_selection == 4) ? 1 : 0;
        if (o_mm == 0 && n_mm == 0) {
        } else if (o_mm == 0 && n_mm == 1) {
            SEND_IF_DIRTY(bs300_encode_mm_plus(nm, calib, new_it, data),
                          0x800062);
        } else if (o_mm == 1 && n_mm == 0) {
            memset(data, 0, 48);
            set_word(data, 0, 0);
            if (session) {
                bs300_session_append(session, 0x800062, data);
            } else {
                ret = bs300_param_write_packet(iic, 0x800062, data);
                if (ret == 0) (*sent)++; else (*fail)++;
            }
        } else {
            int mm_changed = (om->mix_ratio != nm->mix_ratio);
            if (mm_changed || igd_changed) {
                SEND_IF_DIRTY(bs300_encode_mm_plus(nm, calib, new_it, data),
                              0x800062);
            }
        }
    }

    /* === DFBC === */
    {
        u8 o_ena = (om->dfbc_enable_mode & 0x80) ? 1 : 0;
        u8 n_ena = (nm->dfbc_enable_mode & 0x80) ? 1 : 0;
        if (o_ena == 0 && n_ena == 0) {
        } else if (o_ena == 0 && n_ena == 1) {
            SEND_IF_DIRTY(bs300_encode_dfbc(nm, calib, data), 0x800052);
        } else if (o_ena == 1 && n_ena == 0) {
            memset(data, 0, 48);
            set_word(data, 0, 0);
            if (session) {
                bs300_session_append(session, 0x800052, data);
            } else {
                ret = bs300_param_write_packet(iic, 0x800052, data);
                if (ret == 0) (*sent)++; else (*fail)++;
            }
        } else {
            int dfbc_changed = (om->dfbc_enable_mode != nm->dfbc_enable_mode);
            if (dfbc_changed) {
                SEND_IF_DIRTY(bs300_encode_dfbc(nm, calib, data), 0x800052);
            }
        }
    }
#undef SEND_IF_DIRTY
}

static void switch_diff_post_enr(soft_iic_dev iic,
                                 const bs300_modules_t *om, bs300_modules_t *nm,
                                 const bs300_calib_t *calib,
                                 u8 new_it, int igd_changed,
                                 bs300_sync_session_t *session,
                                 int *sent, int *fail, u8 *data)
{
    int ret;
#define SEND_IF_DIRTY(fn_call, cmd) do { \
    memset(data, 0, 48); \
    ret = (fn_call); \
    if (ret == 0) { \
        if (session) { \
            bs300_session_append(session, cmd, data); \
        } else { \
            ret = bs300_param_write_packet(iic, cmd, data); \
            if (ret == 0) { (*sent)++; } else { (*fail)++; \
                bs300_debug("bs300: switch FAIL cmd=0x%06X ret=%d\n", cmd, ret); \
            } \
        } \
    } \
} while(0)

    /* === TC/DAI === */
    {
        int input_changed = (om->input_selection != nm->input_selection);
        u8 o_tc = (om->input_selection == 2
                   || om->input_selection == 3) ? 1 : 0;
        u8 n_tc = (nm->input_selection == 2
                   || nm->input_selection == 3) ? 1 : 0;
        if (o_tc == 0 && n_tc == 0) {
        } else if (o_tc == 0 && n_tc == 1) {
            SEND_IF_DIRTY(bs300_encode_tc_dai(calib, new_it, data),
                          0x804272);
        } else if (o_tc == 1 && n_tc == 0) {
            memset(data, 0, 48);
            if (session) {
                bs300_session_append(session, 0x804272, data);
            } else {
                ret = bs300_param_write_packet(iic, 0x804272, data);
                if (ret == 0) (*sent)++; else (*fail)++;
            }
        } else {
            if (input_changed || igd_changed) {
                SEND_IF_DIRTY(bs300_encode_tc_dai(calib, new_it, data),
                              0x804272);
            }
        }
    }

    /* === ISS === */
    {
        u8 o_ena = om->iss_enable;
        u8 n_ena = nm->iss_enable;
        if (o_ena == 0 && n_ena == 0) {
        } else if (o_ena == 0 && n_ena == 1) {
            SEND_IF_DIRTY(bs300_encode_iss(nm, calib, new_it, data), 0x8001B2);
        } else if (o_ena == 1 && n_ena == 0) {
            memset(data, 0, 48);
            set_word(data, 0, 0);
            if (session) {
                bs300_session_append(session, 0x8001B2, data);
            } else {
                ret = bs300_param_write_packet(iic, 0x8001B2, data);
                if (ret == 0) (*sent)++; else (*fail)++;
            }
        } else {
            int iss_changed = (om->iss_threshold != nm->iss_threshold);
            if (iss_changed || igd_changed) {
                SEND_IF_DIRTY(bs300_encode_iss(nm, calib, new_it, data),
                              0x8001B2);
            }
        }
    }

    /* === WNR === */
    {
        nm->wnr_enable_dual |= 0x01;
        u8 o_ena = om->wnr_enable_dual & 0x01;
        u8 n_ena = nm->wnr_enable_dual & 0x01;
        if (o_ena == 0 && n_ena == 0) {
        } else if (o_ena == 0 && n_ena == 1) {
            SEND_IF_DIRTY(bs300_encode_wnr_setup(nm, calib, data), 0x8001C2);
            SEND_IF_DIRTY(bs300_encode_wnr_band_0_15(nm, calib, new_it, data),
                          0x8011C2);
            SEND_IF_DIRTY(bs300_encode_wnr_band_16_31(nm, calib, new_it, data),
                          0x8411C2);
            SEND_IF_DIRTY(bs300_encode_wnr_single_mic(nm, calib, new_it, data),
                          0x8021C2);
        } else if (o_ena == 1 && n_ena == 0) {
            memset(data, 0, 48);
            set_word(data, 0, 0);
            if (session) {
                bs300_session_append(session, 0x8001C2, data);
            } else {
                ret = bs300_param_write_packet(iic, 0x8001C2, data);
                if (ret == 0) (*sent)++; else (*fail)++;
            }
        } else {
            int wnr_changed = (om->wnr_preset != nm->wnr_preset);
            if (wnr_changed || igd_changed) {
                SEND_IF_DIRTY(bs300_encode_wnr_setup(nm, calib, data),
                              0x8001C2);
            }
            if (igd_changed) {
                SEND_IF_DIRTY(bs300_encode_wnr_band_0_15(nm, calib, new_it,
                                                           data), 0x8011C2);
                SEND_IF_DIRTY(bs300_encode_wnr_band_16_31(nm, calib, new_it,
                                                            data), 0x8411C2);
                SEND_IF_DIRTY(bs300_encode_wnr_single_mic(nm, calib, new_it,
                                                            data), 0x8021C2);
            }
        }
    }

    /* === AGCO === */
    {
        u8 o_ena = om->agco_enable;
        u8 n_ena = nm->agco_enable;
        if (o_ena == 0 && n_ena == 0) {
        } else if (o_ena == 0 && n_ena == 1) {
            SEND_IF_DIRTY(bs300_encode_agco(nm, data), 0x800382);
        } else if (o_ena == 1 && n_ena == 0) {
            memset(data, 0, 48);
            set_word(data, 0, 0);
            if (session) {
                bs300_session_append(session, 0x800382, data);
            } else {
                ret = bs300_param_write_packet(iic, 0x800382, data);
                if (ret == 0) (*sent)++; else (*fail)++;
            }
        } else {
            int agco_changed = (om->agco_threshold_db != nm->agco_threshold_db)
                            || (om->agco_attack_01ms != nm->agco_attack_01ms)
                            || (om->agco_release_01ms != nm->agco_release_01ms);
            if (agco_changed) {
                SEND_IF_DIRTY(bs300_encode_agco(nm, data), 0x800382);
            }
        }
    }
#undef SEND_IF_DIRTY
}

/* ================================================================
 *  Single-parameter modify (App entry point)
 *  Reads old struct from VM, applies mod, saves back,
 *  then runs incremental diff against the snapshot.
 * ================================================================ */
int bs300_param_modify(soft_iic_dev iic, u8 prog_idx, u16 offset,
                       const u8 *val, u8 len)
{
    bs300_prog_struct_t old_prog, new_prog;
    u8 calib_raw[144];
    bs300_calib_t calib;
    u8 data[48];
    u8 old_it, new_it;
    int igd_changed;
    u8 *raw;
    u8 i;
    int sent = 0, fail = 0;
    int ret;

    if (val == NULL || prog_idx >= BS300_PROG_COUNT) return -1;
    if (offset + len > sizeof(bs300_prog_struct_t)) return -1;

    /* Load current struct as "old" snapshot */
    ret = bs300_vm_load_struct(prog_idx, &old_prog);
    if (ret < 0) return -1;

    /* Copy and apply modification */
    new_prog = old_prog;
    raw = (u8 *)&new_prog;
    for (i = 0; i < len; i++) {
        raw[offset + i] = val[i];
    }

    /* Save modified struct to VM */
    ret = bs300_vm_save_struct(prog_idx, &new_prog);
    if (ret < 0) return -1;

    /* Load calibration */
    ret = bs300_calib_vm_load(calib_raw);
    if (ret < 0) return -1;
    ret = bs300_parse_calibration(calib_raw, &calib);
    if (ret < 0) return -1;

    old_it = get_input_type(old_prog.modules.input_selection);
    new_it = get_input_type(new_prog.modules.input_selection);
    igd_changed = (old_it != new_it);

    /* Run all diff functions — only changed fields trigger I2C sends */
    switch_diff_pre_enr(iic, &old_prog.modules, &new_prog.modules,
                        &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_enr(iic, &old_prog.enr, &new_prog.enr,
                    &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_post_enr(iic, &old_prog.modules, &new_prog.modules,
                         &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_vol_beep(iic, &old_prog.modules, &new_prog.modules,
                         &calib, igd_changed, NULL, &sent, &fail, data);
    switch_diff_wdrc(iic, &old_prog.wdrc, &new_prog.wdrc,
                     &old_prog.modules, &new_prog.modules,
                     &calib, new_it, igd_changed, NULL, &sent, &fail, data);

    return (fail > 0) ? -1 : 0;
}

int bs300_switch_program(soft_iic_dev iic, u8 new_prog_idx)
{
    u8 data[48];
    u8 calib_raw[144];
    bs300_calib_t calib;
    bs300_prog_struct_t old_prog, new_prog;
    u8 active_prog;
    u8 old_it, new_it;
    int igd_changed;
    int sent = 0, fail = 0;
    int ret;

    if (new_prog_idx >= 4) return -1;

    /* Get current active program */
    ret = bs300_vm_read_active_prog(&active_prog);
    if (ret < 0) active_prog = 0;
    if (active_prog == new_prog_idx) return 0;

    /* Load old struct; if invalid, fall back to full sync */
    ret = bs300_vm_load_struct(active_prog, &old_prog);
    if (ret < 0) {
        bs300_debug("bs300: switch old struct fail, full sync\n");
        ret = bs300_vm_load_struct(new_prog_idx, &new_prog);
        if (ret < 0) return -1;
        bs300_vm_write_active_prog(new_prog_idx);
        return bs300_sync_program(iic, &new_prog);
    }
    ret = bs300_vm_load_struct(new_prog_idx, &new_prog);
    if (ret < 0) return -1;

    bs300_vm_write_active_prog(new_prog_idx);
    bs300_on_active_prog_changed(new_prog_idx);

    printf("[BS300] switch RAM %d→%d\n", active_prog, new_prog_idx);

    /* Load calibration (shared, never changes across programs) */
    ret = bs300_calib_vm_load(calib_raw);
    if (ret < 0) return -1;
    ret = bs300_parse_calibration(calib_raw, &calib);
    if (ret < 0) return -1;

    old_it = get_input_type(old_prog.modules.input_selection);
    new_it = get_input_type(new_prog.modules.input_selection);
    igd_changed = (old_it != new_it);

    /* Order per HW: DDM2→MM+→DFBC→ENR→TC/DAI→ISS→WNR→AGCO→Vol/Beep→WDRC */
    switch_diff_pre_enr(iic, &old_prog.modules, &new_prog.modules,
                        &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_enr(iic, &old_prog.enr, &new_prog.enr,
                    &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_post_enr(iic, &old_prog.modules, &new_prog.modules,
                         &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_vol_beep(iic, &old_prog.modules, &new_prog.modules,
                         &calib, igd_changed, NULL, &sent, &fail, data);
    switch_diff_wdrc(iic, &old_prog.wdrc, &new_prog.wdrc,
                     &old_prog.modules, &new_prog.modules,
                     &calib, new_it, igd_changed, NULL, &sent, &fail, data);

    /* Force DDM2/MM+ — always send if new prog uses them */
    if (new_prog.modules.input_selection == 5) {
        memset(data, 0, 48);
        bs300_encode_ddm2(&new_prog.modules, &calib, data);
        ret = bs300_param_write_packet(iic, 0x800022, data);
        if (ret == 0) sent++; else fail++;
    }
    if (new_prog.modules.input_selection == 4) {
        memset(data, 0, 48);
        bs300_encode_mm_plus(&new_prog.modules, &calib, new_it, data);
        ret = bs300_param_write_packet(iic, 0x800062, data);
        if (ret == 0) sent++; else fail++;
    }
    /* Force Vol/Beep — always send after mode switch */
    memset(data, 0, 48);
    if (bs300_encode_volume_beep(&new_prog.modules, &calib, data) == 0) {
        ret = bs300_param_write_packet(iic, 0x800081, data);
        if (ret == 0) sent++; else fail++;
    }

    bs300_debug("bs300: switch_program %d→%d done, sent=%d fail=%d\n",
                active_prog, new_prog_idx, sent, fail);
    return (fail > 0) ? -1 : 0;
}

/* ================================================================
 *  Non-blocking fill functions (build command queue, caller ticks)
 * ================================================================ */

int bs300_switch_program_start(bs300_sync_session_t *s, u8 new_prog_idx)
{
    u8 data[48];
    bs300_prog_struct_t old_prog, new_prog;
    u8 active_prog;
    u8 old_it, new_it;
    int igd_changed;
    int sent = 0, fail = 0;
    int ret;

    if (s == NULL || new_prog_idx >= 4) return -1;

    clr_wdt();
    ret = bs300_vm_read_active_prog(&active_prog);
    if (ret < 0) active_prog = 0;
    if (active_prog == new_prog_idx) return 0;
    ret = bs300_vm_load_struct(active_prog, &old_prog);
    if (ret < 0) return -1;
    clr_wdt();
    ret = bs300_vm_load_struct(new_prog_idx, &new_prog);
    if (ret < 0) return -1;
    clr_wdt();

#if TCFG_BS300_VOL_PER_PROGRAM
    /* Each program keeps its own volume — already loaded from VM */
#else
    /* Volume is program-common: carry over from current program */
    new_prog.modules.volume_level = old_prog.modules.volume_level;
#endif
    bs300_vm_save_struct(new_prog_idx, &new_prog);  // persist to VM

    bs300_vm_write_active_prog(new_prog_idx);
    bs300_on_active_prog_changed(new_prog_idx);

    printf("[BS300] switch RAM %d→%d\n", active_prog, new_prog_idx);

    {
        u8 calib_raw[144];
        bs300_calib_t calib;

        ret = bs300_calib_vm_load(calib_raw);
        if (ret < 0) return -1;
        clr_wdt();
        ret = bs300_parse_calibration(calib_raw, &calib);
        if (ret < 0) return -1;

        old_it = get_input_type(old_prog.modules.input_selection);
        new_it = get_input_type(new_prog.modules.input_selection);
        igd_changed = (old_it != new_it);

        /* Order per HW: DDM2→MM+→DFBC→ENR→TC/DAI→ISS→WNR→AGCO→Vol/Beep→WDRC */
        switch_diff_pre_enr(s->iic, &old_prog.modules, &new_prog.modules,
                            &calib, new_it, igd_changed, s, &sent, &fail, data);
        switch_diff_enr(s->iic, &old_prog.enr, &new_prog.enr,
                        &calib, new_it, igd_changed, s, &sent, &fail, data);
        switch_diff_post_enr(s->iic, &old_prog.modules, &new_prog.modules,
                             &calib, new_it, igd_changed, s, &sent, &fail, data);
        switch_diff_vol_beep(s->iic, &old_prog.modules, &new_prog.modules,
                             &calib, igd_changed, s, &sent, &fail, data);
        switch_diff_wdrc(s->iic, &old_prog.wdrc, &new_prog.wdrc,
                         &old_prog.modules, &new_prog.modules,
                         &calib, new_it, igd_changed, s, &sent, &fail, data);

        /* Force DDM2/MM+ — always send if new prog uses them
         * (chip may have been disabled by voice-prompt Telecoil switch) */
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
        /* Force Vol/Beep — always send after mode switch to ensure
         * input + volume are correct (diff may skip if unchanged) */
        memset(data, 0, 48);
        if (bs300_encode_volume_beep(&new_prog.modules, &calib, data) == 0) {
            bs300_session_append(s, 0x800081, data);
        }
        /* Force ENR — always send after mode switch
         * (chip may have been disabled by voice-prompt Telecoil switch) */
        memset(data, 0, 48);
        bs300_encode_enr_general(&new_prog.enr, data);
        bs300_session_append(s, 0x8000C2, data);
    }

#if TCFG_BS300_VOL_PER_PROGRAM
    {
        extern void bs300_sync_volume_tracking(u8 level);
        bs300_sync_volume_tracking(new_prog.modules.volume_level);
    }
#endif

    s->state = BS300_SYNC_SEND;
    return 0;
}

/* Public: diff-sync modified active program to RAM (only changed I2C commands).
 * new_prog is the caller's modified buffer; old is loaded from VM. */
int bs300_resync_diff(soft_iic_dev iic, bs300_prog_struct_t *_new)
{
    bs300_prog_struct_t old_prog;
    u8 calib_raw[144];
    bs300_calib_t calib;
    u8 active_prog;
    u8 old_it, new_it;
    int igd_changed;
    u8 data[48];
    int sent = 0, fail = 0;
    int ret;

    if (_new == NULL) return -1;

    ret = bs300_vm_read_active_prog(&active_prog);
    if (ret < 0) return -1;

    ret = bs300_vm_load_struct(active_prog, &old_prog);
    if (ret < 0) return -1;

    ret = bs300_calib_vm_load(calib_raw);
    if (ret < 0) return -1;
    ret = bs300_parse_calibration(calib_raw, &calib);
    if (ret < 0) return -1;

    old_it = get_input_type(old_prog.modules.input_selection);
    new_it = get_input_type(_new->modules.input_selection);
    igd_changed = (old_it != new_it);

    printf("[BS300] resync diff prog=%d\n", active_prog);

    switch_diff_pre_enr(iic, &old_prog.modules, &_new->modules,
                        &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_enr(iic, &old_prog.enr, &_new->enr,
                    &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_post_enr(iic, &old_prog.modules, &_new->modules,
                         &calib, new_it, igd_changed, NULL, &sent, &fail, data);
    switch_diff_vol_beep(iic, &old_prog.modules, &_new->modules,
                         &calib, igd_changed, NULL, &sent, &fail, data);
    switch_diff_wdrc(iic, &old_prog.wdrc, &_new->wdrc,
                     &old_prog.modules, &_new->modules,
                     &calib, new_it, igd_changed, NULL, &sent, &fail, data);

    printf("[BS300] resync_diff done, sent=%d fail=%d\n", sent, fail);
    return (fail > 0) ? -1 : 0;
}

int bs300_resync_diff_start(bs300_sync_session_t *s, bs300_prog_struct_t *_new)
{
    bs300_prog_struct_t old_prog;
    u8 calib_raw[144];
    bs300_calib_t calib;
    u8 active_prog;
    u8 old_it, new_it;
    int igd_changed;
    u8 data[48];
    int sent = 0, fail = 0;
    int ret;

    if (s == NULL || _new == NULL) return -1;

    clr_wdt();
    ret = bs300_vm_read_active_prog(&active_prog);
    if (ret < 0) return -1;

    ret = bs300_vm_load_struct(active_prog, &old_prog);
    if (ret < 0) return -1;
    clr_wdt();

    ret = bs300_calib_vm_load(calib_raw);
    if (ret < 0) return -1;
    ret = bs300_parse_calibration(calib_raw, &calib);
    if (ret < 0) return -1;

    old_it = get_input_type(old_prog.modules.input_selection);
    new_it = get_input_type(_new->modules.input_selection);
    igd_changed = (old_it != new_it);

    printf("[BS300] resync diff async prog=%d\n", active_prog);

    switch_diff_pre_enr(s->iic, &old_prog.modules, &_new->modules,
                        &calib, new_it, igd_changed, s, &sent, &fail, data);
    switch_diff_enr(s->iic, &old_prog.enr, &_new->enr,
                    &calib, new_it, igd_changed, s, &sent, &fail, data);
    switch_diff_post_enr(s->iic, &old_prog.modules, &_new->modules,
                         &calib, new_it, igd_changed, s, &sent, &fail, data);
    switch_diff_vol_beep(s->iic, &old_prog.modules, &_new->modules,
                         &calib, igd_changed, s, &sent, &fail, data);
    switch_diff_wdrc(s->iic, &old_prog.wdrc, &_new->wdrc,
                     &old_prog.modules, &_new->modules,
                     &calib, new_it, igd_changed, s, &sent, &fail, data);

    printf("[BS300] resync_diff_start: sent=%d fail=%d\n", sent, fail);

    s->state = BS300_SYNC_SEND;
    return (fail > 0) ? -1 : 0;
}

int bs300_param_modify_start(bs300_sync_session_t *s, u8 prog_idx,
                              u16 offset, const u8 *val, u8 len)
{
    bs300_prog_struct_t old_prog, new_prog;
    u8 data[48];
    u8 old_it, new_it;
    int igd_changed;
    u8 *raw;
    u8 i;
    int sent = 0, fail = 0;
    int ret;

    if (s == NULL || val == NULL || prog_idx >= BS300_PROG_COUNT) return -1;
    if (offset + len > sizeof(bs300_prog_struct_t)) return -1;

    ret = bs300_vm_load_struct(prog_idx, &old_prog);
    if (ret < 0) return -1;

    new_prog = old_prog;
    raw = (u8 *)&new_prog;
    for (i = 0; i < len; i++) {
        raw[offset + i] = val[i];
    }

    ret = bs300_vm_save_struct(prog_idx, &new_prog);
    if (ret < 0) return -1;

    {
        u8 calib_raw[144];
        bs300_calib_t calib;

        ret = bs300_calib_vm_load(calib_raw);
        if (ret < 0) return -1;
        ret = bs300_parse_calibration(calib_raw, &calib);
        if (ret < 0) return -1;

        old_it = get_input_type(old_prog.modules.input_selection);
        new_it = get_input_type(new_prog.modules.input_selection);
        igd_changed = (old_it != new_it);

        /* Order per HW: DDM2→MM+→DFBC→ENR→TC/DAI→ISS→WNR→AGCO→Vol/Beep→WDRC */
        switch_diff_pre_enr(s->iic, &old_prog.modules, &new_prog.modules,
                            &calib, new_it, igd_changed, s, &sent, &fail, data);
        switch_diff_enr(s->iic, &old_prog.enr, &new_prog.enr,
                        &calib, new_it, igd_changed, s, &sent, &fail, data);
        switch_diff_post_enr(s->iic, &old_prog.modules, &new_prog.modules,
                             &calib, new_it, igd_changed, s, &sent, &fail, data);
        switch_diff_vol_beep(s->iic, &old_prog.modules, &new_prog.modules,
                             &calib, igd_changed, s, &sent, &fail, data);
        switch_diff_wdrc(s->iic, &old_prog.wdrc, &new_prog.wdrc,
                         &old_prog.modules, &new_prog.modules,
                         &calib, new_it, igd_changed, s, &sent, &fail, data);
    }

    s->state = BS300_SYNC_SEND;
    return 0;
}
