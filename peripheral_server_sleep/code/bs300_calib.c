#include "bs300_calib.h"
#include <string.h>

/* Module info table: (index, length_bytes, is_unsigned) */
static const uint8_t calib_module_index[8] = { 0x01, 0x03, 0x02, 0x04,
                                                0x05, 0x06, 0x07, 0x08 };
static const uint8_t calib_module_len[8]   = { 35, 35, 3, 3, 3, 3, 3, 3 };

bool bs300_calib_parse(const uint8_t raw[144], bs300_calib_data_t *calib)
{
    uint8_t i;

    if (!raw || !calib) return false;

    /* Validate header */
    if (raw[0] != 3 || raw[2] != 9) return false;

    /* Validate module info section (bytes 3-19) */
    for (i = 0; i < 8; i++) {
        if (raw[3 + i * 2] != calib_module_index[i]) return false;
        if (raw[3 + i * 2 + 1] != calib_module_len[i]) return false;
    }
    if (raw[19] != 0xFB) return false;

    /* Parse Mic1 calibration (bytes 20-54):
     * header 0x02 0xFA 0x00, then 32 uint8 values */
    if (raw[20] != 0x02 || raw[21] != 0xFA || raw[22] != 0x00) return false;
    for (i = 0; i < 32; i++)
        calib->mic1_band[i] = (int16_t)raw[23 + i];

    /* Parse Output calibration (bytes 55-89):
     * header 0x01 0xFA 0x00, then 32 uint8 values */
    if (raw[55] != 0x01 || raw[56] != 0xFA || raw[57] != 0x00) return false;
    for (i = 0; i < 32; i++)
        calib->output_band[i] = (int16_t)raw[58 + i];

    /* Parse 6 short modules (bytes 90-107):
     * each: header 0x40 + int16/uint16 little-endian */
    for (i = 0; i < 6; i++) {
        uint8_t off = 90 + i * 3;
        if (raw[off] != 0x40) return false;
    }

    calib->mic2_gain_diff     = (int16_t)(raw[91] | ((uint16_t)raw[92] << 8));
    calib->mic_delay          = (uint16_t)(raw[94] | ((uint16_t)raw[95] << 8));
    calib->telecoil_gain_diff = (int16_t)(raw[97] | ((uint16_t)raw[98] << 8));
    calib->dai_gain_diff      = (int16_t)(raw[100] | ((uint16_t)raw[101] << 8));
    calib->fbc_bulk_delay     = (uint16_t)(raw[103] | ((uint16_t)raw[104] << 8));
    calib->digital_audio_sensitivity =
        (int16_t)(raw[106] | ((uint16_t)raw[107] << 8));

    /* Verify zero-padding (bytes 108-143) */
    for (i = 108; i < 144; i++) {
        if (raw[i] != 0) return false;
    }

    return true;
}

int16_t bs300_calib_avg_mic1(const bs300_calib_data_t *calib)
{
    int32_t sum = 0;
    uint8_t i;
    if (!calib) return 0;
    for (i = 1; i < 32; i++)
        sum += calib->mic1_band[i];
    /* Round to nearest integer: (sum * 10 + 155) / 310
     * Actually return value × 10 for precision, caller can use as needed.
     * For the integer average: (sum + 15) / 31 (round half-up) */
    return (int16_t)((sum + 15) / 31);
}

int16_t bs300_calib_avg_output(const bs300_calib_data_t *calib)
{
    int32_t sum = 0;
    uint8_t i;
    if (!calib) return 0;
    for (i = 1; i < 32; i++)
        sum += calib->output_band[i];
    return (int16_t)((sum + 15) / 31);
}

int16_t bs300_calib_input_gain_diff_tenth_db(const bs300_calib_data_t *calib,
                                              const char *input_type)
{
    if (!calib || !input_type) return 0;
    if (strncmp(input_type, "telecoil", 8) == 0)
        return calib->telecoil_gain_diff;
    if (strncmp(input_type, "dai", 3) == 0)
        return calib->dai_gain_diff;
    return 0;
}
