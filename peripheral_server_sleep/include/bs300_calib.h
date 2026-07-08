#ifndef BS300_CALIB_H
#define BS300_CALIB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t  mic1_band[32];
    int16_t  output_band[32];
    int16_t  mic2_gain_diff;
    uint16_t mic_delay;
    int16_t  telecoil_gain_diff;
    int16_t  dai_gain_diff;
    uint16_t fbc_bulk_delay;
    int16_t  digital_audio_sensitivity;
} bs300_calib_data_t;

/* Parse 144-byte raw calibration data (3 packets × 48B concatenated). */
bool bs300_calib_parse(const uint8_t raw[144], bs300_calib_data_t *calib);

/* Average mic1_cal = output_cal - gain_cal over bands 1-31 (skip band 0).
 * Returns value × 10 (0.1 dB units). */
int16_t bs300_calib_avg_mic1(const bs300_calib_data_t *calib);

/* Average output_cal over bands 1-31 (skip band 0).
 * Returns value × 10 (0.1 dB units). */
int16_t bs300_calib_avg_output(const bs300_calib_data_t *calib);

/* Input gain diff in 0.1 dB units for telecoil/dai; 0 for mic input. */
int16_t bs300_calib_input_gain_diff_tenth_db(const bs300_calib_data_t *calib,
                                              const char *input_type);

#ifdef __cplusplus
}
#endif

#endif /* BS300_CALIB_H */
