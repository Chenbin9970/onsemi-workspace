#ifndef BS300_CALIB_H
#define BS300_CALIB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  mic1_band[32];       /* output_cal[x] - gain_cal[x], band 0 invalid */
    uint8_t  output_band[32];     /* output_cal[x], band 0 invalid */
    int16_t  mic2_gain_diff;      /* 0.1 dB LSB, range [-5.0, 5.0] dB */
    uint16_t mic_delay;           /* 0.1 us LSB */
    int16_t  telecoil_gain_diff;  /* 0.1 dB LSB, range [-50.0, 50.0] dB */
    int16_t  dai_gain_diff;       /* 0.1 dB LSB */
    uint16_t fbc_bulk_delay;      /* 1 us LSB */
} bs300_calib_t;

int bs300_parse_calibration(const uint8_t *raw, bs300_calib_t *out);

#ifdef __cplusplus
}
#endif

#endif /* BS300_CALIB_H */
