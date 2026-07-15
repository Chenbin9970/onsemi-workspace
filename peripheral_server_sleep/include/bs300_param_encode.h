#ifndef BS300_PARAM_ENCODE_H
#define BS300_PARAM_ENCODE_H

#include <stdint.h>
#include "bs300_calib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  Struct storage sizes
 * ================================================================ */
#define BS300_WDRC_STRUCT_SIZE       292
#define BS300_ENR_STRUCT_SIZE        134
#define BS300_MODULES_STRUCT_SIZE    64
#define BS300_STRUCT_TOTAL_SIZE      490

/* ================================================================
 *  WDRC structured data (292 bytes)
 * ================================================================ */
typedef struct {
    uint8_t  total_channels;        /* [1, 16] */
    uint8_t  nsbc;                  /* single-band channel count */
    uint8_t  kp_mode;               /* 1=1KP, 2=2KP */
    uint8_t  limiter;               /* 0=off, 1=on */
    uint8_t  freq_idx[16];          /* frequency table index per channel */
    int8_t   kp1_th_db[16];         /* KP1 threshold, value_in_MT */
    int8_t   kp2_th_db[16];         /* KP2 threshold */
    uint8_t  epd_at_idx[16];        /* Expander Attack table index */
    uint8_t  epd_rt_idx[16];        /* Expander Release */
    uint8_t  epd_r_idx[16];         /* Expander Ratio */
    uint8_t  kp1_at_idx[16];        /* KP1 Attack */
    uint8_t  kp2_at_idx[16];        /* KP2 Attack */
    uint8_t  kp1_rt_idx[16];        /* KP1 Release */
    uint8_t  kp2_rt_idx[16];        /* KP2 Release */
    uint8_t  kp1_r_idx[16];         /* KP1 Ratio */
    uint8_t  kp2_r_idx[16];         /* KP2 Ratio */
    int8_t   lmt_th_db[16];         /* Limiter threshold (value_in_MT) */
    uint8_t  lmt_at_idx[16];        /* Limiter Attack */
    uint8_t  lmt_rt_idx[16];        /* Limiter Release */
    uint8_t  lmt_r_idx[16];         /* Limiter Ratio */
    int8_t   bin_gain[32];          /* 32 band gain, value_in_MT */
} bs300_wdrc_t;

/* ================================================================
 *  ENR structured data (134 bytes)
 * ================================================================ */
typedef struct {
    uint8_t  enable_num_ch;         /* bit7=enable, bit[3:0]=channel_count */
    uint8_t  nfsf;                  /* noise falling sf [1,16] */
    uint8_t  nhsf;                  /* noise hold sf [1,16] */
    uint8_t  nnsf;                  /* noise normal sf [1,16] */
    uint8_t  snasf;                 /* speech non-adap sf [1,16] */
    uint8_t  freq_idx[16];          /* frequency table index per channel */
    uint8_t  snr_th_db[16];         /* [4, 30] dB */
    uint8_t  max_att_db[16];        /* [0, 30] dB */
    uint8_t  noise_th_db[16];       /* dB SPL */
    uint8_t  upper_noise_th_db[16]; /* dB SPL */
    uint8_t  etr_x100[16];          /* exp_trans_ratio x 100, [20,100] */
    uint8_t  nrr_x10[16];           /* noise_red_ratio x 10, [1,15] */
    uint8_t  sasf[16];              /* speech adap sf [1,16] */
} bs300_enr_t;

/* ================================================================
 *  Other modules structured data (64 bytes)
 * ================================================================ */
typedef struct {
    /* Volume/Beep (8 bytes) */
    uint8_t  vol_enable;            /* 0=off, 1=on */
    uint8_t  beep_level;            /* dB, [20, 140] */
    uint8_t  beep_freq_idx;         /* Freq table index */
    int8_t   min_vol;               /* dB, [-48, 18] */
    int8_t   max_vol;               /* dB, [-48, 18] */
    uint8_t  input_selection;       /* 0=FrontMic..6=MM+DAI */
    uint8_t  batt_beep_level;       /* dB */
    uint8_t  batt_beep_freq_idx;    /* Freq table index */

    /* DFBC (1 byte + 1 reserved) */
    uint8_t  dfbc_enable_mode;      /* bit7=enable, bit[3:0]=mode */
    uint8_t  _reserved_dfbc;

    /* ISS (2 bytes) */
    uint8_t  iss_enable;            /* 0=off, 1=on */
    uint8_t  iss_threshold;         /* dB SPL, [50, 110] */

    /* WNR (2 bytes + 1 reserved) */
    uint8_t  wnr_enable_dual;       /* bit0=enable, bit1=dual_mic */
    uint8_t  wnr_preset;            /* 0=Minimal..4=Maximum */
    uint8_t  _reserved_wnr;

    /* AGCO (6 bytes) */
    uint8_t  agco_enable;           /* 0=off, 1=on */
    int8_t   agco_threshold_db;     /* negative value, e.g. -3 */
    uint16_t agco_attack_01ms;      /* [1, 2500] */
    uint16_t agco_release_01ms;     /* [1, 2500] */

    /* MM Plus (3 bytes) */
    uint8_t  mm_plus_enable;        /* 0=off, 1=on */
    int8_t   mix_ratio;             /* [-50, 24] dB */
    uint8_t  mm_type;               /* 0x00=Telecoil, 0x01=DAI */

    /* DDM2 (5 bytes) */
    uint8_t  ddm2_enable;           /* 0=off, 1=on */
    uint8_t  open_ear;              /* 0=off, 1=on */
    uint8_t  polar_pattern;         /* 0=off, 1=on */
    uint8_t  adm_fdm;               /* 0=off, 1=on */
    uint8_t  omni_threshold;        /* [40,100] dB, 0=disabled */

    /* Runtime Volume/EQ (4 bytes) */
    uint8_t  volume_level;          /* 0-9, each step 3dB */
    int8_t   eq_low;                /* low freq gain (<500Hz), [-12,12] dB */
    int8_t   eq_mid;                /* mid freq gain (500-2000Hz), [-12,12] dB */
    int8_t   eq_high;               /* high freq gain (>2000Hz), [-12,12] dB */

    /* Padding to 64 bytes */
    uint8_t  reserved[1];
} bs300_modules_t;

/* ================================================================
 *  Per-program complete structured data
 * ================================================================ */
typedef struct {
    bs300_wdrc_t    wdrc;
    bs300_enr_t     enr;
    bs300_modules_t modules;
} bs300_prog_struct_t;

/* ================================================================
 *  Flash decode
 * ================================================================ */
int bs300_flash_to_struct(const uint8_t *flash_buf, bs300_prog_struct_t *out);
int bs300_struct_to_flash(const bs300_prog_struct_t *prog, uint8_t *flash_buf);

/* ================================================================
 *  Param I2C Encode functions (31 total)
 *  Each encodes struct fields + calibration → 48-byte data packet.
 * ================================================================ */

/* WDRC (11 commands) */
int bs300_encode_wdrc_general(const bs300_wdrc_t *wdrc, uint8_t *data);
int bs300_encode_wdrc_freq_spacing(const bs300_wdrc_t *wdrc, uint8_t *data);
int bs300_encode_wdrc_kp_threshold(const bs300_wdrc_t *wdrc,
                                    const bs300_calib_t *calib,
                                    uint8_t input_type, uint8_t *data);
int bs300_encode_wdrc_attack_time(const bs300_wdrc_t *wdrc, uint8_t *data);
int bs300_encode_wdrc_release_time(const bs300_wdrc_t *wdrc, uint8_t *data);
int bs300_encode_wdrc_ratio(const bs300_wdrc_t *wdrc, uint8_t *data);
int bs300_encode_wdrc_bin_gain(const bs300_wdrc_t *wdrc,
                                const bs300_calib_t *calib,
                                const bs300_modules_t *mod,
                                uint8_t input_type, uint8_t *data);
int bs300_encode_wdrc_lmt_threshold(const bs300_wdrc_t *wdrc,
                                     const bs300_calib_t *calib, uint8_t *data);
int bs300_encode_wdrc_lmt_attack(const bs300_wdrc_t *wdrc, uint8_t *data);
int bs300_encode_wdrc_lmt_release(const bs300_wdrc_t *wdrc, uint8_t *data);
int bs300_encode_wdrc_lmt_ratio(const bs300_wdrc_t *wdrc, uint8_t *data);

/* ENR (9 commands) */
int bs300_encode_enr_general(const bs300_enr_t *enr, uint8_t *data);
int bs300_encode_enr_freq_spacing(const bs300_enr_t *enr, uint8_t *data);
int bs300_encode_enr_snr_threshold(const bs300_enr_t *enr, uint8_t *data);
int bs300_encode_enr_max_att(const bs300_enr_t *enr, uint8_t *data);
int bs300_encode_enr_noise_th(const bs300_enr_t *enr,
                               const bs300_calib_t *calib,
                               uint8_t input_type, uint8_t *data);
int bs300_encode_enr_upper_noise_th(const bs300_enr_t *enr,
                                     const bs300_calib_t *calib,
                                     uint8_t input_type, uint8_t *data);
int bs300_encode_enr_smoothing(const bs300_enr_t *enr, uint8_t *data);
int bs300_encode_enr_etr(const bs300_enr_t *enr, uint8_t *data);
int bs300_encode_enr_nrr(const bs300_enr_t *enr, uint8_t *data);
int bs300_encode_enr_sasf(const bs300_enr_t *enr, uint8_t *data);

/* Volume/Beep/Input (1 command) */
int bs300_encode_volume_beep(const bs300_modules_t *mod,
                              const bs300_calib_t *calib, uint8_t *data);

/* DFBC (1 command) */
int bs300_encode_dfbc(const bs300_modules_t *mod,
                       const bs300_calib_t *calib, uint8_t *data);

/* ISS (1 command) */
int bs300_encode_iss(const bs300_modules_t *mod,
                      const bs300_calib_t *calib,
                      uint8_t input_type, uint8_t *data);

/* WNR (4 commands) */
int bs300_encode_wnr_setup(const bs300_modules_t *mod,
                            const bs300_calib_t *calib, uint8_t *data);
int bs300_encode_wnr_band_0_15(const bs300_modules_t *mod,
                                const bs300_calib_t *calib,
                                uint8_t input_type, uint8_t *data);
int bs300_encode_wnr_band_16_31(const bs300_modules_t *mod,
                                 const bs300_calib_t *calib,
                                 uint8_t input_type, uint8_t *data);
int bs300_encode_wnr_single_mic(const bs300_modules_t *mod,
                                 const bs300_calib_t *calib,
                                 uint8_t input_type, uint8_t *data);

/* AGCO (1 command) */
int bs300_encode_agco(const bs300_modules_t *mod, uint8_t *data);

/* Input source (3 commands, conditional) */
int bs300_encode_mm_plus(const bs300_modules_t *mod,
                          const bs300_calib_t *calib,
                          uint8_t input_type, uint8_t *data);
int bs300_encode_ddm2(const bs300_modules_t *mod,
                       const bs300_calib_t *calib, uint8_t *data);
int bs300_encode_tc_dai(const bs300_calib_t *calib,
                         uint8_t input_type, uint8_t *data);

/* Input Tone Generator (0x8001E2) */
int bs300_encode_itg(uint8_t level_db, uint16_t freq_hz, uint8_t enable,
                     const bs300_calib_t *calib, uint8_t *data);

#ifdef __cplusplus
}
#endif

#endif /* BS300_PARAM_ENCODE_H */
