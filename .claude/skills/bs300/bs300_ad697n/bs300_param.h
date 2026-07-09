#ifndef BS300_APP_COMMON_DEVICE_BS300_BS300_PARAM_H
#define BS300_APP_COMMON_DEVICE_BS300_BS300_PARAM_H

/*
 * Type dependencies (u8, u16, s8, etc.) are provided by bs300_driver.h
 * which includes asm/iic_soft.h → cpu.h before including this header.
 * Do NOT include this header directly without bs300_driver.h first.
 */

/* ================================================================
 *  Sync mode: 0 = hardcoded I2C trace, 1 = dynamic encode from struct
 *  Default: dynamic (uses C encode functions translated from Python codegen)
 * ================================================================ */
#ifndef BS300_SYNC_USE_DYNAMIC
#define BS300_SYNC_USE_DYNAMIC  1
#endif

/* ================================================================
 *  Structured VM storage sizes
 * ================================================================ */
#define BS300_WDRC_STRUCT_SIZE       292
#define BS300_ENR_STRUCT_SIZE        134
#define BS300_MODULES_STRUCT_SIZE    64
#define BS300_STRUCT_TOTAL_SIZE      (BS300_WDRC_STRUCT_SIZE + \
                                      BS300_ENR_STRUCT_SIZE + \
                                      BS300_MODULES_STRUCT_SIZE)  /* 490B */
#define BS300_STRUCT_MAX_SIZE        512

/* VM ID for current active program number (0-3) + struct format version */
#define BS300_VM_ID_ACTIVE_PROG      31
#define BS300_STRUCT_VERSION         2    /* increment when struct layout changes */

/* ================================================================
 *  WDRC structured data (292 bytes)
 * ================================================================ */
typedef struct {
    u8 total_channels;          /* [1, 16] */
    u8 nsbc;                    /* single-band channel count */
    u8 kp_mode;                 /* 1=1KP, 2=2KP */
    u8 limiter;                 /* 0=off, 1=on */
    u8 freq_idx[16];            /* frequency table index per channel */
    s8 kp1_th_db[16];           /* KP1 threshold, value_in_MT */
    s8 kp2_th_db[16];           /* KP2 threshold */
    u8 epd_at_idx[16];          /* Expander Attack, Table 2-2 index */
    u8 epd_rt_idx[16];          /* Expander Release */
    u8 epd_r_idx[16];           /* Expander Ratio, Table 2-3 index */
    u8 kp1_at_idx[16];          /* KP1 Attack */
    u8 kp2_at_idx[16];          /* KP2 Attack */
    u8 kp1_rt_idx[16];          /* KP1 Release */
    u8 kp2_rt_idx[16];          /* KP2 Release */
    u8 kp1_r_idx[16];           /* KP1 Ratio */
    u8 kp2_r_idx[16];           /* KP2 Ratio */
    s8 lmt_th_db[16];           /* Limiter threshold (value_in_MT) */
    u8 lmt_at_idx[16];          /* Limiter Attack */
    u8 lmt_rt_idx[16];          /* Limiter Release */
    u8 lmt_r_idx[16];           /* Limiter Ratio */
    s8 bin_gain[32];            /* 32 band gain, value_in_MT */
} bs300_wdrc_t;

/* ================================================================
 *  ENR structured data (134 bytes)
 * ================================================================ */
typedef struct {
    u8 enable_num_ch;           /* bit7=enable, bit[3:0]=channel_count */
    u8 nfsf;                    /* noise falling sf [1,16] */
    u8 nhsf;                    /* noise hold sf [1,16] */
    u8 nnsf;                    /* noise normal sf [1,16] */
    u8 snasf;                   /* speech non-adap sf [1,16] */
    u8 freq_idx[16];            /* frequency table index per channel */
    u8 snr_th_db[16];           /* [4, 30] dB */
    u8 max_att_db[16];          /* [0, 30] dB */
    u8 noise_th_db[16];         /* [10, 72] dB SPL */
    u8 upper_noise_th_db[16];   /* [40, 102] dB SPL */
    u8 etr_x100[16];            /* exp_trans_ratio × 100, [20, 100] */
    u8 nrr_x10[16];             /* noise_red_ratio × 10, [1, 15] */
    u8 sasf[16];                /* speech adap sf [1,16] */
} bs300_enr_t;

/* ================================================================
 *  Other modules structured data (~64 bytes)
 * ================================================================ */
typedef struct {
    /* Volume/Beep (8 bytes) */
    u8 vol_enable;              /* 0=off, 1=on */
    u8 beep_level;              /* dB, [20, 140] */
    u8 beep_freq_idx;           /* Freq table index */
    s8  min_vol;                /* dB, [-48, 18] */
    s8  max_vol;                /* dB, [-48, 18] */
    u8 input_selection;         /* 0=FrontMic..6=MM+DAI */
    u8 batt_beep_level;         /* dB */
    u8 batt_beep_freq_idx;      /* Freq table index */

    /* DFBC (2 bytes) */
    u8 dfbc_enable_mode;        /* bit7=enable, bit[3:0]=mode */

    /* ISS (2 bytes) */
    u8 iss_enable;              /* 0=off, 1=on */
    u8 iss_threshold;           /* dB SPL, [50, 110] */

    /* WNR (3 bytes) */
    u8 wnr_enable_dual;         /* bit0=enable, bit1=dual_mic */
    u8 wnr_preset;              /* 0=Minimal..4=Maximum */

    /* AGCO (6 bytes) */
    u8 agco_enable;             /* 0=off, 1=on */
    s8 agco_threshold_db;       /* negative value, e.g. -3 */
    u16 agco_attack_01ms;       /* [1, 2500], little-endian */
    u16 agco_release_01ms;      /* [1, 2500], little-endian */

    /* MM Plus (2 bytes) */
    u8 mm_plus_enable;          /* 0=off, 1=on */
    u8 mix_ratio;               /* integer */

    /* DDM2 (5 bytes) */
    u8 ddm2_enable;             /* 0=off, 1=on */
    u8 open_ear;                /* 0=off, 1=on */
    u8 polar_pattern;           /* 0=off, 1=on */
    u8 adm_fdm;                 /* 0=off, 1=on */
    u8 omni_threshold;          /* [40,100] dB, 0=disabled */

    /* Runtime Volume/EQ (4 bytes) */
    u8 volume_level;            /* 0-5, each step +5dB */
    s8 eq_low;                  /* low freq gain (<500Hz), [-12, 12] dB */
    s8 eq_mid;                  /* mid freq gain (500-2000Hz), [-12, 12] dB */
    s8 eq_high;                 /* high freq gain (>2000Hz), [-12, 12] dB */

    /* Padding to 64 bytes */
    u8 reserved[2];
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
 *  API declarations
 * ================================================================ */

/**
 * @brief Convert BS300 Flash 480B bit-packed data to structured values.
 * Only used on first boot when VM is empty.
 *
 * @param flash_buf  480-byte Flash bit-packed program data
 * @param out        Output structured data
 * @return 0 on success, negative on error
 */
int bs300_flash_to_struct(const u8 *flash_buf, bs300_prog_struct_t *out);

/**
 * @brief Read current active program index from VM.
 *
 * @param prog_idx  Output program index (0-3), defaults to 0 if VM empty
 * @return 0 on success, negative on error
 */
int bs300_vm_read_active_prog(u8 *prog_idx);

/**
 * @brief Save current active program index to VM.
 *
 * @param prog_idx  Program index (0-3)
 * @return 0 on success, negative on error
 */
int bs300_vm_write_active_prog(u8 prog_idx);

/**
 * @brief Load structured program data from VM segments.
 *
 * @param prog_idx  Program index (0-3)
 * @param out       Output structured data buffer
 * @return 0 on success, negative on error
 */
int bs300_vm_load_struct(u8 prog_idx, bs300_prog_struct_t *out);

/**
 * @brief Save structured program data to VM segments.
 *
 * @param prog_idx  Program index (0-3)
 * @param data      Structured data to save
 * @return 0 on success, negative on error
 */
int bs300_vm_save_struct(u8 prog_idx, const bs300_prog_struct_t *data);

/* ================================================================
 *  Calibration data (parsed from VM ID 28, 144B raw)
 * ================================================================ */
typedef struct {
    u8  mic1_band[32];          /* output_cal[x] - gain_cal[x], band 0 invalid */
    u8  output_band[32];        /* output_cal[x], band 0 invalid */
    s16 mic2_gain_diff;         /* 0.1 dB LSB, range [-5.0, 5.0] dB */
    u16 mic_delay;              /* 0.1 us LSB */
    s16 telecoil_gain_diff;     /* 0.1 dB LSB, range [-50.0, 50.0] dB */
    s16 dai_gain_diff;          /* 0.1 dB LSB */
    u16 fbc_bulk_delay;         /* 1 us LSB */
} bs300_calib_t;

/**
 * @brief Parse 144-byte raw calibration data into structured form.
 * @param raw  144-byte buffer (3 packets × 48B)
 * @param out  Output parsed calibration
 * @return 0 on success, negative on error
 */
int bs300_parse_calibration(const u8 *raw, bs300_calib_t *out);

/* ================================================================
 *  Param I2C encode functions (struct + calib → 48B packet)
 * ================================================================ */

/* WDRC (11 commands) */
int bs300_encode_wdrc_general(const bs300_wdrc_t *wdrc, u8 *data);
int bs300_encode_wdrc_freq_spacing(const bs300_wdrc_t *wdrc, u8 *data);
int bs300_encode_wdrc_kp_threshold(const bs300_wdrc_t *wdrc,
                                    const bs300_calib_t *calib,
                                    u8 input_type, u8 *data);
int bs300_encode_wdrc_attack_time(const bs300_wdrc_t *wdrc, u8 *data);
int bs300_encode_wdrc_release_time(const bs300_wdrc_t *wdrc, u8 *data);
int bs300_encode_wdrc_ratio(const bs300_wdrc_t *wdrc, u8 *data);
int bs300_encode_wdrc_bin_gain(const bs300_wdrc_t *wdrc,
                                const bs300_calib_t *calib,
                                const bs300_modules_t *mod,
                                u8 input_type, u8 *data);
int bs300_encode_wdrc_lmt_threshold(const bs300_wdrc_t *wdrc,
                                     const bs300_calib_t *calib, u8 *data);
int bs300_encode_wdrc_lmt_attack(const bs300_wdrc_t *wdrc, u8 *data);
int bs300_encode_wdrc_lmt_release(const bs300_wdrc_t *wdrc, u8 *data);
int bs300_encode_wdrc_lmt_ratio(const bs300_wdrc_t *wdrc, u8 *data);

/* ENR (10 commands) */
int bs300_encode_enr_general(const bs300_enr_t *enr, u8 *data);
int bs300_encode_enr_freq_spacing(const bs300_enr_t *enr, u8 *data);
int bs300_encode_enr_snr_threshold(const bs300_enr_t *enr, u8 *data);
int bs300_encode_enr_max_att(const bs300_enr_t *enr, u8 *data);
int bs300_encode_enr_noise_th(const bs300_enr_t *enr,
                               const bs300_calib_t *calib,
                               u8 input_type, u8 *data);
int bs300_encode_enr_upper_noise_th(const bs300_enr_t *enr,
                                     const bs300_calib_t *calib,
                                     u8 input_type, u8 *data);
int bs300_encode_enr_smoothing(const bs300_enr_t *enr, u8 *data);
int bs300_encode_enr_etr(const bs300_enr_t *enr, u8 *data);
int bs300_encode_enr_nrr(const bs300_enr_t *enr, u8 *data);
int bs300_encode_enr_sasf(const bs300_enr_t *enr, u8 *data);

/* Volume/Beep/Input (1 command) */
int bs300_encode_volume_beep(const bs300_modules_t *mod,
                              const bs300_calib_t *calib, u8 *data);

/* DFBC (1 command) */
int bs300_encode_dfbc(const bs300_modules_t *mod,
                       const bs300_calib_t *calib, u8 *data);

/* ISS (1 command) */
int bs300_encode_iss(const bs300_modules_t *mod,
                      const bs300_calib_t *calib,
                      u8 input_type, u8 *data);

/* WNR (4 commands) */
int bs300_encode_wnr_setup(const bs300_modules_t *mod,
                            const bs300_calib_t *calib, u8 *data);
int bs300_encode_wnr_band_0_15(const bs300_modules_t *mod,
                                const bs300_calib_t *calib,
                                u8 input_type, u8 *data);
int bs300_encode_wnr_band_16_31(const bs300_modules_t *mod,
                                 const bs300_calib_t *calib,
                                 u8 input_type, u8 *data);
int bs300_encode_wnr_single_mic(const bs300_modules_t *mod,
                                 const bs300_calib_t *calib,
                                 u8 input_type, u8 *data);

/* AGCO (1 command) */
int bs300_encode_agco(const bs300_modules_t *mod, u8 *data);

/* Input source (3 commands, conditional) */
int bs300_encode_mm_plus(const bs300_modules_t *mod,
                          const bs300_calib_t *calib,
                          u8 input_type, u8 *data);
int bs300_encode_ddm2(const bs300_modules_t *mod,
                       const bs300_calib_t *calib, u8 *data);
int bs300_encode_tc_dai(const bs300_calib_t *calib,
                         u8 input_type, u8 *data);

/* Input Tone Generator (0x8001E2) */
int bs300_encode_itg(u8 level_db, u16 freq_hz, u8 enable,
                     const bs300_calib_t *calib, u8 *data);
int bs300_itg_write(soft_iic_dev iic, u8 level_db, u16 freq_hz,
                    const bs300_calib_t *calib);
int bs300_itg_clear(soft_iic_dev iic);

/* ================================================================
 *  Pure-tone Audiometry (0x8001E2 + module disable/restore)
 * ================================================================ */

/**
 * @brief Enter pure-tone audiometry mode.
 * Mutes DSP, disables all non-WDRC modules (ENR/DDM2/MM+/DFBC/
 * NoiseGen2/ISS/WNR/AGCO), then activates. WDRC is left unchanged.
 * After this, use bs300_itg_write/clear (cmd 13/14) to play tones.
 *
 * @param iic  Software I2C device index
 * @return 0 on success, negative on error
 */
int bs300_audiometry_enter(soft_iic_dev iic);

/**
 * @brief Exit pure-tone audiometry mode.
 * Mutes DSP, clears ITG, then restores the full program from VM
 * (bs300_sync_program), then activates.
 *
 * @param iic  Software I2C device index
 * @return 0 on success, negative on error
 */
int bs300_audiometry_exit(soft_iic_dev iic);

/* ================================================================
 *  Main sync entry point
 * ================================================================ */

/**
 * @brief Sync all params from struct to BS300 RAM via Param I2C.
 * Reads calibration from VM, encodes all modules, sends each packet.
 *
 * @param iic       Software I2C device index
 * @param prog      Program struct data
 * @return 0 on success, negative on error
 */
int bs300_sync_program(soft_iic_dev iic, bs300_prog_struct_t *prog);

/**
 * @brief Switch to a different program with incremental sync.
 * Compares old and new program structs, only sends changed commands.
 *
 * @param iic           Software I2C device index
 * @param new_prog_idx  Target program index (0-3)
 * @return 0 on success, negative on error
 */
int bs300_switch_program(soft_iic_dev iic, u8 new_prog_idx);

#endif /* BS300_APP_COMMON_DEVICE_BS300_BS300_PARAM_H */
