#include "bs300_param_encode.h"
#include "bs300_encode_tables.h"
#include "app.h"
#include <rsl10.h>
/* rsl10: use app.h */
#include "bs300_encode_tables.h"
/* rsl10: no app_config */

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

/* ================================================================
 *  Bit-level reading helpers for Flash decode
 * ================================================================ */

typedef struct {
    const uint8_t *data;
    uint16_t byte_pos;
    uint8_t  bit_pos;
} bit_reader_t;

static void br_init(bit_reader_t *br, const uint8_t *data)
{
    br->data = data;
    br->byte_pos = 0;
    br->bit_pos = 0;
}

static uint32_t br_read(bit_reader_t *br, uint8_t n_bits)
{
    uint32_t val = 0;
    uint8_t i;

    for (i = 0; i < n_bits; i++) {
        uint8_t byte_val = br->data[br->byte_pos];
        uint8_t bit = (byte_val >> br->bit_pos) & 1;
        val |= ((uint32_t)bit << i);
        br->bit_pos++;
        if (br->bit_pos >= 8) {
            br->bit_pos = 0;
            br->byte_pos++;
        }
    }
    return val;
}

static void br_skip(bit_reader_t *br, uint8_t n_bits)
{
    uint16_t total = (uint16_t)br->byte_pos * 8 + br->bit_pos + n_bits;
    br->byte_pos = total / 8;
    br->bit_pos = (uint8_t)(total % 8);
}

/* ================================================================
 *  Bit-level writing helpers for Flash encode
 * ================================================================ */

typedef struct {
    uint8_t *data;
    uint16_t byte_pos;
    uint8_t  bit_pos;
} bit_writer_t;

static void bw_init(bit_writer_t *bw, uint8_t *data)
{
    bw->data = data;
    bw->byte_pos = 0;
    bw->bit_pos = 0;
}

static void bw_write(bit_writer_t *bw, uint32_t value, uint8_t n_bits)
{
    uint8_t i;
    for (i = 0; i < n_bits; i++) {
        if (value & 1)
            bw->data[bw->byte_pos] |= (uint8_t)(1 << bw->bit_pos);
        else
            bw->data[bw->byte_pos] &= (uint8_t)(~(1 << bw->bit_pos));
        value >>= 1;
        bw->bit_pos++;
        if (bw->bit_pos >= 8) {
            bw->bit_pos = 0;
            bw->byte_pos++;
        }
    }
}

static void bw_skip(bit_writer_t *bw, uint8_t n_bits)
{
    uint16_t total = (uint16_t)bw->byte_pos * 8 + bw->bit_pos + n_bits;
    bw->byte_pos = total / 8;
    bw->bit_pos = (uint8_t)(total % 8);
}

static uint16_t bw_total_bits(const bit_writer_t *bw)
{
    return (uint16_t)bw->byte_pos * 8 + bw->bit_pos;
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

static module_type_t get_module_type(uint8_t cmd_data)
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

static void decode_wdrc_channel(bit_reader_t *br, uint8_t ch_idx,
                                bs300_wdrc_t *wdrc)
{
    wdrc->freq_idx[ch_idx] = (uint8_t)br_read(br, 6);
    wdrc->epd_at_idx[ch_idx] = (uint8_t)br_read(br, 7);
    wdrc->epd_rt_idx[ch_idx] = (uint8_t)br_read(br, 7);
    wdrc->epd_r_idx[ch_idx] = (uint8_t)br_read(br, 7);
    br_skip(br, 2);
    wdrc->kp1_th_db[ch_idx] = (int8_t)br_read(br, 7);
    wdrc->kp2_th_db[ch_idx] = (int8_t)br_read(br, 7);
    br_skip(br, 2);
    wdrc->kp1_at_idx[ch_idx] = (uint8_t)br_read(br, 7);
    wdrc->kp2_at_idx[ch_idx] = (uint8_t)br_read(br, 7);
    br_skip(br, 2);
    wdrc->kp1_rt_idx[ch_idx] = (uint8_t)br_read(br, 7);
    wdrc->kp2_rt_idx[ch_idx] = (uint8_t)br_read(br, 7);
    br_skip(br, 2);
    wdrc->kp1_r_idx[ch_idx] = (uint8_t)br_read(br, 7);
    wdrc->kp2_r_idx[ch_idx] = (uint8_t)br_read(br, 7);
    wdrc->lmt_th_db[ch_idx] = (int8_t)(br_read(br, 7) + 30); /* raw = value_in_MT - 30 */
    wdrc->lmt_at_idx[ch_idx] = (uint8_t)br_read(br, 7);
    wdrc->lmt_rt_idx[ch_idx] = (uint8_t)br_read(br, 7);
    wdrc->lmt_r_idx[ch_idx] = (uint8_t)br_read(br, 7);
}

/* WDRC Flash decode: bit-packed → value_in_MT.
 * Field offsets (Flash raw ≠ value_in_MT!):
 *   bin_gain: raw = 27 + value_in_MT  →  value_in_MT = raw - 27
 *   lmt_th:   raw = value_in_MT - 30  →  value_in_MT = raw + 30
 *   kp1_th, kp2_th: raw = value_in_MT  (no offset)
 * See .claude/skills/bs300/docs/Reference_Tables.md §2 for full table. */
static int decode_wdrc_flash(const uint8_t *data, bs300_wdrc_t *wdrc)
{
    bit_reader_t br;
    uint8_t limiter;
    uint8_t kp_mode;
    uint8_t num_ch;
    uint8_t i;

    br_init(&br, data);

    br_skip(&br, 1);              /* B0 bit0: fixed 0b1 */
    limiter = (uint8_t)br_read(&br, 1);
    kp_mode = (uint8_t)br_read(&br, 1);
    br_skip(&br, 5);              /* B0 bits 7:3: 0 */

    br_skip(&br, 1);              /* B1 bit0: 0b1 marker */

    for (i = 0; i < 32; i++) {
        uint8_t raw_gain = (uint8_t)br_read(&br, 7);
        wdrc->bin_gain[i] = (int8_t)((int)raw_gain - 27);
    }

    num_ch = (uint8_t)br_read(&br, 5); /* B29[5:1] */
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
 *  WDRC Flash encode — reverse of decode_wdrc_flash
 * ================================================================ */

static void encode_wdrc_channel_flash(bit_writer_t *bw, uint8_t ch_idx,
                                      const bs300_wdrc_t *wdrc)
{
    bw_write(bw, wdrc->freq_idx[ch_idx], 6);
    bw_write(bw, wdrc->epd_at_idx[ch_idx], 7);
    bw_write(bw, wdrc->epd_rt_idx[ch_idx], 7);
    bw_write(bw, wdrc->epd_r_idx[ch_idx], 7);
    bw_skip(bw, 2);
    bw_write(bw, (uint32_t)(uint8_t)wdrc->kp1_th_db[ch_idx], 7);
    bw_write(bw, (uint32_t)(uint8_t)wdrc->kp2_th_db[ch_idx], 7);
    bw_skip(bw, 2);
    bw_write(bw, wdrc->kp1_at_idx[ch_idx], 7);
    bw_write(bw, wdrc->kp2_at_idx[ch_idx], 7);
    bw_skip(bw, 2);
    bw_write(bw, wdrc->kp1_rt_idx[ch_idx], 7);
    bw_write(bw, wdrc->kp2_rt_idx[ch_idx], 7);
    bw_skip(bw, 2);
    bw_write(bw, wdrc->kp1_r_idx[ch_idx], 7);
    bw_write(bw, wdrc->kp2_r_idx[ch_idx], 7);
    /* lmt_th: raw = value_in_MT - 30 */
    {
        int32_t raw = (int32_t)wdrc->lmt_th_db[ch_idx] - 30;
        if (raw < 0) raw = 0;
        if (raw > 127) raw = 127;
        bw_write(bw, (uint32_t)raw, 7);
    }
    bw_write(bw, wdrc->lmt_at_idx[ch_idx], 7);
    bw_write(bw, wdrc->lmt_rt_idx[ch_idx], 7);
    bw_write(bw, wdrc->lmt_r_idx[ch_idx], 7);
}

static int encode_wdrc_flash(uint8_t *data, uint16_t max_bytes,
                              const bs300_wdrc_t *wdrc)
{
    bit_writer_t bw;
    uint8_t i;

    bw_init(&bw, data);

    /* B0: [fixed 1:1] [limiter:1] [kp_mode:1] [zeros:5] */
    bw_write(&bw, 1, 1);
    bw_write(&bw, wdrc->limiter, 1);
    bw_write(&bw, (wdrc->kp_mode == 1) ? 0 : 1, 1);
    bw_skip(&bw, 5);

    /* B1: [marker 1:1], then 32 × bin_gain (raw = value_in_MT + 27) */
    bw_write(&bw, 1, 1);
    for (i = 0; i < 32; i++) {
        int32_t raw = (int32_t)wdrc->bin_gain[i] + 27;
        if (raw < 0) raw = 0;
        if (raw > 127) raw = 127;
        bw_write(&bw, (uint32_t)raw, 7);
    }

    /* num_ch:5, reserved:1 */
    bw_write(&bw, wdrc->total_channels, 5);
    bw_skip(&bw, 1);

    /* Per-channel data */
    for (i = 0; i < wdrc->total_channels; i++) {
        encode_wdrc_channel_flash(&bw, i, wdrc);
    }

    {
        uint16_t bits = bw_total_bits(&bw);
        uint16_t bytes = (bits + 7) / 8;
        if (bytes > max_bytes) return -1;
        return (int)bytes;
    }
}

/* ================================================================
 *  Volume/Beep Flash decode
 * ================================================================ */

static int decode_volume_flash(const uint8_t *data, bs300_modules_t *mod)
{
    mod->vol_enable = 1;
    mod->beep_level = data[0];
    mod->beep_freq_idx = data[1];
    mod->min_vol = (data[3] > 127) ? ((int8_t)data[3]) : (int8_t)data[3];
    mod->max_vol = (data[4] > 127) ? ((int8_t)data[4]) : (int8_t)data[4];
    mod->batt_beep_level = data[5];
    mod->batt_beep_freq_idx = data[6];
    return 0;
}

/* ================================================================
 *  Input selection mapping
 * ================================================================ */

static uint8_t cmd_data_to_input_selection(uint8_t cmd_data)
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

static int decode_dfbc_flash(const uint8_t *data, bs300_modules_t *mod)
{
    mod->dfbc_enable_mode = data[0];
    return 0;
}

/* ================================================================
 *  ENR Flash decode
 * ================================================================ */

/* ENR Flash decode: bit-packed → value_in_MT.
 * Field offsets:
 *   noise_th (nt):       raw = value_in_MT - 10  →  value_in_MT = raw + 10
 *   upper_noise_th (unt): raw = value_in_MT - 40  →  value_in_MT = raw + 40
 *   snr_th, max_att, etr, nrr: raw = value_in_MT  (no offset) */
static int decode_enr_flash(const uint8_t *data, bs300_enr_t *enr)
{
    bit_reader_t br;
    uint8_t nfsf;
    uint8_t nhsf;
    uint8_t nnsf;
    uint8_t num_ch_low;
    uint8_t num_ch_high;
    uint8_t num_ch;
    uint8_t snasf;
    uint8_t i;

    br_init(&br, data);

    nfsf = (uint8_t)br_read(&br, 4);
    nhsf = (uint8_t)br_read(&br, 4);
    nnsf = (uint8_t)br_read(&br, 4);
    num_ch_low = (uint8_t)br_read(&br, 4);
    num_ch_high = (uint8_t)br_read(&br, 2);
    num_ch = num_ch_low | (num_ch_high << 4);

    enr->enable_num_ch = 0x80 | (num_ch + 1);
    enr->nfsf = (nfsf == 0) ? 1 : nfsf;
    enr->nhsf = (nhsf == 0) ? 1 : nhsf;
    enr->nnsf = (nnsf == 0) ? 1 : nnsf;

    for (i = 0; i < (num_ch + 1) && i < 16; i++) {
        enr->freq_idx[i] = (uint8_t)br_read(&br, 6);
        enr->max_att_db[i] = (uint8_t)br_read(&br, 5);
        enr->snr_th_db[i] = (uint8_t)br_read(&br, 5);
        enr->noise_th_db[i] = (uint8_t)(br_read(&br, 6) + 10);       /* raw = value_in_MT - 10 */
        enr->upper_noise_th_db[i] = (uint8_t)(br_read(&br, 6) + 40); /* raw = value_in_MT - 40 */
        enr->etr_x100[i] = (uint8_t)br_read(&br, 7);
        enr->nrr_x10[i] = (uint8_t)br_read(&br, 4);
    }

    snasf = (uint8_t)br_read(&br, 4);
    enr->snasf = (snasf == 0) ? 1 : snasf;

    for (i = 0; i < 16; i++) {
        enr->sasf[i] = 1;
    }

    return 0;
}

/* Reverse of decode_enr_flash — see field offset table above. */
static int encode_enr_flash(uint8_t *data, uint16_t max_bytes,
                             const bs300_enr_t *enr)
{
    bit_writer_t bw;
    uint8_t num_ch;
    uint8_t i;

    bw_init(&bw, data);

    num_ch = enr->enable_num_ch & 0x0F;
    if (num_ch == 0) num_ch = 16;
    if (num_ch > 16) num_ch = 16;

    /* Global smoothing factors */
    bw_write(&bw, enr->nfsf, 4);
    bw_write(&bw, enr->nhsf, 4);
    bw_write(&bw, enr->nnsf, 4);
    bw_write(&bw, num_ch & 0x0F, 4);
    bw_write(&bw, (num_ch >> 4) & 0x03, 2);

    /* Per-channel data */
    for (i = 0; i < num_ch && i < 16; i++) {
        int32_t raw;

        bw_write(&bw, enr->freq_idx[i], 6);
        bw_write(&bw, enr->max_att_db[i], 5);       /* raw = value_in_MT */
        bw_write(&bw, enr->snr_th_db[i], 5);         /* raw = value_in_MT */

        raw = (int32_t)enr->noise_th_db[i] - 10;     /* raw = value_in_MT - 10 */
        if (raw < 0) raw = 0; if (raw > 63) raw = 63;
        bw_write(&bw, (uint32_t)raw, 6);

        raw = (int32_t)enr->upper_noise_th_db[i] - 40; /* raw = value_in_MT - 40 */
        if (raw < 0) raw = 0; if (raw > 63) raw = 63;
        bw_write(&bw, (uint32_t)raw, 6);

        bw_write(&bw, enr->etr_x100[i], 7);          /* raw = value_in_MT */
        bw_write(&bw, enr->nrr_x10[i], 4);           /* raw = value_in_MT */
    }

    bw_write(&bw, enr->snasf, 4);

    {
        uint16_t bits = bw_total_bits(&bw);
        uint16_t bytes = (bits + 7) / 8;
        if (bytes > max_bytes) return -1;
        return (int)bytes;
    }
}

/* ================================================================
 *  ISS Flash decode
 * ================================================================ */

static int decode_iss_flash(const uint8_t *data, bs300_modules_t *mod)
{
    mod->iss_enable = 1;
    mod->iss_threshold = data[0];
    return 0;
}

/* ================================================================
 *  WNR Flash decode
 * ================================================================ */

static int decode_wnr_flash(const uint8_t *data, bs300_modules_t *mod)
{
    mod->wnr_enable_dual = data[0] | 0x01;
    mod->wnr_preset = data[1];
    return 0;
}

/* ================================================================
 *  AGCO Flash decode
 * ================================================================ */

static int decode_agco_flash(const uint8_t *data, bs300_modules_t *mod)
{
    uint16_t attack;
    uint16_t release;

    attack = (uint16_t)data[0] | ((uint16_t)(data[1] & 0x0F) << 8);
    release = ((uint16_t)(data[1] & 0xF0) >> 4) | ((uint16_t)data[2] << 4);
    mod->agco_enable = 1;
    mod->agco_threshold_db = (int8_t)data[3];
    mod->agco_attack_01ms = attack;
    mod->agco_release_01ms = release;
    return 0;
}

/* ================================================================
 *  Main Flash → Struct conversion
 * ================================================================ */

int bs300_flash_to_struct(const uint8_t *flash_buf, bs300_prog_struct_t *out)
{
    uint8_t module_count;
    uint8_t i;
    uint16_t pos;
    int ret;

    if (flash_buf == NULL || out == NULL) {
        return -1;
    }
    {
        uint16_t idx;
        uint8_t *raw = (uint8_t *)out;
        for (idx = 0; idx < sizeof(bs300_prog_struct_t); idx++) {
            raw[idx] = 0x00;
        }
    }

    /* Default runtime values */
    out->modules.volume_level = 9;  /* default 9 → vol_gain=0dB */
    out->modules.eq_low = 0;
    out->modules.eq_mid = 0;
    out->modules.eq_high = 0;

    /* Validate header */
    if (flash_buf[1] != 0x80 || flash_buf[2] != 0x00) {
        PRINTF("[BS300] flash_to_struct invalid header\r\n");
        return -1;
    }
    module_count = flash_buf[3] - 1;
    if (module_count == 0 || module_count > 16) {
        PRINTF("[BS300] flash_to_struct bad module_count=%d\r\n",
                    module_count);
        return -1;
    }

    /* Parse module directory */
    pos = 4;
    for (i = 0; i < module_count; i++) {
        uint8_t cmd_data = flash_buf[pos];
        uint8_t length_words = flash_buf[pos + 2];
        (void)length_words;
        pos += 3;
        (void)cmd_data;
    }

    /* Skip FB 00 marker */
    pos += 2;

    /* Decode each module's data */
    for (i = 0; i < module_count; i++) {
        uint8_t cmd_data = flash_buf[4 + i * 3];
        uint8_t length_words = flash_buf[4 + i * 3 + 2];
        uint16_t length_bytes = (uint16_t)length_words * 3;
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
                if (length_bytes >= 3) {
                    int16_t raw = (int16_t)flash_buf[pos];
                    out->modules.mix_ratio = (int8_t)(raw - 50);
                    out->modules.mm_type = flash_buf[pos + 1];
                }
            } else if (out->modules.input_selection == 5) {
                const uint8_t *ddm2 = flash_buf + pos;
                uint8_t byte2 = ddm2[2];

                out->modules.ddm2_enable = 1;
                out->modules.open_ear    = (byte2 >> 6) & 1;
                out->modules.adm_fdm     = (byte2 >> 5) & 1;
                out->modules.polar_pattern = byte2 & 0x07;
                out->modules.omni_threshold = ddm2[1];

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
            PRINTF("[BS300] flash_to_struct module=%d fail\n",
                        cmd_data);
            return ret;
        }
        pos += length_bytes;
    }

    PRINTF("[BS300] flash_to_struct ok, modules=%d, channels=%d\r\n",
                module_count, out->wdrc.total_channels);
    return 0;
}

/* ================================================================
 *  struct_to_flash — reverse: modify 480B flash in-place from struct
 * ================================================================ */

int bs300_struct_to_flash(const bs300_prog_struct_t *prog, uint8_t *flash_buf)
{
    uint8_t module_count;
    uint8_t i;
    uint16_t dir_end;
    uint16_t pos;
    int new_len;

    uint8_t found_wdrc = 0;
    uint8_t found_enr  = 0;

    if (prog == NULL || flash_buf == NULL) return -1;
    if (flash_buf[1] != 0x80 || flash_buf[2] != 0x00) return -1;

    module_count = flash_buf[3] - 1;
    if (module_count == 0 || module_count > 16) return -1;

    dir_end = 4U + (uint16_t)module_count * 3U;
    pos = dir_end + 2;  /* skip "FB 00" marker */

    for (i = 0; i < module_count; i++) {
        uint8_t cmd_data  = flash_buf[4 + i * 3];
        uint8_t length_w  = flash_buf[4 + i * 3 + 2];
        uint16_t length_b = (uint16_t)length_w * 3;

        if (cmd_data == 0x12) {
            /* Re-encode WDRC */
            uint8_t mod_buf[320];
            memset(mod_buf, 0, sizeof(mod_buf));
            new_len = encode_wdrc_flash(mod_buf, length_b, &prog->wdrc);
            if (new_len < 0) return -1;
            if ((uint16_t)new_len > length_b) {
                PRINTF("[BS300] struct_to_flash: WDRC overflow %d > %u\r\n",
                       new_len, length_b);
                return -1;
            }
            memcpy(flash_buf + pos, mod_buf, (uint16_t)new_len);
            if ((uint16_t)new_len < length_b)
                memset(flash_buf + pos + new_len, 0, length_b - (uint16_t)new_len);
            found_wdrc = 1;
        }
        else if (cmd_data == 0x1C) {
            /* Re-encode ENR */
            uint8_t mod_buf[200];
            memset(mod_buf, 0, sizeof(mod_buf));
            new_len = encode_enr_flash(mod_buf, length_b, &prog->enr);
            if (new_len < 0) return -1;
            if ((uint16_t)new_len > length_b) {
                PRINTF("[BS300] struct_to_flash: ENR overflow %d > %u\r\n",
                       new_len, length_b);
                return -1;
            }
            memcpy(flash_buf + pos, mod_buf, (uint16_t)new_len);
            if ((uint16_t)new_len < length_b)
                memset(flash_buf + pos + new_len, 0, length_b - (uint16_t)new_len);
            found_enr = 1;
        }
        pos += length_b;
    }

    if (!found_wdrc) {
        PRINTF("[BS300] struct_to_flash: WDRC module not found\r\n");
        return -1;
    }
    /* ENR is optional — some programs may not have it */
    if (!found_enr) {
        PRINTF("[BS300] struct_to_flash: ENR module not found (skipped)\r\n");
    }

    return 0;
}

/* ================================================================
 *  Lookup tables for Param I2C encoding
 * ================================================================ */

/* Frequency table: index → Hz, 32 entries, 250Hz step from idx 1 */
static const uint16_t freq_table[32] = {
    0, 125, 375, 625, 875, 1125, 1375, 1625,
    1875, 2125, 2375, 2625, 2875, 3125, 3375, 3625,
    3875, 4125, 4375, 4625, 4875, 5125, 5375, 5625,
    5875, 6125, 6375, 6625, 6875, 7125, 7375, 7625,
};

/* Beep frequency index → Hz mapping */
static const uint16_t beep_idx_to_hz[25] = {
    0, 250, 500, 750, 1000, 1250, 1500, 1750,
    2000, 2250, 2500, 2750, 3000, 3250, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/* WNR suppression strength offset: [band 0-31][preset 0-4] */
static const int8_t wnr_ssp_offset[32][5] = {
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
static const int8_t wnr_data2_offset[3][5] = {
    {  0,  -8, -16, -26, -34 },
    { -8, -16, -24, -32, -40 },
    {-40, -50, -58, -68, -78 },
};

/* WDRC KP per-channel calibration offset (cross-validated with codegen Step 5) */
static const int8_t wdrc_kp_cal_offset[16] = {
    0, 0, 0, 1, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

/* WDRC Lmt per-channel calibration offset (cross-validated with codegen Step 5) */
static const int8_t wdrc_lmt_cal_offset[16] = {
    0, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0,
};

/* WNR preset index → SSP level mapping */
static const uint8_t wnr_preset_to_ssp[5] = { 0, 1, 3, 6, 12 };

/* ================================================================
 *  Math helpers (integer arithmetic, matching chip behavior)
 * ================================================================ */

static int32_t clamp_s32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint32_t db_to_frac24(int32_t n_tenths_db)
{
    /* ceil(val_db * 65536 / 6.02) as unsigned frac24.
     * n_tenths_db = round(val_db * 10).
     * scaling: (n/10) * 65536 / 6.02 = n * 327680 / 301 */
    return ((n_tenths_db * 327680 + 300) / 301) & 0xFFFFFF;
}

static uint32_t db_to_int24(int32_t n_tenths_db)
{
    /* trunc(val_db * 65536 / 6.02) as signed int24. */
    int32_t result;
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
static int32_t get_input_gain_diff_tenth_db(uint8_t input_type, const bs300_calib_t *calib)
{
    if (input_type == INPUT_TYPE_TELECOIL) {
        return (int32_t)calib->telecoil_gain_diff;
    } else if (input_type == INPUT_TYPE_DAI) {
        return (int32_t)calib->dai_gain_diff;
    }
    return 0;
}

/* Apply input_gain_diff (tenth-dB) to a value computed in tenth-dB,
 * then truncate to integer dB (matching Python int() → C truncation toward zero).
 * numer_tenth_db: numerator in tenth-dB units.
 * igd: input_gain_diff in tenth-dB.
 * Returns: trunc((numer_tenth_db - igd) / 10). */
static int32_t apply_igd_trunc(int32_t numer_tenth_db, int32_t igd)
{
    return (numer_tenth_db - igd) / 10;
}

/* Round-half-away-from-zero: (num + den/2) / den for num>=0, (num - den/2) / den for num<0.
 * Matches Python round() for values not at exact .5 boundary. */
static int32_t round_div(int32_t num, int32_t den)
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
static int32_t enr_nt_int(uint8_t nt_db, int32_t mic1_cal, int32_t igd)
{
    int32_t x10 = (int32_t)nt_db * 10 + 1300 - mic1_cal * 10 - igd;
    int32_t num = 5307 * x10 - 3712000;
    return round_div(num, 10000);
}

/* ================================================================
 *  Packing helpers for 48-byte data section
 * ================================================================ */

static void pack_bytes(uint8_t *data, uint8_t start, const uint8_t *values, uint8_t count)
{
    uint8_t i;
    for (i = 0; i < count; i++) {
        data[start + i] = values[i];
    }
}

static void pack_int12_2pw(uint8_t *data, uint8_t word_start, const int16_t *values, uint8_t count)
{
    uint8_t i;
    for (i = 0; i < count; i++) {
        uint8_t wi = word_start + i / 2;
        uint32_t w = (uint32_t)data[wi * 3] | ((uint32_t)data[wi * 3 + 1] << 8)
              | ((uint32_t)data[wi * 3 + 2] << 16);
        if (i % 2 == 0) {
            w = (w & 0xFFF000) | ((uint32_t)values[i] & 0xFFF);
        } else {
            w = (w & 0x000FFF) | (((uint32_t)values[i] & 0xFFF) << 12);
        }
        data[wi * 3]     = (uint8_t)(w & 0xFF);
        data[wi * 3 + 1] = (uint8_t)((w >> 8) & 0xFF);
        data[wi * 3 + 2] = (uint8_t)((w >> 16) & 0xFF);
    }
}

static void pack_uint6_4pw(uint8_t *data, uint8_t word_start, const uint8_t *values, uint8_t count)
{
    uint8_t i;
    for (i = 0; i < count; i++) {
        uint8_t wi = word_start + i / 4;
        uint8_t shift = (uint8_t)((3 - (i % 4)) * 6);
        uint32_t mask = (uint32_t)0x3F << shift;
        uint32_t w = (uint32_t)data[wi * 3] | ((uint32_t)data[wi * 3 + 1] << 8)
              | ((uint32_t)data[wi * 3 + 2] << 16);
        w = (w & ~mask) | (((uint32_t)values[i] & 0x3F) << shift);
        data[wi * 3]     = (uint8_t)(w & 0xFF);
        data[wi * 3 + 1] = (uint8_t)((w >> 8) & 0xFF);
        data[wi * 3 + 2] = (uint8_t)((w >> 16) & 0xFF);
    }
}

static void set_word(uint8_t *data, uint8_t n, uint32_t value)
{
    uint16_t off = (uint16_t)n * 3;
    data[off]     = (uint8_t)(value & 0xFF);
    data[off + 1] = (uint8_t)((value >> 8) & 0xFF);
    data[off + 2] = (uint8_t)((value >> 16) & 0xFF);
}

/* ================================================================
 *  Input type constants
 * ================================================================ */

/* get_input_type() — defined in bs300_ram_sync.c (caller context) */

/* ================================================================
 *  Calibration band lookup
 * ================================================================ */

static uint8_t freq_to_cal_band(uint16_t hz)
{
    uint8_t best = 0;
    int32_t best_dist = 999999;
    uint8_t i;
    for (i = 1; i < 32; i++) {
        int32_t d = (int32_t)hz - (int32_t)freq_table[i];
        if (d < 0) d = -d;
        if (d < best_dist) {
            best_dist = d;
            best = i;
        }
    }
    return best;
}

/* bs300_parse_calibration — defined in bs300_calib.c */

/* ================================================================
 *  ENR helpers: band counts and multi-band mic1 averaging
 * ================================================================ */

/* Derive per-channel calibration band count from ENR freq indices.
 * Channel i covers cal bands from freq_idx[i] to freq_idx[i+1]-1
 * (or 31 for the last channel). */
static void enr_compute_band_counts(const bs300_enr_t *enr, uint8_t *band_counts)
{
    uint8_t total_ch = enr->enable_num_ch & 0x3F;
    uint8_t i;
    for (i = 0; i < 16; i++) {
        if (i < total_ch) {
            uint8_t start = enr->freq_idx[i];
            uint8_t end = (i + 1 < total_ch) ? enr->freq_idx[i + 1] : 32;
            band_counts[i] = (end > start) ? (end - start) : 1;
        } else {
            band_counts[i] = 0;
        }
    }
}

/* Compute mic1_cal for one ENR channel using multi-band averaging.
 * Formula from chip: sum(mic1_band[fidx..fidx+cnt-1]) * 10 // cnt,
 * then 四舍五入 (round half up). */
static int32_t enr_mic1_cal_avg(const bs300_calib_t *calib, uint8_t fidx, uint8_t cnt)
{
    int32_t s = 0;
    uint8_t j;
    for (j = 0; j < cnt; j++) {
        s += (int32_t)calib->mic1_band[fidx + j];
    }
    {
        int32_t t = s * 10 / (int32_t)cnt;
        return (t / 10) + ((t % 10) >= 5 ? 1 : 0);
    }
}

/* ================================================================
 *  WDRC Param Encoders (11 commands)
 *  Matches codegen.py Step 5 cross-validated implementations.
 * ================================================================ */

int bs300_encode_wdrc_general(const bs300_wdrc_t *wdrc, uint8_t *data)
{
    uint8_t nmbc;
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

int bs300_encode_wdrc_freq_spacing(const bs300_wdrc_t *wdrc, uint8_t *data)
{
    uint8_t mbc_ch[16];
    uint8_t nmbc;
    uint8_t i, wi;

    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);
    nmbc = wdrc->total_channels - wdrc->nsbc;

    /* MBC_CHx = (bin_count - 1), minimum 1. Estimate bin distribution. */
    {
        uint8_t bins_per_ch = (nmbc > 0) ? (uint8_t)(32 / nmbc) : 2;
        for (i = 0; i < 16; i++) {
            mbc_ch[i] = 1; /* NOBC default: 0b000001 */
        }
        for (i = 0; i < nmbc && i < 16; i++) {
            mbc_ch[i] = (bins_per_ch >= 2) ? (uint8_t)(bins_per_ch - 1) : 1;
        }
    }
    pack_uint6_4pw(data, 0, mbc_ch, 16);
    for (wi = (uint8_t)((16 + 3) / 4); wi < 16; wi++) {
        set_word(data, wi, 0x041041);
    }
    return 0;
}

int bs300_encode_wdrc_kp_threshold(const bs300_wdrc_t *wdrc,
                                    const bs300_calib_t *calib,
                                    uint8_t input_type, uint8_t *data)
{
    uint8_t values[32];
    uint8_t i, cnt;
    int32_t igd;

    if (wdrc == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);
    igd = get_input_gain_diff_tenth_db(input_type, calib);
    cnt = 0;

    for (i = 0; i < wdrc->total_channels && i < 16; i++) {
        uint8_t fidx = wdrc->freq_idx[i];
        int32_t mic1_cal;
        int32_t numer_tenth;

        /* 2-band calibration averaging per WDRC channel */
        if (fidx < 31) {
            mic1_cal = ((int32_t)calib->mic1_band[fidx]
                      + (int32_t)calib->mic1_band[fidx + 1]) / 2;
        } else {
            mic1_cal = (int32_t)calib->mic1_band[fidx];
        }
        mic1_cal += (int32_t)wdrc_kp_cal_offset[i];

        /* encode: int(60 + th - mic1_cal - igd/10.0) = trunc((600 + th*10 - mic1_cal*10 - igd) / 10) */
        numer_tenth = 600 + (int32_t)wdrc->kp1_th_db[i] * 10 - mic1_cal * 10;
        values[cnt++] = (uint8_t)clamp_s32(apply_igd_trunc(numer_tenth, igd), -128, 127);

        if (wdrc->kp_mode == 2) {
            numer_tenth = 600 + (int32_t)wdrc->kp2_th_db[i] * 10 - mic1_cal * 10;
            values[cnt++] = (uint8_t)clamp_s32(apply_igd_trunc(numer_tenth, igd), -128, 127);
        }
    }
    pack_bytes(data, 0, values, cnt);
    return 0;
}

int bs300_encode_wdrc_attack_time(const bs300_wdrc_t *wdrc, uint8_t *data)
{
    uint8_t i;

    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    if (wdrc->kp_mode == 1) {
        uint8_t values[32];
        uint8_t cnt = 0;
        for (i = 0; i < wdrc->total_channels && i < 16; i++) {
            values[cnt++] = wdrc->epd_at_idx[i];
            values[cnt++] = wdrc->kp1_at_idx[i];
        }
        pack_bytes(data, 0, values, cnt);
    } else {
        for (i = 0; i < wdrc->total_channels && i < 16; i++) {
            uint32_t w = (uint32_t)wdrc->epd_at_idx[i]
                  | ((uint32_t)wdrc->kp1_at_idx[i] << 8)
                  | ((uint32_t)wdrc->kp2_at_idx[i] << 16);
            set_word(data, i, w);
        }
    }
    return 0;
}

int bs300_encode_wdrc_release_time(const bs300_wdrc_t *wdrc, uint8_t *data)
{
    uint8_t i;

    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    if (wdrc->kp_mode == 1) {
        uint8_t values[32];
        uint8_t cnt = 0;
        for (i = 0; i < wdrc->total_channels && i < 16; i++) {
            values[cnt++] = wdrc->epd_rt_idx[i];
            values[cnt++] = wdrc->kp1_rt_idx[i];
        }
        pack_bytes(data, 0, values, cnt);
    } else {
        for (i = 0; i < wdrc->total_channels && i < 16; i++) {
            uint32_t w = (uint32_t)wdrc->epd_rt_idx[i]
                  | ((uint32_t)wdrc->kp1_rt_idx[i] << 8)
                  | ((uint32_t)wdrc->kp2_rt_idx[i] << 16);
            set_word(data, i, w);
        }
    }
    return 0;
}

int bs300_encode_wdrc_ratio(const bs300_wdrc_t *wdrc, uint8_t *data)
{
    uint8_t i;

    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    if (wdrc->kp_mode == 1) {
        uint8_t values[32];
        uint8_t cnt = 0;
        for (i = 0; i < wdrc->total_channels && i < 16; i++) {
            values[cnt++] = wdrc->epd_r_idx[i];
            values[cnt++] = wdrc->kp1_r_idx[i];
        }
        pack_bytes(data, 0, values, cnt);
    } else {
        for (i = 0; i < wdrc->total_channels && i < 16; i++) {
            uint32_t w = (uint32_t)wdrc->epd_r_idx[i]
                  | ((uint32_t)wdrc->kp1_r_idx[i] << 8)
                  | ((uint32_t)wdrc->kp2_r_idx[i] << 16);
            set_word(data, i, w);
        }
    }
    return 0;
}

static int8_t get_eq_gain_for_band(const bs300_modules_t *mod, uint8_t band)
{
    uint16_t hz;

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
                                uint8_t input_type, uint8_t *data)
{
    uint8_t values[32];
    uint8_t i;
    int32_t igd;
    int32_t vol_gain;

    if (wdrc == NULL || calib == NULL || mod == NULL || data == NULL) return -1;
    memset(data, 0, 48);
    igd = get_input_gain_diff_tenth_db(input_type, calib);
    vol_gain = ((int32_t)mod->volume_level - 9) * 3;

    for (i = 0; i < 32; i++) {
        int32_t gain_cal = (int32_t)calib->output_band[i] - (int32_t)calib->mic1_band[i];
        int32_t eq_gain = (int32_t)get_eq_gain_for_band(mod, i);
        int32_t baseline = (int32_t)wdrc->bin_gain[i] + vol_gain + eq_gain;
        int32_t numer_tenth = baseline * 10 - gain_cal * 10;
        values[i] = (uint8_t)clamp_s32(apply_igd_trunc(numer_tenth, -igd), -128, 127);
    }
    pack_bytes(data, 0, values, 32);
    return 0;
}

int bs300_encode_wdrc_lmt_threshold(const bs300_wdrc_t *wdrc,
                                     const bs300_calib_t *calib, uint8_t *data)
{
    uint8_t i;

    if (wdrc == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    /* Formula: int(60 + th - avg_output_cal_per_ch)
     * avg = (output_band[fidx] + output_band[fidx+1])/2 + cal_offset */
    for (i = 0; i < wdrc->total_channels && i < 16; i++) {
        uint8_t fidx = wdrc->freq_idx[i];
        int32_t out_cal;
        int32_t encoded;
        if (fidx < 31) {
            out_cal = ((int32_t)calib->output_band[fidx]
                     + (int32_t)calib->output_band[fidx + 1]) / 2;
        } else {
            out_cal = (int32_t)calib->output_band[fidx];
        }
        out_cal += (int32_t)wdrc_lmt_cal_offset[i];
        encoded = 60 + (int32_t)wdrc->lmt_th_db[i] - out_cal;
        data[i] = (uint8_t)clamp_s32(encoded, -128, 127);
    }
    return 0;
}

int bs300_encode_wdrc_lmt_attack(const bs300_wdrc_t *wdrc, uint8_t *data)
{
    uint8_t i;
    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);
    for (i = 0; i < wdrc->total_channels && i < 16; i++) {
        data[i] = wdrc->lmt_at_idx[i];
    }
    return 0;
}

int bs300_encode_wdrc_lmt_release(const bs300_wdrc_t *wdrc, uint8_t *data)
{
    uint8_t i;
    if (wdrc == NULL || data == NULL) return -1;
    memset(data, 0, 48);
    for (i = 0; i < wdrc->total_channels && i < 16; i++) {
        data[i] = wdrc->lmt_rt_idx[i];
    }
    return 0;
}

int bs300_encode_wdrc_lmt_ratio(const bs300_wdrc_t *wdrc, uint8_t *data)
{
    uint8_t i;
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

int bs300_encode_enr_general(const bs300_enr_t *enr, uint8_t *data)
{
    uint8_t ena, total_ch, sbc, mbc;
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

int bs300_encode_enr_freq_spacing(const bs300_enr_t *enr, uint8_t *data)
{
    uint8_t band_counts[16];

    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    enr_compute_band_counts(enr, band_counts);
    pack_uint6_4pw(data, 0, band_counts, 16);
    return 0;
}

int bs300_encode_enr_snr_threshold(const bs300_enr_t *enr, uint8_t *data)
{
    int16_t encoded[16];
    uint8_t i, total_ch;

    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    /* SNRT_CHx = floor(32/6.02 * value), matched by integer arithmetic:
     * int(v * 32.0 / 6.02) = v * 1600 / 301 (truncation toward zero) */
    for (i = 0; i < total_ch && i < 16; i++) {
        encoded[i] = (int16_t)clamp_s32((int32_t)enr->snr_th_db[i] * 1600 / 301,
                                    0, 4095);
    }
    for (i = total_ch; i < 16; i++) {
        encoded[i] = 0;
    }
    pack_int12_2pw(data, 0, encoded, 16);
    return 0;
}

int bs300_encode_enr_max_att(const bs300_enr_t *enr, uint8_t *data)
{
    int16_t encoded[16];
    uint8_t i, total_ch;

    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    /* MAR_CHx = floor((max_att / max(snr_th, 1)) * 256) */
    for (i = 0; i < total_ch && i < 16; i++) {
        int32_t st = (enr->snr_th_db[i] > 0) ? (int32_t)enr->snr_th_db[i] : 1;
        encoded[i] = (int16_t)clamp_s32((int32_t)enr->max_att_db[i] * 256 / st,
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
                               uint8_t input_type, uint8_t *data)
{
    int16_t encoded[16];
    uint8_t band_counts[16];
    uint8_t total_ch;
    int32_t igd;
    uint8_t i;

    if (enr == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    igd = get_input_gain_diff_tenth_db(input_type, calib);
    enr_compute_band_counts(enr, band_counts);

    /* NT_CHx = round(5.307 * (x + 130 - mic1Cal - input_gain) - 371.2)
     * mic1Cal = 四舍五入(sum(mic1_band[fidx..fidx+cnt-1]) / cnt) */
    for (i = 0; i < total_ch && i < 16; i++) {
        uint8_t fidx = enr->freq_idx[i];
        uint8_t cnt = band_counts[i];
        int32_t mic1_cal;
        int32_t val;

        if (cnt > 0 && fidx + cnt <= 32) {
            mic1_cal = enr_mic1_cal_avg(calib, fidx, cnt);
        } else {
            mic1_cal = (int32_t)calib->mic1_band[fidx < 32 ? fidx : 1];
        }

        /* Integer formula: val = round(5.307 * (nt + 130 - mic1_cal - igd/10.0) - 371.2)
         * matches codegen.py cross-validated output byte-for-byte. */
        val = enr_nt_int(enr->noise_th_db[i], mic1_cal, igd);
        encoded[i] = (int16_t)clamp_s32(val, -2048, 2047);
    }
    for (i = total_ch; i < 16; i++) {
        encoded[i] = 0;
    }
    pack_int12_2pw(data, 0, encoded, 16);
    return 0;
}

int bs300_encode_enr_upper_noise_th(const bs300_enr_t *enr,
                                     const bs300_calib_t *calib,
                                     uint8_t input_type, uint8_t *data)
{
    int16_t encoded[16];
    uint8_t band_counts[16];
    uint8_t total_ch;
    int32_t igd;
    uint8_t i;

    if (enr == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    igd = get_input_gain_diff_tenth_db(input_type, calib);
    enr_compute_band_counts(enr, band_counts);

    /* Same formula as Noise Threshold but uses upper_noise_th_db */
    for (i = 0; i < total_ch && i < 16; i++) {
        uint8_t fidx = enr->freq_idx[i];
        uint8_t cnt = band_counts[i];
        int32_t mic1_cal;
        int32_t val;

        if (cnt > 0 && fidx + cnt <= 32) {
            mic1_cal = enr_mic1_cal_avg(calib, fidx, cnt);
        } else {
            mic1_cal = (int32_t)calib->mic1_band[fidx < 32 ? fidx : 1];
        }

        val = enr_nt_int(enr->upper_noise_th_db[i], mic1_cal, igd);
        encoded[i] = (int16_t)clamp_s32(val, -2048, 2047);
    }
    for (i = total_ch; i < 16; i++) {
        encoded[i] = 0;
    }
    pack_int12_2pw(data, 0, encoded, 16);
    return 0;
}

int bs300_encode_enr_smoothing(const bs300_enr_t *enr, uint8_t *data)
{
    uint8_t i;

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
        uint8_t sf_vals[4];
        sf_vals[0] = enr->nhsf;
        sf_vals[1] = enr->nfsf;
        sf_vals[2] = enr->nnsf;
        sf_vals[3] = 4;  /* chip overrides snasf to 4 regardless of stored value */

        for (i = 0; i < 4; i++) {
            uint8_t val = sf_vals[i];
            uint32_t d1, d2;
            uint8_t w1, w2;
            if (val >= 2) {
                d1 = (uint32_t)1 << (23 - val);
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

int bs300_encode_enr_etr(const bs300_enr_t *enr, uint8_t *data)
{
    uint8_t i, total_ch;

    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    /* coded = (int64_t)2524971008 * (etr_x100 - 100) / (1600 * etr_x100 * ma)
     * 301 * 0x800000 = 2524971008, fits uint32_t. Product needs int64_t.
     * Verified byte-for-byte against codegen.py float output. */
    for (i = 0; i < total_ch && i < 16; i++) {
        int32_t etr_x100 = (int32_t)enr->etr_x100[i];
        int32_t ma = (int32_t)(enr->max_att_db[i] > 0 ? enr->max_att_db[i] : 1);
        int64_t num = 2524971008ULL * (int64_t)(etr_x100 - 100);
        int32_t den = 1600 * etr_x100 * ma;
        int32_t coded = (int32_t)(num / (int64_t)den);
        set_word(data, i, (uint32_t)(coded & 0xFFFFFF));
    }
    return 0;
}

int bs300_encode_enr_nrr(const bs300_enr_t *enr, uint8_t *data)
{
    uint8_t i, total_ch;

    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    /* coded = (int64_t)2524970707 * nrr_x10 / (16000 * ma)
     * 301 * 0x7FFFFF = 2524970707, fits uint32_t. Product needs int64_t.
     * Verified byte-for-byte against codegen.py float output. */
    for (i = 0; i < total_ch && i < 16; i++) {
        int32_t nrr_x10 = (int32_t)enr->nrr_x10[i];
        int32_t ma = (int32_t)(enr->max_att_db[i] > 0 ? enr->max_att_db[i] : 1);
        int64_t num = 2524970707ULL * (int64_t)nrr_x10;
        int32_t den = 16000 * ma;
        int32_t coded = (int32_t)(num / (int64_t)den);
        set_word(data, i, (uint32_t)(coded & 0xFFFFFF));
    }
    return 0;
}

int bs300_encode_enr_sasf(const bs300_enr_t *enr, uint8_t *data)
{
    uint8_t i, total_ch;

    if (enr == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    total_ch = enr->enable_num_ch & 0x3F;
    /* SASF: if sv >= 2, data1 = 1 << (23 - sv), else data1 = 0x7FFFFF.
     * Only data1 is sent per codegen (data2 is computed but not stored). */
    for (i = 0; i < total_ch && i < 16; i++) {
        uint8_t sv = enr->sasf[i];
        uint32_t d1;
        if (sv >= 2) {
            d1 = (uint32_t)1 << (23 - sv);
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
                              const bs300_calib_t *calib, uint8_t *data)
{
    uint16_t beep_hz, batt_hz;
    uint8_t beep_band, batt_band;
    int32_t outcal_beep, outcal_batt;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    /* Beep level: frac24 = 0x7FFFFF * 10^((beep_level - outCal) / 20)
     * Use lookup table: index = (beep_level - outcal_beep) + 255 */
    beep_hz = (mod->beep_freq_idx < 25)
              ? beep_idx_to_hz[mod->beep_freq_idx] : 1000;
    beep_band = freq_to_cal_band(beep_hz);
    outcal_beep = (int32_t)calib->output_band[beep_band];
    {
        int32_t x = (int32_t)mod->beep_level - outcal_beep;
        int32_t idx = x + 255;
        if (idx < 0) idx = 0;
        if (idx >= BS300_BEEP_TABLE_SIZE) idx = BS300_BEEP_TABLE_SIZE - 1;
        set_word(data, 0, bs300_beep_frac24_table[idx]);
    }

    set_word(data, 1, beep_hz);
    set_word(data, 2, db_to_int24((int32_t)mod->min_vol * 10));
    set_word(data, 3, db_to_int24((int32_t)mod->max_vol * 10));
    /* Translate struct encoding → protocol encoding for input_selection:
     * struct: 0=FrontMic 1=RearMic 2=Telecoil 3=DAI 4=MM+ 5=DDM2 6=DualMic
     * proto:  0=FrontMic 1=Telecoil 2=DAI 3=RearMic 4=DDM2 5=MM+Tel 6=MM+DAI */
    {
        static const uint8_t s_input_struct_to_proto[7] =
            { 0, 3, 1, 2, 5, 4, 6 };
        uint8_t sel = mod->input_selection;
        if (sel < 7) sel = s_input_struct_to_proto[sel];
        set_word(data, 4, sel);
    }

    /* Battery flat beep (same lookup table logic) */
    batt_hz = (mod->batt_beep_freq_idx < 25)
              ? beep_idx_to_hz[mod->batt_beep_freq_idx] : 1000;
    batt_band = freq_to_cal_band(batt_hz);
    set_word(data, 5, batt_hz);
    outcal_batt = (int32_t)calib->output_band[batt_band];
    {
        int32_t x = (int32_t)mod->batt_beep_level - outcal_batt;
        int32_t idx = x + 255;
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
                       const bs300_calib_t *calib, uint8_t *data)
{
    uint32_t delay_n;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    set_word(data, 0, mod->dfbc_enable_mode & 0x0F);
    /* delay_n = round(fbc_bulk_delay_us / 62.5) = round(bd * 10 / 625) */
    delay_n = (uint32_t)(((uint32_t)calib->fbc_bulk_delay * 10 + 312) / 625);
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
                      uint8_t input_type, uint8_t *data)
{
    int32_t all_mic1;
    int32_t mic1_cal;
    int32_t igd;
    uint8_t i;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    set_word(data, 0, mod->iss_enable ? 1 : 0);

    if (!mod->iss_enable) return 0;

    /* mic1_cal = round(sum of all 32 mic1 bands / 32) */
    all_mic1 = 0;
    for (i = 0; i < 32; i++) {
        all_mic1 += (int32_t)calib->mic1_band[i];
    }
    mic1_cal = (all_mic1 + 16) / 32;

    igd = get_input_gain_diff_tenth_db(input_type, calib);

    /* ISS frac48 = round(1/10^exponent * 2^47), exponent = (-3-thr+mic1_cal+igd_dB)/10.
     * Table covers exponent_tenth = [0, 100], BS300_ISS_TABLE_OFFSET=0. */
    {
        int32_t exp_tenth_numer = (-30 - (int32_t)mod->iss_threshold * 10
                                + mic1_cal * 10 + igd);
        int32_t exp_tenth = round_div(exp_tenth_numer, 10);
        int32_t idx = exp_tenth - BS300_ISS_TABLE_OFFSET;
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
                            const bs300_calib_t *calib, uint8_t *data)
{
    int32_t all_mic1, avg_ceil;
    uint8_t ssp;
    uint8_t i;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    /* word 0: selection (bit0=enable, bit1=dual_mic) */
    set_word(data, 0, mod->wnr_enable_dual & 0x03);

    /* word 1: detection level = round((75 - ceil(avg_mic1)) * (65536/6.02/8))
     * Integer: round((75 - avg_ceil) * 409600 / 301) */
    all_mic1 = 0;
    for (i = 0; i < 32; i++) {
        all_mic1 += (int32_t)calib->mic1_band[i];
    }
    avg_ceil = (all_mic1 + 31) / 32; /* ceil */
    {
        int32_t diff = 75 - (int32_t)avg_ceil;
        int32_t detect_val = round_div(diff * 409600, 301);
        set_word(data, 1, (uint32_t)(detect_val & 0xFFFFFF));
    }

    /* word 2: mic2_cal = 0x800000 * 10^(mic2_gain_diff_tenth_db / 200) */
    {
        int32_t idx = (int32_t)calib->mic2_gain_diff + BS300_MIC2_CAL_TABLE_OFFSET;
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
                                uint8_t input_type, uint8_t *data)
{
    int32_t igd;
    uint8_t ssp;
    uint8_t i;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    igd = get_input_gain_diff_tenth_db(input_type, calib);
    ssp = 0;  /* chip uses SSP level 0 for band data offsets */

    /* band_N_data = 0x2A9764 - db_to_frac24((mic1 + igd/10) * 2 - offset)
     * n_tenth_db = mic1*20 + igd*2 - offset*10 (exact integer) */
    for (i = 0; i < 16; i++) {
        int32_t offset = (int32_t)wnr_ssp_offset[i][ssp];
        int32_t n_tenth = (int32_t)calib->mic1_band[i] * 20 + igd * 2 - offset * 10;
        uint32_t frac = db_to_frac24(n_tenth);
        uint32_t result = 0x2A9764 - frac;
        set_word(data, i, result & 0xFFFFFF);
    }
    return 0;
}

int bs300_encode_wnr_band_16_31(const bs300_modules_t *mod,
                                 const bs300_calib_t *calib,
                                 uint8_t input_type, uint8_t *data)
{
    int32_t igd;
    uint8_t ssp;
    uint8_t i;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    igd = get_input_gain_diff_tenth_db(input_type, calib);
    ssp = 0;  /* chip uses SSP level 0 for band data offsets */

    for (i = 0; i < 16; i++) {
        uint8_t band = 16 + i;
        int32_t offset = (int32_t)wnr_ssp_offset[band][ssp];
        int32_t n_tenth = (int32_t)calib->mic1_band[band] * 20 + igd * 2 - offset * 10;
        uint32_t frac = db_to_frac24(n_tenth);
        uint32_t result = 0x2A9764 - frac;
        set_word(data, i, result & 0xFFFFFF);
    }
    return 0;
}

int bs300_encode_wnr_single_mic(const bs300_modules_t *mod,
                                 const bs300_calib_t *calib,
                                 uint8_t input_type, uint8_t *data)
{
    int32_t igd;
    uint8_t ssp;
    uint8_t i;

    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    igd = get_input_gain_diff_tenth_db(input_type, calib);
    ssp = 0;  /* chip uses SSP level 0 for band data offsets */

    /* Single mic detection: bands 0-2, data2 formula:
     * data2 = 3292041 - db_to_frac24((mic1 + igd/10) * 2 - offset) */
    for (i = 0; i < 3; i++) {
        int32_t offset = (int32_t)wnr_data2_offset[i][ssp];
        int32_t n_tenth = (int32_t)calib->mic1_band[i] * 20 + igd * 2 - offset * 10;
        uint32_t frac = db_to_frac24(n_tenth);
        uint32_t result = 3292041 - frac;
        set_word(data, i, result & 0xFFFFFF);
    }
    return 0;
}

/* ================================================================
 *  AGCO Param Encoder
 *  Matches codegen.py encode_agco_param.
 * ================================================================ */

int bs300_encode_agco(const bs300_modules_t *mod, uint8_t *data)
{
    if (mod == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    set_word(data, 0, mod->agco_enable ? 1 : 0);

    if (!mod->agco_enable) return 0;

    /* Threshold: 0xFA0000 - ceil(|th_dB| * 65536 / 6.02)
     * db_to_frac24 expects tenth-dB: th_tenth_dB = |th_dB| * 10 */
    {
        int32_t abs_th = (int32_t)mod->agco_threshold_db;
        if (abs_th < 0) abs_th = -abs_th;
        set_word(data, 1, (uint32_t)(0xFA0000U - db_to_frac24(abs_th * 10)));
    }

    /* Attack: (1 - exp(-10/atk_01ms)) * 0x7FFFFF, lookup table */
    {
        uint16_t atk = mod->agco_attack_01ms;
        if (atk >= BS300_AGCO_EXP_TABLE_SIZE) atk = BS300_AGCO_EXP_TABLE_SIZE - 1;
        set_word(data, 2, bs300_agco_exp_table[atk]);
    }

    /* Release: same formula */
    {
        uint16_t rel = mod->agco_release_01ms;
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
                          uint8_t input_type, uint8_t *data)
{
    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    set_word(data, 0, mod->mm_plus_enable ? 1 : 0);

    if (!mod->mm_plus_enable) return 0;

    {
        int32_t igd;

        /* mm_type: 0x00=Telecoil, 0x01=DAI (per handbook §MM Plus) */
        if (mod->mm_type == 0x00)
            igd = (int32_t)calib->telecoil_gain_diff;
        else if (mod->mm_type == 0x01)
            igd = (int32_t)calib->dai_gain_diff;
        else
            igd = 0;

        {
            int32_t x = (int32_t)mod->mix_ratio * 10 - igd;
            int32_t idx = x + BS300_MM_PLUS_TABLE_OFFSET;
            if (idx < 0) idx = 0;
            if (idx >= BS300_MM_PLUS_TABLE_SIZE) idx = BS300_MM_PLUS_TABLE_SIZE - 1;
            set_word(data, 1, bs300_mm_plus_frac24_table[idx]);
        }
    }
    return 0;
}

/* Omni threshold frac48 lookup: index = (avg_mic1 - omni_mt) diff */
#define BS300_OMNI_TABLE_SIZE  121
static const uint32_t bs300_omni_frac48_l[BS300_OMNI_TABLE_SIZE] = {
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
                       const bs300_calib_t *calib, uint8_t *data)
{
    if (mod == NULL || calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    set_word(data, 0, mod->ddm2_enable ? 1 : 0);
    if (!mod->ddm2_enable) return 0;

    /* polar_pattern uint3 → frac24 */
    {
        static const uint32_t polar_frac24[8] = {
            0x000000,  /* 0: Bi-directional    */
            0x200000,  /* 1: Hyper-cardioid    */
            0x300000,  /* 2: Super-cardioid    */
            0x400000,  /* 3: Cardioid           */
            0x7FFFFF,  /* 4: Omni-directional  */
            0x000000,  /* 5: (unused)          */
            0x000000,  /* 6: (unused)          */
            0x000000,  /* 7: (unused)          */
        };
        uint8_t idx = mod->polar_pattern & 0x07;
        uint32_t pval = polar_frac24[idx];
        if (mod->adm_fdm) { pval = 0x7FFFFF; }  /* ADM → Omni */
        set_word(data, 2, pval);
    }

    set_word(data, 1, mod->open_ear);
    set_word(data, 3, mod->adm_fdm);

    /* mic2_dly_data = mic_delay_raw * 8388607 / 1250 (truncation toward zero)
     * 0.008 * 0.1 * 0x7FFFFF = 8388607/1250 ≈ 6710.8856 */
    {
        int64_t num = (int64_t)calib->mic_delay * 8388607ULL;
        int32_t dly_val = (int32_t)(num / 1250ULL);
        set_word(data, 4, (uint32_t)(dly_val & 0xFFFFFF));
    }

    /* mic2_cal_data = 0.5 * 0x7FFFFF * 10^(mic2_gain_diff_tenth_db / 200)
     * = 4194303 * 10^(x/200). Use mic2_cal table / 2. */
    {
        int32_t idx = (int32_t)calib->mic2_gain_diff + BS300_MIC2_CAL_TABLE_OFFSET;
        if (idx < 0) idx = 0;
        if (idx >= BS300_MIC2_CAL_TABLE_SIZE) idx = BS300_MIC2_CAL_TABLE_SIZE - 1;
        set_word(data, 5, (bs300_mic2_cal_frac24_table[idx] / 2) & 0xFFFFFF);
    }

    /* Omni threshold (frac48): table indexed by diff = avg_mic1 - omni_mt
     * omni_mt = (omni_threshold_I2C - 2) / 4 + 40, avg_mic1 = sum(mic1_band[1:31])/31 */
    if (mod->omni_threshold >= 40 && mod->omni_threshold <= 100) {
        int32_t sum_mic1 = 0;
        int32_t diff_tenth;      /* (avg_mic1 - omni_mt) * 10              */
        int32_t idx;
        int32_t omni_mt;
        for (uint8_t i = 1; i < 32; i++) {
            sum_mic1 += (int32_t)calib->mic1_band[i];
        }
        diff_tenth = (sum_mic1 * 10) / 31;        /* avg_mic1 × 10     */
        omni_mt    = ((int32_t)mod->omni_threshold - 2) / 4 * 10 + 400;
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
                         uint8_t input_type, uint8_t *data)
{
    int32_t gain_raw;

    if (calib == NULL || data == NULL) return -1;
    memset(data, 0, 48);

    if (input_type == INPUT_TYPE_TELECOIL) {
        gain_raw = (int32_t)calib->telecoil_gain_diff;
    } else if (input_type == INPUT_TYPE_DAI) {
        gain_raw = (int32_t)calib->dai_gain_diff;
    } else {
        return 0;
    }

    /* data = (gain_dB * 2) * (65536 / 6.02)
     * gain_dB = gain_raw / 10.0 → data = gain_raw * 655360 / 301 */
    {
        int32_t val = gain_raw * 655360 / 301;
        set_word(data, 0, (uint32_t)(val & 0xFFFFFF));
    }
    return 0;
}

/* ================================================================
 *  Input Tone Generator (0x8001E2) — see bs300_ram_sync.c for write/clear
 *  bs300_encode_itg() declared in bs300_param_encode.h
 ================================================================ */