#include "bs300_param_encode.h"
#include "bs300_param_tables.h"
#include <string.h>

/* u32 type needed by bs300_encode_tables.h (not defined in RSL10 SDK) */
typedef uint32_t u32;
#include "bs300_encode_tables.h"

/* ============================================================
 * Math helpers (static)
 * ============================================================ */

static void bs300_set_word(uint8_t *data, uint8_t wi, uint32_t val)
{
    uint8_t off = wi * 3;
    data[off]     = (uint8_t)(val & 0xFF);
    data[off + 1] = (uint8_t)((val >> 8) & 0xFF);
    data[off + 2] = (uint8_t)((val >> 16) & 0xFF);
}


/* Convert dB value (in 0.1 dB units) to unsigned frac24.
 * Formula: ceil(val_db * 65536 / 6.02)
 * Integer: (abs_n * 327680 + 300) / 301   where abs_n = |val_db_tenths| */
static uint32_t db_to_frac24(int32_t val_db_tenths)
{
    int64_t n = val_db_tenths;
    if (n < 0) n = -n;
    return (uint32_t)(((n * 327680LL + 300LL) / 301LL) & 0xFFFFFFLL);
}

/* Convert dB value (in 0.1 dB units) to signed int24.
 * Truncation toward zero. */
static uint32_t db_to_int24(int32_t val_db_tenths)
{
    int64_t n = val_db_tenths;
    int64_t result;
    if (n >= 0)
        result = (n * 327680LL) / 301LL;
    else
        result = -((-n * 327680LL) / 301LL);
    return (uint32_t)(result & 0xFFFFFFLL);
}

static int32_t clamp_s32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int32_t avg_ceil(const int16_t *vals, uint8_t count)
{
    int32_t sum = 0;
    uint8_t i;
    for (i = 0; i < count; i++) sum += vals[i];
    return (sum + count - 1) / count;
}

/* Pack uint6 values: 4 values per 24-bit word, LSB = values[wi*4] */
static void pack_uint6_4pw(uint8_t *data, uint8_t word_start,
                            const uint8_t *values, uint8_t count)
{
    uint8_t wi, i;
    for (wi = 0; wi < (count + 3) / 4; wi++) {
        uint32_t word = 0;
        for (i = 0; i < 4; i++) {
            uint8_t vi = wi * 4 + i;
            uint8_t v = (vi < count) ? (values[vi] & 0x3F) : 1;
            word |= (uint32_t)v << (i * 6);
        }
        bs300_set_word(data, word_start + wi, word);
    }
}

/* Pack int12 values: 2 values per 24-bit word, LSB = values[wi*2] */
static void pack_int12_2pw(uint8_t *data, uint8_t word_start,
                            const int16_t *values, uint8_t count)
{
    uint8_t wi;
    for (wi = 0; wi < count; wi++) {
        uint32_t w0 = (uint32_t)(values[wi] & 0xFFF);
        bs300_set_word(data, word_start + wi, w0);
    }
}

/* ============================================================
 * WDRC General Setup (0x8000B2)
 * ============================================================ */

void bs300_enc_wdrc_general(uint8_t total_channels, bool limiter,
                             uint8_t data_out[48])
{
    memset(data_out, 0, 48);
    bs300_set_word(data_out, 0, 0x000001);
    bs300_set_word(data_out, 1, total_channels);
    bs300_set_word(data_out, 2, 0);                    /* NSBC = 0 */
    bs300_set_word(data_out, 3, total_channels);       /* NMBC = all channels */
    bs300_set_word(data_out, 4, 3);                    /* kp_mode: 3 = 2KP */
    bs300_set_word(data_out, 5, limiter ? 1 : 0);
}

/* ============================================================
 * WDRC Frequency Spacing (0x8010B2)
 * ============================================================ */

void bs300_enc_wdrc_freq_spacing(const uint8_t freq_idx[16],
                                  uint8_t num_channels, uint8_t data_out[48])
{
    uint8_t mbc_counts[16];
    uint8_t i;
    memset(data_out, 0, 48);

    for (i = 0; i < num_channels - 1; i++)
        mbc_counts[i] = freq_idx[i + 1] - freq_idx[i];
    mbc_counts[num_channels - 1] = 32 - freq_idx[num_channels - 1];

    /* Convert to MBC_CHx = bin_count - 1, minimum 1 (each MBC has >= 2 bins) */
    for (i = 0; i < num_channels; i++) {
        if (mbc_counts[i] < 2) mbc_counts[i] = 1;
        else mbc_counts[i] = mbc_counts[i] - 1;
    }
    for (i = num_channels; i < 16; i++)
        mbc_counts[i] = 1;

    pack_uint6_4pw(data_out, 0, mbc_counts, 16);

    /* Fill unused words with 0x041041 */
    {
        uint8_t used_words = (16 + 3) / 4;
        for (i = used_words; i < 16; i++)
            bs300_set_word(data_out, i, 0x041041);
    }
}

/* ============================================================
 * WDRC KP Threshold (0x8020B2)
 * ============================================================ */

void bs300_enc_wdrc_kp_threshold(const bs300_wdrc_t *wdrc,
                                  const bs300_calib_data_t *calib,
                                  const char *input_type, uint8_t data_out[48])
{
    int32_t igd_tenth, igd_db, mic1_cal, th, encoded;
    uint8_t i, fidx;
    memset(data_out, 0, 48);

    igd_tenth = bs300_calib_input_gain_diff_tenth_db(calib, input_type);
    igd_db = igd_tenth / 10;

    for (i = 0; i < wdrc->num_channels && i < 16; i++) {
        fidx = wdrc->channels[i].frequency_idx;
        mic1_cal = ((int32_t)calib->mic1_band[fidx]
                  + (int32_t)calib->mic1_band[fidx + 1]) / 2;

        /* KP1 */
        th = (int32_t)(int8_t)wdrc->channels[i].kp1_th;
        encoded = 60 + th - mic1_cal - igd_db;
        data_out[i * 2] = (uint8_t)(clamp_s32(encoded, -128, 127) & 0xFF);

        /* KP2 */
        th = (int32_t)(int8_t)wdrc->channels[i].kp2_th;
        encoded = 60 + th - mic1_cal - igd_db;
        data_out[i * 2 + 1] = (uint8_t)(clamp_s32(encoded, -128, 127) & 0xFF);
    }
}

/* ============================================================
 * WDRC Attack Time (0x8030B2)
 * ============================================================ */

void bs300_enc_wdrc_attack_time(const bs300_wdrc_t *wdrc,
                                 uint8_t data_out[48])
{
    uint8_t i;
    memset(data_out, 0, 48);
    for (i = 0; i < wdrc->num_channels && i < 16; i++) {
        uint32_t epd = wdrc->channels[i].epd_at & 0xFF;
        uint32_t kp1 = wdrc->channels[i].kp1_at & 0xFF;
        uint32_t kp2 = wdrc->channels[i].kp2_at & 0xFF;
        bs300_set_word(data_out, i, epd | (kp1 << 8) | (kp2 << 16));
    }
}

/* ============================================================
 * WDRC Release Time (0x8040B2)
 * ============================================================ */

void bs300_enc_wdrc_release_time(const bs300_wdrc_t *wdrc,
                                  uint8_t data_out[48])
{
    uint8_t i;
    memset(data_out, 0, 48);
    for (i = 0; i < wdrc->num_channels && i < 16; i++) {
        uint32_t epd = wdrc->channels[i].epd_rt & 0xFF;
        uint32_t kp1 = wdrc->channels[i].kp1_rt & 0xFF;
        uint32_t kp2 = wdrc->channels[i].kp2_rt & 0xFF;
        bs300_set_word(data_out, i, epd | (kp1 << 8) | (kp2 << 16));
    }
}

/* ============================================================
 * WDRC Ratio (0x8050B2)
 * ============================================================ */

void bs300_enc_wdrc_ratio(const bs300_wdrc_t *wdrc,
                           uint8_t data_out[48])
{
    uint8_t i;
    memset(data_out, 0, 48);
    for (i = 0; i < wdrc->num_channels && i < 16; i++) {
        uint32_t epd = wdrc->channels[i].epd_r & 0xFF;
        uint32_t kp1 = wdrc->channels[i].kp1_r & 0xFF;
        uint32_t kp2 = wdrc->channels[i].kp2_r & 0xFF;
        bs300_set_word(data_out, i, epd | (kp1 << 8) | (kp2 << 16));
    }
}

/* ============================================================
 * WDRC Bin Gain (0x8060B2)
 * ============================================================ */

void bs300_enc_wdrc_bin_gain(const bs300_wdrc_t *wdrc,
                              const bs300_calib_data_t *calib,
                              const char *input_type, uint8_t data_out[48])
{
    int32_t igd_tenth, igd_db;
    uint8_t i;
    memset(data_out, 0, 48);

    igd_tenth = bs300_calib_input_gain_diff_tenth_db(calib, input_type);
    igd_db = igd_tenth / 10;

    for (i = 0; i < 32; i++) {
        /* Flash bin_gain = 27 + value_in_MT, convert to signed dB */
        int32_t bg_db = (int32_t)(int8_t)(wdrc->bin_gain[i]) - 27;
        int32_t gain_cal = (int32_t)calib->output_band[i]
                         - (int32_t)calib->mic1_band[i];
        int32_t encoded = bg_db - gain_cal + igd_db;
        data_out[i] = (uint8_t)(clamp_s32(encoded, -128, 127) & 0xFF);
    }
}

/* ============================================================
 * WDRC Lmt Threshold (0x8070B2)
 * ============================================================ */

void bs300_enc_wdrc_lmt_threshold(const bs300_wdrc_t *wdrc,
                                   const bs300_calib_data_t *calib,
                                   uint8_t data_out[48])
{
    uint8_t i;
    memset(data_out, 0, 48);

    for (i = 0; i < wdrc->num_channels && i < 16; i++) {
        uint8_t fidx = wdrc->channels[i].frequency_idx;
        int32_t out_cal = ((int32_t)calib->output_band[fidx]
                        + (int32_t)calib->output_band[fidx + 1]) / 2;
        /* Flash lmt_th = value_in_MT - 30, add 30 back */
        int32_t th = (int32_t)(int8_t)wdrc->channels[i].lmt_th + 30;
        int32_t encoded = 60 + th - out_cal;
        data_out[i] = (uint8_t)(clamp_s32(encoded, -128, 127) & 0xFF);
    }
}

/* ============================================================
 * WDRC Lmt Attack/Release/Ratio (indices as-is)
 * ============================================================ */

void bs300_enc_wdrc_lmt_attack(const bs300_wdrc_t *wdrc,
                                uint8_t data_out[48])
{
    uint8_t i;
    memset(data_out, 0, 48);
    for (i = 0; i < wdrc->num_channels && i < 16; i++)
        data_out[i] = wdrc->channels[i].lmt_at;
}

void bs300_enc_wdrc_lmt_release(const bs300_wdrc_t *wdrc,
                                 uint8_t data_out[48])
{
    uint8_t i;
    memset(data_out, 0, 48);
    for (i = 0; i < wdrc->num_channels && i < 16; i++)
        data_out[i] = wdrc->channels[i].lmt_rt;
}

void bs300_enc_wdrc_lmt_ratio(const bs300_wdrc_t *wdrc,
                               uint8_t data_out[48])
{
    uint8_t i;
    memset(data_out, 0, 48);
    for (i = 0; i < wdrc->num_channels && i < 16; i++)
        data_out[i] = wdrc->channels[i].lmt_r;
}

/* ============================================================
 * Volume/Beep/Input (0x800081)
 * ============================================================ */

void bs300_enc_volume_beep(const bs300_volume_t *vol, uint8_t input_selection,
                            const bs300_calib_data_t *calib,
                            uint8_t data_out[48])
{
    uint8_t beep_band, batt_band;
    int32_t outcal_beep, batt_outcal, idx;

    memset(data_out, 0, 48);

    /* Beep level: beep_frequency (data word) directly maps to cal band.
     * For beep freqs at 250Hz multiples, data_word = Hz/250 = cal_band. */
    beep_band = vol->beep_frequency;
    if (beep_band > 31) beep_band = 31;
    outcal_beep = (int32_t)(int8_t)calib->output_band[beep_band];
    idx = outcal_beep - (int32_t)(int8_t)vol->beep_level + BS300_BEEP_TABLE_OFFSET;
    if (idx < 0) idx = 0;
    if (idx >= BS300_BEEP_TABLE_SIZE) idx = BS300_BEEP_TABLE_SIZE - 1;
    bs300_set_word(data_out, 0, bs300_beep_frac24_table[idx]);

    bs300_set_word(data_out, 1, vol->beep_frequency & 0xFF);

    /* Min/Max volume: int24 via db_to_int24 */
    bs300_set_word(data_out, 2, db_to_int24((int32_t)(int8_t)vol->min_volume * 10));
    bs300_set_word(data_out, 3, db_to_int24((int32_t)(int8_t)vol->max_volume * 10));

    bs300_set_word(data_out, 4, input_selection & 0xFF);

    /* Battery flat beep */
    batt_band = vol->battery_flat_beep_frequency;
    if (batt_band > 31) batt_band = 31;
    bs300_set_word(data_out, 5, vol->battery_flat_beep_frequency & 0xFF);
    batt_outcal = (int32_t)(int8_t)calib->output_band[batt_band];
    idx = batt_outcal - (int32_t)(int8_t)vol->battery_flat_beep_level
          + BS300_BEEP_TABLE_OFFSET;
    if (idx < 0) idx = 0;
    if (idx >= BS300_BEEP_TABLE_SIZE) idx = BS300_BEEP_TABLE_SIZE - 1;
    bs300_set_word(data_out, 6, bs300_beep_frac24_table[idx]);
}

/* ============================================================
 * DFBC (0x800052)
 * ============================================================ */

void bs300_enc_dfbc(uint8_t mode, const bs300_calib_data_t *calib,
                     uint8_t data_out[48])
{
    int32_t delay_n;
    memset(data_out, 0, 48);
    bs300_set_word(data_out, 0, mode & 0xFF);

    /* delay_n = round(fbc_bulk_delay / 62.5)
     * fbc_bulk_delay is in us. 62.5 us = 1/16000 * 1e6.
     * Integer: (fbc_bulk_delay * 10 + 312) / 625  (round) */
    delay_n = ((int32_t)calib->fbc_bulk_delay * 10 + 312) / 625;
    bs300_set_word(data_out, 1, clamp_u32((uint32_t)delay_n, 0, 524));
}

/* ============================================================
 * ISS (0x8001B2)
 * ============================================================ */

void bs300_enc_iss(uint8_t threshold_dbspl, const bs300_calib_data_t *calib,
                    const char *input_type, uint8_t data_out[48])
{
    int32_t mic1_cal, igd_tenth;
    int32_t exponent;
    int64_t frac48;
    int8_t thr_signed;

    memset(data_out, 0, 48);

    /* Word 0: ISS threshold = 0x010000 - ceil(thr * 65536 / 6.02) */
    thr_signed = (int8_t)threshold_dbspl;
    bs300_set_word(data_out, 0,
                   0x010000 - db_to_frac24((int32_t)thr_signed * 10));

    /* Word 1: frac48 low, Word 2: frac48 high */
    mic1_cal = bs300_calib_avg_mic1(calib);
    igd_tenth = bs300_calib_input_gain_diff_tenth_db(calib, input_type);

    /* exponent = (-3 - thr + mic1_cal_avg + igd) / 10
     * All in integer units: (-30 - thr*10 + mic1_cal*10 + igd_tenth) / 100 */
    exponent = (-30 - (int32_t)thr_signed * 10
                + mic1_cal * 10 + igd_tenth) / 100;

    /* frac48 = round(1.0 / (10**exponent) * (1<<47))
     * For exponent ∈ [-5, 3]: use pre-computed values.
     * frac48 = (1LL << 47) / 10^exponent (for positive exponent).
     * frac48 = (1LL << 47) * 10^(-exponent) (for negative exponent). */
    if (exponent >= 0) {
        int64_t denom = 1;
        int32_t e;
        for (e = 0; e < exponent; e++) denom *= 10;
        frac48 = ((1LL << 47) + denom / 2) / denom;
    } else {
        int64_t numer = 1LL << 47;
        int32_t e;
        for (e = 0; e < -exponent; e++) numer *= 10;
        frac48 = numer;
    }

    bs300_set_word(data_out, 1, (uint32_t)(frac48 & 0xFFFFFF));
    bs300_set_word(data_out, 2, (uint32_t)((frac48 >> 24) & 0xFFFFFF));
}

/* ============================================================
 * WNR General (0x8001C2)
 * ============================================================ */

void bs300_enc_wnr_general(const bs300_calib_data_t *calib,
                            uint8_t suppression_preset, uint8_t data_out[48])
{
    int32_t avg_all, detect_val, mic2_cal_idx;
    uint32_t mic2_cal;

    memset(data_out, 0, 48);

    /* Word 0: selection (enable=1, dual_mic=0) */
    bs300_set_word(data_out, 0, 1);

    /* Word 1: detection threshold = round((75 - ceil_avg(mic1)) * 65536/6.02/8) */
    avg_all = avg_ceil(calib->mic1_band, 32);
    detect_val = (int32_t)(((75LL - avg_all) * 327680LL + 150LL) / 301LL / 8LL);
    bs300_set_word(data_out, 1, (uint32_t)(detect_val & 0xFFFFFF));

    /* Word 2: mic2 cal = 0x800000 * 10^(mic2_gain_diff/200)
     * Use beep table: table[x+255] = 0x7FFFFF * 10^(x/20)
     * mic2_gain_diff is in 0.1 dB, so x = mic2_gain_diff/10.
     * table[mic2_gain_diff/10 + 255] * 0x800000/0x7FFFFF ≈ table[idx] */
    mic2_cal_idx = calib->mic2_gain_diff / 10 + 255;
    if (mic2_cal_idx < 0) mic2_cal_idx = 0;
    if (mic2_cal_idx >= BS300_BEEP_TABLE_SIZE)
        mic2_cal_idx = BS300_BEEP_TABLE_SIZE - 1;
    mic2_cal = bs300_beep_frac24_table[mic2_cal_idx];
    mic2_cal = (uint32_t)(((uint64_t)mic2_cal * 0x800000ULL) / 0x7FFFFFULL);
    bs300_set_word(data_out, 2, mic2_cal & 0xFFFFFF);

    /* Word 3: suppression strength preset */
    bs300_set_word(data_out, 3, (suppression_preset >= 5) ? 0x000006 : 0x000003);

    /* Words 4-6: fixed values */
    bs300_set_word(data_out, 4, 0x001543);
    bs300_set_word(data_out, 5, 0x2aaaab);
    bs300_set_word(data_out, 6, 0x200000);
}

/* ============================================================
 * WNR Band Data (common implementation for bands 0-15 and 16-31)
 * ============================================================ */

static void enc_wnr_bands(const bs300_calib_data_t *calib,
                           const char *input_type,
                           uint8_t band_start, uint8_t data_out[48])
{
    int32_t igd_tenth, igd_db;
    uint8_t bi;

    memset(data_out, 0, 48);
    igd_tenth = bs300_calib_input_gain_diff_tenth_db(calib, input_type);
    igd_db = igd_tenth / 10;

    for (bi = 0; bi < 16; bi++) {
        uint8_t band = band_start + bi;
        int32_t offset = (int32_t)bs300_wnr_ssp_offset[band][0];
        int32_t mic1 = (int32_t)(int8_t)calib->mic1_band[band];
        int32_t val_db = (mic1 + igd_db) * 2 - offset;
        uint32_t val = 0x2A9764 - db_to_frac24(val_db * 10);
        bs300_set_word(data_out, bi, val & 0xFFFFFF);
    }
}

void bs300_enc_wnr_bands_0_15(const bs300_calib_data_t *calib,
                               const char *input_type, uint8_t data_out[48])
{
    enc_wnr_bands(calib, input_type, 0, data_out);
}

void bs300_enc_wnr_bands_16_31(const bs300_calib_data_t *calib,
                                const char *input_type, uint8_t data_out[48])
{
    enc_wnr_bands(calib, input_type, 16, data_out);
}

/* ============================================================
 * WNR Single Mic Detection (0x8021C2)
 * ============================================================ */

void bs300_enc_wnr_single_mic(const bs300_calib_data_t *calib,
                               const char *input_type, uint8_t data_out[48])
{
    int32_t igd_tenth, igd_db;
    uint8_t band;

    memset(data_out, 0, 48);
    igd_tenth = bs300_calib_input_gain_diff_tenth_db(calib, input_type);
    igd_db = igd_tenth / 10;

    for (band = 0; band < 3; band++) {
        int32_t offset = (int32_t)bs300_wnr_data2_offset[band][0];
        int32_t mic1 = (int32_t)(int8_t)calib->mic1_band[band];
        int32_t val_db = (mic1 + igd_db) * 2 - offset;
        uint32_t val = 3292041 - db_to_frac24(val_db * 10);
        bs300_set_word(data_out, band, val & 0xFFFFFF);
    }
}

/* ============================================================
 * ENR General Setup (0x8000C2)
 * ============================================================ */

void bs300_enc_enr_general(uint8_t num_channels, uint8_t data_out[48])
{
    memset(data_out, 0, 48);
    bs300_set_word(data_out, 0, 1);                  /* selection */
    bs300_set_word(data_out, 1, num_channels);
    bs300_set_word(data_out, 2, 2);                  /* sbc = 2 */
    bs300_set_word(data_out, 3, num_channels - 2);   /* mbc */
}

/* ============================================================
 * ENR Frequency Spacing (0x8010C2)
 * ============================================================ */

void bs300_enc_enr_freq_spacing(const uint8_t freq_idx[16],
                                 uint8_t num_channels, uint8_t data_out[48])
{
    uint8_t band_counts[16];
    int16_t values[16];
    uint8_t i;
    memset(data_out, 0, 48);

    for (i = 0; i < num_channels - 1; i++)
        band_counts[i] = freq_idx[i + 1] - freq_idx[i];
    band_counts[num_channels - 1] = 32 - freq_idx[num_channels - 1];

    for (i = 0; i < num_channels; i++)
        values[i] = (int16_t)band_counts[i];
    for (i = num_channels; i < 16; i++)
        values[i] = 0;

    pack_int12_2pw(data_out, 0, values, 16);
}

/* ============================================================
 * ENR SNR Threshold (0x8020C2)
 * ============================================================ */

void bs300_enc_enr_snr_threshold(const bs300_enr_t *enr,
                                  uint8_t data_out[48])
{
    int16_t values[16];
    uint8_t i;
    memset(data_out, 0, 48);

    for (i = 0; i < enr->num_channels && i < 16; i++) {
        /* Formula: round(32 / 6.02 * snr_th_db)
         * Integer: round(snr_th * 32 * 10 / 602 * 10)...
         * Actually: round(snr_th_db * 327680 / 301 / 10 / 10)
         * snr_th_db = snrth from flash (value_in_MT, as-is)
         * round(snrth * 327680 / 30100) */
        int32_t snr_db = (int32_t)enr->channels[i].snrth;
        values[i] = (int16_t)(((int64_t)snr_db * 327680LL + 15050LL) / 30100LL);
    }
    pack_int12_2pw(data_out, 0, values, enr->num_channels);
}

/* ============================================================
 * ENR Max Attenuation (0x8030C2)
 * ============================================================ */

void bs300_enc_enr_max_att(const bs300_enr_t *enr,
                            uint8_t data_out[48])
{
    int16_t values[16];
    uint8_t i;
    memset(data_out, 0, 48);

    for (i = 0; i < enr->num_channels && i < 16; i++) {
        /* Formula: floor(max_att / snr_th * 256)
         * max_att = ma (from flash, value_in_MT)
         * snr_th = snrth (from flash, value_in_MT)
         * floor(max_att * 256 / snr_th) */
        int32_t ma = (int32_t)enr->channels[i].ma;
        int32_t snr = (int32_t)enr->channels[i].snrth;
        if (snr == 0) snr = 1;
        values[i] = (int16_t)((ma * 256) / snr);
    }
    pack_int12_2pw(data_out, 0, values, enr->num_channels);
}

/* ============================================================
 * ENR Noise Threshold (0x8040C2)
 * ============================================================ */

static int32_t enr_mic1_cal_round_half_up(const bs300_calib_data_t *calib,
                                           uint8_t fidx, uint8_t count)
{
    int32_t sum = 0;
    uint8_t i;
    for (i = 0; i < count; i++)
        sum += (int32_t)(int8_t)calib->mic1_band[fidx + i];
    /* Round half-up: t = sum * 10 / count; result = t/10 + (t%10>=5 ? 1 : 0) */
    int32_t t = sum * 10 / count;
    return t / 10 + ((t % 10) >= 5 ? 1 : 0);
}

void bs300_enc_enr_noise_th(const bs300_enr_t *enr,
                             const bs300_calib_data_t *calib,
                             const char *input_type, uint8_t data_out[48])
{
    int32_t igd_tenth, igd_db, mic1_cal, nt, val;
    uint8_t i, fidx, cnt;
    int16_t values[16];
    memset(data_out, 0, 48);

    igd_tenth = bs300_calib_input_gain_diff_tenth_db(calib, input_type);
    igd_db = igd_tenth / 10;

    for (i = 0; i < enr->num_channels && i < 16; i++) {
        fidx = enr->channels[i].frequency_idx;
        cnt = (i < enr->num_channels - 1)
              ? enr->channels[i + 1].frequency_idx - fidx
              : 32 - fidx;
        mic1_cal = enr_mic1_cal_round_half_up(calib, fidx, cnt);
        /* Flash NT = value_in_MT - 10, add 10 back */
        nt = (int32_t)enr->channels[i].nt + 10;
        /* Formula: round(5.307 * (nt + 130 - mic1_cal - igd) - 371.2)
         * Integer: round(5307 * (nt + 130 - mic1_cal - igd_db) / 1000 - 371.2)
         * = round((5307 * adj - 371200) / 1000)
         * where adj = nt + 130 - mic1_cal - igd_db */
        int32_t adj = nt + 130 - mic1_cal - igd_db;
        val = ((int64_t)5307LL * adj - 371200LL + 500LL) / 1000LL;
        values[i] = (int16_t)(clamp_s32((int32_t)val, -2048, 2047) & 0xFFF);
    }
    pack_int12_2pw(data_out, 0, values, enr->num_channels);
}

/* ============================================================
 * ENR Upper Noise Threshold (0x8050C2)
 * ============================================================ */

void bs300_enc_enr_upper_noise_th(const bs300_enr_t *enr,
                                   const bs300_calib_data_t *calib,
                                   const char *input_type, uint8_t data_out[48])
{
    int32_t igd_tenth, igd_db, mic1_cal, unt, val;
    uint8_t i, fidx, cnt;
    int16_t values[16];
    memset(data_out, 0, 48);

    igd_tenth = bs300_calib_input_gain_diff_tenth_db(calib, input_type);
    igd_db = igd_tenth / 10;

    for (i = 0; i < enr->num_channels && i < 16; i++) {
        fidx = enr->channels[i].frequency_idx;
        cnt = (i < enr->num_channels - 1)
              ? enr->channels[i + 1].frequency_idx - fidx
              : 32 - fidx;
        mic1_cal = enr_mic1_cal_round_half_up(calib, fidx, cnt);
        /* Flash UNT = value_in_MT - 10, add 10 back */
        unt = (int32_t)enr->channels[i].unt + 10;
        int32_t adj = unt + 130 - mic1_cal - igd_db;
        val = ((int64_t)5307LL * adj - 371200LL + 500LL) / 1000LL;
        values[i] = (int16_t)(clamp_s32((int32_t)val, -2048, 2047) & 0xFFF);
    }
    pack_int12_2pw(data_out, 0, values, enr->num_channels);
}

/* ============================================================
 * ENR Smoothing (0x8060C2)
 * ============================================================ */

void bs300_enc_enr_smoothing(const bs300_enr_t *enr,
                              uint8_t data_out[48])
{
    memset(data_out, 0, 48);

    /* Word 0-3: nhsf, nfsf, nnsf, snasf (each value - 1, uint8) */
    bs300_set_word(data_out, 0, (enr->nhsf >= 1) ? (enr->nhsf - 1) : 0);
    bs300_set_word(data_out, 1, (enr->nfsf >= 1) ? (enr->nfsf - 1) : 0);
    bs300_set_word(data_out, 2, (enr->nnsf >= 1) ? (enr->nnsf - 1) : 0);
    /* snasf: chip overrides to 4 regardless of flash config */
    bs300_set_word(data_out, 3, 4 - 1);
}

/* ============================================================
 * ENR ETR (0x8070C2)
 * ============================================================ */

void bs300_enc_enr_etr(const bs300_enr_t *enr,
                        uint8_t data_out[48])
{
    uint8_t i;
    memset(data_out, 0, 48);

    for (i = 0; i < enr->num_channels && i < 16; i++) {
        /* ETR frac24 = round(etr / 100 * 0x800000 / max_att)
         * Actually from Python codegen: frac24 = int(etr_ratio * 0x800000 / ma)
         * etr_ratio = etr / 100.0, ma is max_att
         * So: frac24 = etr * 0x800000 / (100 * ma)  with rounding */
        int32_t etr = (int32_t)enr->channels[i].etr;
        int32_t ma = (int32_t)enr->channels[i].ma;
        uint32_t val;
        if (ma == 0) ma = 1;
        val = (uint32_t)(((int64_t)etr * 0x800000LL + (int64_t)ma * 50LL)
                         / ((int64_t)ma * 100LL));
        bs300_set_word(data_out, i, val & 0xFFFFFF);
    }
}

/* ============================================================
 * ENR NRR (0x8080C2)
 * ============================================================ */

void bs300_enc_enr_nrr(const bs300_enr_t *enr,
                        uint8_t data_out[48])
{
    uint8_t i;
    memset(data_out, 0, 48);

    for (i = 0; i < enr->num_channels && i < 16; i++) {
        /* NRR frac24 = round(nrr / 10 * 0x800000 / max_att)
         * nrr is stored as x10 in flash
         * So: frac24 = nrr * 0x800000 / (10 * ma)  with rounding */
        int32_t nrr = (int32_t)enr->channels[i].nrr;
        int32_t ma = (int32_t)enr->channels[i].ma;
        uint32_t val;
        if (ma == 0) ma = 1;
        val = (uint32_t)(((int64_t)nrr * 0x800000LL + (int64_t)ma * 5LL)
                         / ((int64_t)ma * 10LL));
        bs300_set_word(data_out, i, val & 0xFFFFFF);
    }
}

/* ============================================================
 * AGCO (0x800382)
 * ============================================================ */

void bs300_enc_agco(uint8_t threshold_db, uint16_t attack_01ms,
                     uint16_t release_01ms, uint8_t data_out[48])
{
    int8_t thr_signed;
    uint32_t atk_frac, rel_frac;

    memset(data_out, 0, 48);

    /* Word 0: selection */
    bs300_set_word(data_out, 0, 1);

    /* Word 1: threshold = 0xFA0000 - ceil(|thr| * 65536 / 6.02) */
    thr_signed = (int8_t)threshold_db;
    {
        int32_t abs_thr = (int32_t)thr_signed;
        if (abs_thr < 0) abs_thr = -abs_thr;
        bs300_set_word(data_out, 1,
                       0xFA0000 - db_to_frac24(abs_thr * 10));
    }

    /* Word 2: attack time frac24 = 1 - exp(-16 / (atk_sec * 16000))
     * This is a pre-computed table lookup.
     * For now, use a simplified approach: small table for common values.
     * atk_01ms = attack time in 0.1ms units; atk_sec = atk_01ms / 10000.
     * exp_arg = -16 / (atk_01ms / 10000 * 16000) = -10 / atk_01ms
     * frac24 = round((1 - exp(-10/atk_01ms)) * 0xFFFFFF)
     * For atk_01ms == 0, use 0xFFFFFF (instant).
     */
    if (attack_01ms == 0) {
        atk_frac = 0xFFFFFF;
    } else {
        /* 1 - exp(-10 / atk_01ms) ≈ 10/atk_01ms for large atk_01ms
         * Use Taylor: 1 - exp(-x) ≈ x for small x.
         * frac24 = round(0xFFFFFF * 10 / atk_01ms) */
        atk_frac = ((uint64_t)0xFFFFFFULL * 10ULL
                    + (uint64_t)attack_01ms / 2ULL)
                   / (uint64_t)attack_01ms;
        if (atk_frac > 0xFFFFFF) atk_frac = 0xFFFFFF;
    }
    bs300_set_word(data_out, 2, atk_frac & 0xFFFFFF);

    /* Word 3: release time frac24 — same formula */
    if (release_01ms == 0) {
        rel_frac = 0xFFFFFF;
    } else {
        rel_frac = ((uint64_t)0xFFFFFFULL * 10ULL
                    + (uint64_t)release_01ms / 2ULL)
                   / (uint64_t)release_01ms;
        if (rel_frac > 0xFFFFFF) rel_frac = 0xFFFFFF;
    }
    bs300_set_word(data_out, 3, rel_frac & 0xFFFFFF);
}

/* ============================================================
 * DDM2 / MM+ / TC-DAI (disabled / passthrough)
 * ============================================================ */

void bs300_enc_ddm2_disabled(uint8_t data_out[48])
{
    memset(data_out, 0, 48);
}

void bs300_enc_mm_plus_disabled(uint8_t data_out[48])
{
    memset(data_out, 0, 48);
}

void bs300_enc_tc_dai_gain_diff(const bs300_calib_data_t *calib,
                                 uint8_t data_out[48])
{
    int32_t n;
    int64_t val;

    memset(data_out, 0, 48);

    /* Word 0: ig_diff = (gain_db * 2) * (65536 / 6.02)
     * gain_db = telecoil_gain_diff / 10 (tenth-dB → dB)
     * Integer: (n * 2 * 327680) / 301, truncation toward zero */
    n = calib->telecoil_gain_diff;
    if (n >= 0)
        val = (n * 2LL * 327680LL) / 301LL;
    else
        val = -((-n * 2LL * 327680LL) / 301LL);
    bs300_set_word(data_out, 0, (uint32_t)(val & 0xFFFFFF));
}

