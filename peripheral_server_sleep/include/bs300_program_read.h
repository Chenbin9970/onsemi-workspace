#ifndef BS300_PROGRAM_READ_H
#define BS300_PROGRAM_READ_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BS300_PKT_SIZE          48
#define BS300_PKT_COUNT         10
#define BS300_TOTAL_DATA        (BS300_PKT_SIZE * BS300_PKT_COUNT)  /* 480 */

#define BS300_WDRC_MAX_CHANNELS 16
#define BS300_WDRC_BANDS        32
#define BS300_ENR_MAX_CHANNELS  16

/* --- Decoded program data structures --- */

typedef struct {
    uint8_t frequency_idx;
    uint8_t epd_at;
    uint8_t epd_rt;
    uint8_t epd_r;
    uint8_t kp1_th;
    uint8_t kp2_th;
    uint8_t kp1_at;
    uint8_t kp2_at;
    uint8_t kp1_rt;
    uint8_t kp2_rt;
    uint8_t kp1_r;
    uint8_t kp2_r;
    uint8_t lmt_th;
    uint8_t lmt_at;
    uint8_t lmt_rt;
    uint8_t lmt_r;
} bs300_wdrc_channel_t;

typedef struct {
    uint8_t  kneepoints_per_channel;
    uint8_t  output_limiting_sel;
    uint8_t  num_channels;
    uint8_t  bin_gain[BS300_WDRC_BANDS];
    bs300_wdrc_channel_t channels[BS300_WDRC_MAX_CHANNELS];
} bs300_wdrc_parsed_t;

typedef struct {
    uint8_t  beep_level;
    uint16_t beep_frequency;
    int8_t   min_volume;
    int8_t   max_volume;
    uint8_t  battery_flat_beep_level;
    uint16_t battery_flat_beep_frequency;
} bs300_volume_t;

typedef struct {
    char     input_type[12];
    uint8_t  mic_mixing_ratio;
    uint8_t  mm_type;
    uint8_t  omni_threshold;
    uint8_t  mode;
    uint32_t cutoff_frequency;
} bs300_inputs_t;

typedef struct {
    uint8_t dfbc_mode;
} bs300_dfbc_t;

typedef struct {
    uint8_t  frequency_idx;
    uint8_t  ma;
    uint8_t  snrth;
    uint8_t  nt;
    uint8_t  unt;
    uint8_t  etr;
    uint8_t  nrr;
} bs300_enr_channel_t;

typedef struct {
    uint8_t  nfsf;
    uint8_t  nhsf;
    uint8_t  nnsf;
    uint8_t  num_channels;
    uint8_t  snasf;
    bs300_enr_channel_t channels[BS300_ENR_MAX_CHANNELS];
} bs300_enr_parsed_t;

typedef struct {
    uint8_t iss_threshold;
} bs300_iss_t;

typedef struct {
    uint8_t dual_mic_mode_sel;
    uint8_t suppression_strength_preset;
} bs300_wnr_t;

typedef struct {
    uint16_t attack_time;
    uint16_t release_time;
    uint8_t  threshold;
} bs300_agco_t;

typedef struct {
    bs300_wdrc_parsed_t   wdrc;
    bs300_volume_t volume;
    bs300_inputs_t inputs;
    bool           has_dfbc;
    bs300_dfbc_t   dfbc;
    bool           has_enr;
    bs300_enr_parsed_t    enr;
    bool           has_iss;
    bs300_iss_t    iss;
    bool           has_wnr;
    bs300_wnr_t    wnr;
    bool           has_agco;
    bs300_agco_t   agco;
} bs300_program_data_t;

/* --- API --- */

/* Read 528 bytes of raw program burn data from BS300 via I2C.
 * Returns true on success, false on I2C error or timeout. */
bool bs300_program_read(uint8_t program_index, uint8_t *data_out);

/* Parse 528-byte raw data into decoded program struct.
 * Returns true on success. */
bool bs300_program_parse(const uint8_t *raw, bs300_program_data_t *prog);

/* Read + parse in one call.
 * Returns true on success. */
bool bs300_program_read_and_parse(uint8_t program_index,
                                  bs300_program_data_t *prog);

#ifdef __cplusplus
}
#endif

#endif /* BS300_PROGRAM_READ_H */
