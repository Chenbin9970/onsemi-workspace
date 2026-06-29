#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BS300 DSP Protocol — split-module hub. Re-exports all public symbols."""

# ── proto ──
from proto import (
    bs300_checksum, bs300_build_simple_cmd, bs300_build_read_request,
    bs300_build_advanced_write, bs300_parse_response,
    bs300_get_word, bs300_set_word,
    bs300_cmd_furproc, bs300_cmd_pktnum, bs300_cmd_rdwrtbn,
    test_step0,
)

# ── calib ──
from calib import (
    CalibData, parse_calibration,
    _build_calib_packet0, _build_calib_packet1, _build_calib_packet2,
    _find_data_offset,
    test_step1, test_step1_crossval,
)

# ── flash_read ──
from flash_read import (
    BitReader,
    WdrcChannelFlash, WdrcFlash, VolumeFlash, InputsFlash,
    DfbcFlash, EnrChannelFlash, EnrFlash, IssFlash, WnrFlash, AgcoFlash,
    ProgramData, _CH_FREQ_TABLE,
    _decode_wdrc_channel, _decode_wdrc, _decode_volume, _decode_inputs,
    _decode_dfbc, _decode_enr, _decode_iss, _decode_wnr, _decode_agco,
    parse_program_data,
)

# ── flash_write ──
from flash_write import (
    _BitWriter, _build_program_raw, _build_wdrc_flash, _build_volume_flash,
    encode_wdrc_flash, encode_volume_flash, encode_dfbc_flash,
    encode_iss_flash, encode_wnr_flash, encode_agco_flash, encode_enr_flash,
    build_program_flash_data, split_into_packets,
    generate_burn_write_frames, generate_burn_end_frame, generate_read_result_json,
    _extract_readback_data, _extract_write_data,
    test_step2, test_step3_crossval, test_step3_crossval_p1, _step3_crossval_prog,
)

# ── math_utils ──
from math_utils import (
    _TIME_TABLE, _RATIO_TABLE,
    _build_time_table, _time_to_index, _ratio_to_index, _freq_to_index,
    frac24_to_s32, clamp_u32, clamp_s32,
    test_step4,
)

# ── param ──
from param import (
    _frac24, _int24, _db_to_frac24, _db_to_int24,
    _avg_ceil, _avg_floor, _beep_freq_to_data,
    _pack_bytes, _pack_int12_2pw, _pack_uint6_4pw,
    encode_wdrc_general_param, encode_wdrc_freq_spacing_param,
    encode_wdrc_kp_threshold_param, encode_wdrc_attack_time_param,
    encode_wdrc_release_time_param, encode_wdrc_ratio_param,
    encode_wdrc_bin_gain_param, encode_wdrc_lmt_threshold_param,
    encode_wdrc_lmt_atk_time_param, encode_wdrc_lmt_rel_time_param,
    encode_wdrc_lmt_ratio_param,
    encode_volume_beep_param, encode_tc_dai_gain_diff_param,
    encode_dfbc_param, encode_iss_param,
    encode_wnr_1_param, encode_wnr_band_data_param,
    encode_wnr_single_mic_detect_param, encode_wnr_dual_mic_mode_param,
    encode_enr_general_param, encode_enr_freq_spacing_param,
    encode_enr_snr_threshold_param, encode_enr_max_att_param,
    encode_enr_noise_th_param, encode_enr_upper_noise_th_param,
    encode_enr_smoothing_param, encode_enr_etr_param, encode_enr_nrr_param,
    encode_enr_sasf_param,
    encode_agco_param, encode_mm_plus_param, encode_ddm2_param,
    _beep_freq_to_cal_band,
    test_step5, test_step5_crossval, test_step5_crossval_p1, _step5_crossval_prog,
)


if __name__ == '__main__':
    test_step0()
    test_step1()
    test_step1_crossval()
    test_step2()
    test_step3_crossval()
    test_step3_crossval_p1()
    test_step4()
    test_step5()
    test_step5_crossval()
    test_step5_crossval_p1()
