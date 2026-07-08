#ifndef BS300_PARAM_ENCODE_H
#define BS300_PARAM_ENCODE_H

#include <stdint.h>
#include <stdbool.h>
#include "bs300_program_read.h"
#include "bs300_calib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Each encode function produces a 48-byte payload for bs300_advanced_write().
 * The caller provides a data_out[48] buffer. */

/* --- WDRC (11 commands) --- */
void bs300_enc_wdrc_general(uint8_t total_channels, bool limiter,
                             uint8_t data_out[48]);
void bs300_enc_wdrc_freq_spacing(const uint8_t freq_idx[16],
                                  uint8_t num_channels, uint8_t data_out[48]);
void bs300_enc_wdrc_kp_threshold(const bs300_wdrc_t *wdrc,
                                  const bs300_calib_data_t *calib,
                                  const char *input_type, uint8_t data_out[48]);
void bs300_enc_wdrc_attack_time(const bs300_wdrc_t *wdrc,
                                 uint8_t data_out[48]);
void bs300_enc_wdrc_release_time(const bs300_wdrc_t *wdrc,
                                  uint8_t data_out[48]);
void bs300_enc_wdrc_ratio(const bs300_wdrc_t *wdrc,
                           uint8_t data_out[48]);
void bs300_enc_wdrc_bin_gain(const bs300_wdrc_t *wdrc,
                              const bs300_calib_data_t *calib,
                              const char *input_type, uint8_t data_out[48]);
void bs300_enc_wdrc_lmt_threshold(const bs300_wdrc_t *wdrc,
                                   const bs300_calib_data_t *calib,
                                   uint8_t data_out[48]);
void bs300_enc_wdrc_lmt_attack(const bs300_wdrc_t *wdrc,
                                uint8_t data_out[48]);
void bs300_enc_wdrc_lmt_release(const bs300_wdrc_t *wdrc,
                                 uint8_t data_out[48]);
void bs300_enc_wdrc_lmt_ratio(const bs300_wdrc_t *wdrc,
                               uint8_t data_out[48]);

/* --- Volume/Beep/Input (1 command) --- */
void bs300_enc_volume_beep(const bs300_volume_t *vol, uint8_t input_selection,
                            const bs300_calib_data_t *calib,
                            uint8_t data_out[48]);

/* --- DFBC (1 command) --- */
void bs300_enc_dfbc(uint8_t mode, const bs300_calib_data_t *calib,
                     uint8_t data_out[48]);

/* --- ISS (1 command) --- */
void bs300_enc_iss(uint8_t threshold_dbspl, const bs300_calib_data_t *calib,
                    const char *input_type, uint8_t data_out[48]);

/* --- WNR (4 commands) --- */
void bs300_enc_wnr_general(const bs300_calib_data_t *calib,
                            uint8_t suppression_preset, uint8_t data_out[48]);
void bs300_enc_wnr_bands_0_15(const bs300_calib_data_t *calib,
                               const char *input_type, uint8_t data_out[48]);
void bs300_enc_wnr_bands_16_31(const bs300_calib_data_t *calib,
                                const char *input_type, uint8_t data_out[48]);
void bs300_enc_wnr_single_mic(const bs300_calib_data_t *calib,
                               const char *input_type, uint8_t data_out[48]);

/* --- ENR (8 commands, excluding SASF 0x8090C2) --- */
void bs300_enc_enr_general(uint8_t num_channels, uint8_t data_out[48]);
void bs300_enc_enr_freq_spacing(const uint8_t freq_idx[16],
                                 uint8_t num_channels, uint8_t data_out[48]);
void bs300_enc_enr_snr_threshold(const bs300_enr_t *enr,
                                  uint8_t data_out[48]);
void bs300_enc_enr_max_att(const bs300_enr_t *enr,
                            uint8_t data_out[48]);
void bs300_enc_enr_noise_th(const bs300_enr_t *enr,
                             const bs300_calib_data_t *calib,
                             const char *input_type, uint8_t data_out[48]);
void bs300_enc_enr_upper_noise_th(const bs300_enr_t *enr,
                                   const bs300_calib_data_t *calib,
                                   const char *input_type, uint8_t data_out[48]);
void bs300_enc_enr_smoothing(const bs300_enr_t *enr,
                              uint8_t data_out[48]);
void bs300_enc_enr_etr(const bs300_enr_t *enr,
                        uint8_t data_out[48]);
void bs300_enc_enr_nrr(const bs300_enr_t *enr,
                        uint8_t data_out[48]);

/* --- AGCO (1 command) --- */
void bs300_enc_agco(uint8_t threshold_db, uint16_t attack_01ms,
                     uint16_t release_01ms, uint8_t data_out[48]);

/* --- DDM2 / MM+ / TC-DAI (3 commands) --- */
void bs300_enc_ddm2_disabled(uint8_t data_out[48]);
void bs300_enc_mm_plus_disabled(uint8_t data_out[48]);
void bs300_enc_tc_dai_gain_diff(const bs300_calib_data_t *calib,
                                 uint8_t data_out[48]);

#ifdef __cplusplus
}
#endif

#endif /* BS300_PARAM_ENCODE_H */
