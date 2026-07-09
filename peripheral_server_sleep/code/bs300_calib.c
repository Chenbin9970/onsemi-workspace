#include "bs300_calib.h"
#include "app.h"
#include <string.h>

int bs300_parse_calibration(const uint8_t *raw, bs300_calib_t *out)
{
    uint8_t i;

    if (raw == NULL || out == NULL) {
        return -1;
    }

    if (raw[0] != 3 || raw[2] != 9) {
        PRINTF("[BS300] calib invalid header: [0]=%d [2]=%d\r\n",
               raw[0], raw[2]);
        return -1;
    }

    if (raw[20] != 0x02 || raw[21] != 0xFA || raw[22] != 0x00) {
        PRINTF("[BS300] calib mic1 header mismatch\r\n");
        return -1;
    }
    for (i = 0; i < 32; i++) {
        out->mic1_band[i] = raw[23 + i];
    }

    if (raw[55] != 0x01 || raw[56] != 0xFA || raw[57] != 0x00) {
        PRINTF("[BS300] calib output header mismatch\r\n");
        return -1;
    }
    for (i = 0; i < 32; i++) {
        out->output_band[i] = raw[58 + i];
    }

    out->mic2_gain_diff     = (int16_t)((uint16_t)raw[91] | ((uint16_t)raw[92] << 8));
    out->mic_delay          = (uint16_t)((uint16_t)raw[94] | ((uint16_t)raw[95] << 8));
    out->telecoil_gain_diff = (int16_t)((uint16_t)raw[97] | ((uint16_t)raw[98] << 8));
    out->dai_gain_diff      = (int16_t)((uint16_t)raw[100] | ((uint16_t)raw[101] << 8));
    out->fbc_bulk_delay     = (uint16_t)((uint16_t)raw[103] | ((uint16_t)raw[104] << 8));

    PRINTF("[BS300] calib parsed: mic1[1]=%d out[1]=%d tc_gd=%d dai_gd=%d fbc_bd=%d\r\n",
           out->mic1_band[1], out->output_band[1],
           out->telecoil_gain_diff, out->dai_gain_diff,
           out->fbc_bulk_delay);
    return 0;
}
