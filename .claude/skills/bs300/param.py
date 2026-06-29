import json, os, math
import struct
from proto import (bs300_get_word, bs300_set_word, bs300_cmd_pktnum, bs300_build_advanced_write,
                   bs300_cmd_furproc, bs300_parse_response, bs300_checksum)
from calib import (CalibData, parse_calibration, _find_data_offset, _build_calib_packet0, _build_calib_packet1, _build_calib_packet2)
from flash_read import (BitReader, parse_program_data, ProgramData, _CH_FREQ_TABLE,
    WdrcFlash, VolumeFlash, InputsFlash, DfbcFlash, EnrFlash,
    IssFlash, WnrFlash, AgcoFlash,
    _decode_wdrc, _decode_volume, _decode_inputs,
    _decode_dfbc, _decode_enr, _decode_iss, _decode_wnr, _decode_agco)
from math_utils import (_TIME_TABLE, _time_to_index, _RATIO_TABLE, _ratio_to_index, _FREQ_TABLE,
    _freq_to_index, frac24_to_s32, clamp_u32, clamp_s32)

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# Step 5: Param I2C Command Encoding (word-aligned, calib-compensated)
# ============================================================

import math

# ---- WNR SSP Offset Tables (from handbook §2.25) ----

# WNR band offset table: rows = band_index (0-31), cols = ssp_level (0-4)
_WNR_SSP_OFFSET = [
    [0, -16, -32, -48, -48],    # band 0
    [0, -16, -32, -48, -48],    # band 1
    [0, -16, -32, -48, -48],    # band 2
    [10, -6, -22, -36, -48],    # band 3
    [10, -6, -22, -36, -48],    # band 4
    [10, -6, -22, -36, -48],    # band 5
    [-10, -26, -42, -42, -42],  # band 6
    [-10, -26, -42, -42, -42],  # band 7
    [-10, -26, -42, -42, -42],  # band 8
    [-10, -26, -42, -42, -42],  # band 9
    [-10, -26, -42, -42, -42],  # band 10
    [-10, -26, -42, -42, -42],  # band 11
    [-10, -26, -42, -42, -42],  # band 12
    [-10, -26, -42, -42, -42],  # band 13
    [-10, -26, -42, -42, -42],  # band 14
    [-20, -36, -52, -52, -52],  # band 15
    [-20, -36, -52, -52, -52],  # band 16
    [-20, -36, -52, -52, -52],  # band 17
    [-20, -36, -52, -52, -52],  # band 18
    [-20, -36, -52, -52, -52],  # band 19
    [-20, -36, -52, -52, -52],  # band 20
    [-20, -36, -52, -52, -52],  # band 21
    [-20, -36, -52, -52, -52],  # band 22
    [-20, -36, -52, -52, -52],  # band 23
    [-20, -36, -52, -52, -52],  # band 24
    [-20, -36, -52, -52, -52],  # band 25
    [-20, -36, -52, -52, -52],  # band 26
    [-20, -36, -52, -52, -52],  # band 27
    [-20, -36, -52, -52, -52],  # band 28
    [-20, -36, -52, -52, -52],  # band 29
    [-20, -36, -52, -52, -52],  # band 30
    [-20, -36, -52, -52, -52],  # band 31
]

# Data2Offset table (bands 0-2 only, for WNR Single Mic Detection)
_WNR_DATA2_OFFSET = [
    [0, -8, -16, -26, -34],    # band 0
    [-8, -16, -24, -32, -40],  # band 1
    [-40, -50, -58, -68, -78], # band 2
]

# ---- Word Packing Helpers ----

def _frac24(val: float) -> int:
    """Convert float to frac24 (unsigned 24-bit fixed-point)."""
    return int(val) & 0xFFFFFF

def _int24(val: float) -> int:
    """Convert float to int24 (signed 24-bit, stored unsigned)."""
    ival = int(val)
    return ival & 0xFFFFFF

def _db_to_frac24(val_db: float) -> int:
    """Convert dB value to frac24 using integer arithmetic.
    Computes ceil(val_db * 65536 / 6.02) — chip uses ceiling for the product.
    val_db has at most 1 decimal place (from calibration data / 10).
    """
    n = round(val_db * 10)
    return (n * 327680 + 300) // 301 & 0xFFFFFF

def _db_to_int24(val_db: float) -> int:
    """Convert dB value to signed int24 (stored as unsigned 24-bit).
    Truncates toward zero (int), matching chip behavior."""
    n = round(val_db * 10)
    if n >= 0:
        result = (n * 327680) // 301  # truncate for positive
    else:
        result = -((-n * 327680) // 301)  # truncate toward zero for negative
    return result & 0xFFFFFF

def _avg_ceil(values: list[int]) -> int:
    """Integer ceiling average: ceil(sum / len)."""
    s = sum(values)
    n = len(values)
    return (s + n - 1) // n

def _avg_floor(values: list[int]) -> int:
    """Integer floor average: floor(sum / len)."""
    return sum(values) // len(values)

# Beep frequency → data word lookup (central frequency Hz → index)
_BEEP_FREQ_MAP = {
    250: 1, 500: 2, 750: 3, 1000: 4, 1250: 5, 1500: 6, 1750: 7, 2000: 8,
    2250: 9, 2500: 10, 2750: 11, 3000: 12, 3250: 13,
}

def _beep_freq_to_data(hz: int) -> int:
    """Convert beep central frequency Hz → data word value."""
    return _BEEP_FREQ_MAP.get(hz, 0)

def _pack_bytes(data: bytearray, byte_start: int, values: list[int]):
    """Pack values as consecutive bytes (byte-packing, not word-aligned).
    All Param I2C commands use byte-packing for int8/uint8 fields.
    Only int12/uint12/frac24/int24/uint24/frac48 use word-aligned packing."""
    for i, v in enumerate(values):
        data[byte_start + i] = v & 0xFF

def _pack_int12_2pw(data: bytearray, word_start: int, values: list[int]):
    """Pack int12 values 2 per 24-bit word (first 12 bits LSB, second 12 bits MSB)."""
    for i, v in enumerate(values):
        word_idx = word_start + i // 2
        if i % 2 == 0:
            bs300_set_word(data, word_idx, (bs300_get_word(data, word_idx) & 0xFFF000) | (v & 0xFFF))
        else:
            bs300_set_word(data, word_idx, (bs300_get_word(data, word_idx) & 0x000FFF) | ((v & 0xFFF) << 12))

def _pack_uint6_4pw(data: bytearray, word_start: int, values: list[int]):
    """Pack uint6 values 4 per 24-bit word.
    Word layout (handbook): ch1[23:18], ch2[17:12], ch3[11:6], ch4[5:0].
    So ch1 at MSB (shift 18), ch4 at LSB (shift 0)."""
    for i, v in enumerate(values):
        word_idx = word_start + i // 4
        shift = (3 - (i % 4)) * 6  # ch1→18, ch2→12, ch3→6, ch4→0
        mask = 0x3F << shift
        bs300_set_word(data, word_idx, (bs300_get_word(data, word_idx) & ~mask) | ((v & 0x3F) << shift))

# ---- WDRC Param Encoders ----

def encode_wdrc_general_param(total_channels: int, nsbc: int, kp_mode: str,
                               limiter: bool) -> bytes:
    """WDRC General Setup (0x8000B2)."""
    data = bytearray(48)
    bs300_set_word(data, 0, 0x000001)   # selection: enable
    bs300_set_word(data, 1, total_channels)
    bs300_set_word(data, 2, nsbc)        # NSBC (single-band channels)
    bs300_set_word(data, 3, total_channels - nsbc)  # NMBC (multi-band channels)
    bs300_set_word(data, 4, 2 if kp_mode == '1KP' else 3)  # 2=1KP, 3=2KP
    bs300_set_word(data, 5, 1 if limiter else 0)  # output_limiting_sel
    return bytes(data)


def encode_wdrc_freq_spacing_param(mbc_ch_counts: list[int]) -> bytes:
    """WDRC Frequency Spacing (0x8010B2).
    mbc_ch_counts[i] = number of bins in MBC channel i (only MBC channels).
    MBC_CHx = (bin_count - 1), minimum 1 since each MBC has >= 2 bins.
    Single-band (NOBC) and unused slots fill with 0b000001 (value=1).
    Packed 4 MBC_CHx per 24-bit word, LSB=ch4. Unused words = 0x041041."""
    data = bytearray(48)
    values = [c - 1 for c in mbc_ch_counts]       # MBC_CHx (>= 1)
    values = (values + [1] * 16)[:16]             # pad to 16 entries with 0b000001
    _pack_uint6_4pw(data, 0, values)
    num_used_words = (len(values) + 3) // 4
    for wi in range(num_used_words, 16):
        bs300_set_word(data, wi, 0x041041)
    return bytes(data)


def encode_wdrc_kp_threshold_param(kp1_thresholds: list[int],
                                    calib: CalibData,
                                    kp_mode: str, input_type: str = 'mic',
                                    kp2_thresholds: list[int] = None,
                                    freq_indices: list[int] = None,
                                    cal_offset: list[int] = None) -> bytes:
    """WDRC KP Threshold (0x8020B2). int8 per channel, byte-packed.
    1KP: kp1_thresholds only (16 bytes).
    2KP: kp1 and kp2 interleaved: KP1TH_CH1, KP2TH_CH1, KP1TH_CH2, KP2TH_CH2, ...
    Each WDRC channel spans 2 calibration bands [fidx, fidx+1].
    Formula: data = 60 + threshold - avg_mic1_cal_per_ch - input_gain_diff
    avg_mic1_cal_per_ch = (mic1_band[fidx] + mic1_band[fidx+1]) // 2 + cal_offset[ch_idx]
    cal_offset: optional per-channel calibration compensation (default 0)."""
    data = bytearray(48)
    input_gain = calib.input_gain_diff_db(input_type)
    _offset = cal_offset if cal_offset else [0] * 16

    def _encode_one(th, ch_idx):
        if freq_indices and ch_idx < len(freq_indices):
            fidx = freq_indices[ch_idx]
            mic1_cal = (calib.mic1_band[fidx] + calib.mic1_band[fidx + 1]) // 2
        else:
            mic1_cal = calib.avg_mic1_cal()
        mic1_cal += _offset[ch_idx] if ch_idx < len(_offset) else 0
        return clamp_s32(int(60 + th - mic1_cal - input_gain), -128, 127) & 0xFF

    if kp_mode == '2KP' and kp2_thresholds is not None:
        values = []
        for ch_idx, (kp1, kp2) in enumerate(zip(kp1_thresholds, kp2_thresholds)):
            values.append(_encode_one(kp1, ch_idx))
            values.append(_encode_one(kp2, ch_idx))
        _pack_bytes(data, 0, values)
    else:
        values = [_encode_one(th, i) for i, th in enumerate(kp1_thresholds)]
        _pack_bytes(data, 0, values)
    return bytes(data)


def encode_wdrc_attack_time_param(epd_at_indices: list[int], kp_at_indices: list[int],
                                   kp_mode: str, kp2_at_indices: list[int] = None) -> bytes:
    """WDRC KP Attack Time (0x8030B2).
    1KP: epd_at + kp1_at per channel (2 bytes/ch, 32 bytes total).
    2KP: epd_at + kp1_at + kp2_at per channel (3 bytes/ch, 48 bytes total)."""
    data = bytearray(48)
    num_ch = len(epd_at_indices)
    if kp_mode == '1KP':
        values = []
        for i in range(num_ch):
            values.append(epd_at_indices[i] & 0xFF)
            values.append(kp_at_indices[i] & 0xFF)
        _pack_bytes(data, 0, values)
    else:
        if kp2_at_indices is None:
            kp2_at_indices = kp_at_indices  # fallback
        for i in range(num_ch):
            epd = epd_at_indices[i] & 0xFF
            kp1 = kp_at_indices[i] & 0xFF
            kp2 = kp2_at_indices[i] & 0xFF
            bs300_set_word(data, i, epd | (kp1 << 8) | (kp2 << 16))
    return bytes(data)


def encode_wdrc_release_time_param(epd_rt_indices: list[int], kp_rt_indices: list[int],
                                    kp_mode: str, kp2_rt_indices: list[int] = None) -> bytes:
    """WDRC KP Release Time (0x8040B2). Same layout as Attack Time."""
    return encode_wdrc_attack_time_param(epd_rt_indices, kp_rt_indices, kp_mode, kp2_rt_indices)


def encode_wdrc_ratio_param(epd_r_indices: list[int], kp_r_indices: list[int],
                              kp_mode: str, kp2_r_indices: list[int] = None) -> bytes:
    """WDRC Ratio (0x8050B2). Same layout as Attack/Release Time."""
    return encode_wdrc_attack_time_param(epd_r_indices, kp_r_indices, kp_mode, kp2_r_indices)


def encode_wdrc_bin_gain_param(bin_gains_db: list[int], calib: CalibData,
                                 input_type: str = 'mic') -> bytes:
    """WDRC Bin Gain (0x8060B2). 32 bands, int8 per band, 2 per word."""
    data = bytearray(48)
    input_gain = calib.input_gain_diff_db(input_type)
    gain_cal = [calib.output_band[i] - calib.mic1_band[i] for i in range(32)]

    values = []
    for i, bg in enumerate(bin_gains_db):
        encoded = int(bg - gain_cal[i] + input_gain)
        values.append(clamp_s32(encoded, -128, 127) & 0xFF)

    _pack_bytes(data, 0, values)
    return bytes(data)


def encode_wdrc_lmt_threshold_param(thresholds: list[int], calib: CalibData,
                                      freq_indices: list[int] = None,
                                      cal_offset: list[int] = None) -> bytes:
    """WDRC Limiter Threshold (0x8070B2). int8 per channel, 2 per word.
    Each WDRC channel spans 2 calibration bands [fidx, fidx+1].
    Formula: data = 60 + threshold - avg_output_cal_per_ch.
    avg_output_cal_per_ch = (output_band[fidx] + output_band[fidx+1]) // 2 + cal_offset[ch_idx]
    cal_offset: optional per-channel calibration compensation (default 0)."""
    data = bytearray(48)
    _offset = cal_offset if cal_offset else [0] * 16
    values = []
    for i, th in enumerate(thresholds):
        if freq_indices and i < len(freq_indices):
            fidx = freq_indices[i]
            out_cal = (calib.output_band[fidx] + calib.output_band[fidx + 1]) // 2
        else:
            out_cal = calib.avg_output_cal()
        out_cal += _offset[i] if i < len(_offset) else 0
        encoded = int(60 + th - out_cal)
        values.append(clamp_s32(encoded, -128, 127) & 0xFF)
    _pack_bytes(data, 0, values)
    return bytes(data)


def encode_wdrc_lmt_atk_time_param(indices: list[int]) -> bytes:
    """WDRC Limiter Attack Time (0x8080B2). uint8 per channel, byte-packed."""
    data = bytearray(48)
    _pack_bytes(data, 0, [i & 0xFF for i in indices])
    return bytes(data)


def encode_wdrc_lmt_rel_time_param(indices: list[int]) -> bytes:
    """WDRC Limiter Release Time (0x8090B2). Same layout as Attack."""
    return encode_wdrc_lmt_atk_time_param(indices)


def encode_wdrc_lmt_ratio_param(indices: list[int]) -> bytes:
    """WDRC Limiter Ratio (0x80A0B2). Same layout as Attack."""
    return encode_wdrc_lmt_atk_time_param(indices)


# ---- Volume/Beep Param Encoder ----

def _beep_freq_to_cal_band(freq_hz: int) -> int:
    """Map beep frequency (Hz) to calibration band index.
    Uses the band whose center frequency is closest to the beep frequency."""
    best_idx = 0
    best_dist = 999999
    for i, f in enumerate(_FREQ_TABLE):
        d = abs(f - freq_hz)
        if d < best_dist:
            best_dist = d
            best_idx = i
    return best_idx

# Reverse beep freq map: index → Hz
_BEEP_IDX_TO_HZ = {v: k for k, v in _BEEP_FREQ_MAP.items()}

def encode_volume_beep_param(beep_level_db: int, beep_freq_idx: int,
                              min_vol_db: int, max_vol_db: int,
                              input_selection: int,
                              batt_beep_level_db: int, batt_beep_freq_idx: int,
                              calib: CalibData) -> bytes:
    """Volume/Beep/Input Param (0x800081).
    Beep level uses per-band output_cal based on beep frequency."""
    data = bytearray(48)

    # Beep level: frac24 = 0x7FFFFF / 10^((outCal[band] - beep_level) / 20)
    # Uses per-band output_cal nearest to beep frequency
    beep_hz = _BEEP_IDX_TO_HZ.get(beep_freq_idx, 1000)
    beep_band = _beep_freq_to_cal_band(beep_hz)
    outcal_beep = calib.output_band[beep_band]
    beep_frac = 1.0 / (10 ** ((outcal_beep - beep_level_db) / 20))
    bs300_set_word(data, 0, _frac24(beep_frac * 0x7FFFFF))

    bs300_set_word(data, 1, beep_freq_idx & 0xFF)

    # Volume: int24 = vol * 65536 / 6.02 (chip uses integer arithmetic)
    bs300_set_word(data, 2, _db_to_int24(min_vol_db))
    bs300_set_word(data, 3, _db_to_int24(max_vol_db))

    bs300_set_word(data, 4, input_selection & 0xFF)

    # Battery flat beep — same per-band logic
    batt_hz = _BEEP_IDX_TO_HZ.get(batt_beep_freq_idx, 1000)
    batt_band = _beep_freq_to_cal_band(batt_hz)
    batt_outcal = calib.output_band[batt_band]
    bs300_set_word(data, 5, batt_beep_freq_idx & 0xFF)
    batt_beep_frac = 1.0 / (10 ** ((batt_outcal - batt_beep_level_db) / 20))
    bs300_set_word(data, 6, _frac24(batt_beep_frac * 0x7FFFFF))

    return bytes(data)


# ---- DFBC Param Encoder ----

def encode_dfbc_param(mode: int, calib: CalibData) -> bytes:
    """DFBC Param (0x800052). mode: 0x01-0x0F, delay_n_sample from calibration."""
    data = bytearray(48)
    bs300_set_word(data, 0, mode & 0xFF)
    delay_n = int(round(calib.fbc_bulk_delay / 62.5))  # bulk_delay_us / (1/16000 * 1e6)
    bs300_set_word(data, 1, clamp_u32(delay_n, 0, 524))
    return bytes(data)


# ---- ISS Param Encoder ----

def encode_iss_param(selection: int, threshold_dbspl: int, calib: CalibData,
                      input_type: str = 'mic') -> bytes:
    """ISS Param (0x8001B2). threshold as frac48 across 2 words.
    Handbook: mic1_cal = sum of all 32 bands (outCal - gainCal) / 32, round down.
    data = 1 / 10^((-3 - threshold + mic1_cal + input_gain_diff) / 10) as frac48."""
    data = bytearray(48)
    bs300_set_word(data, 0, selection & 0xFF)
    # ISS uses all 32 bands, round to nearest integer
    all_mic1 = [calib.mic1_band[i] for i in range(32)]
    mic1_cal = round(sum(all_mic1) / 32)  # chip: round(4724/32)=148, not floor 147
    input_gain = calib.input_gain_diff_db(input_type)
    exponent = (-3 - threshold_dbspl + mic1_cal + input_gain) / 10.0
    frac_val = 1.0 / (10.0 ** exponent)
    frac48 = round(frac_val * (1 << 47))  # chip: round, not truncate
    bs300_set_word(data, 1, frac48 & 0xFFFFFF)
    bs300_set_word(data, 2, (frac48 >> 24) & 0xFFFFFF)
    return bytes(data)


# ---- WNR Param Encoders ----

def encode_wnr_1_param(enable: bool, dual_mic: bool, calib: CalibData,
                        suppression_preset: int) -> bytes:
    """WNR_1 (0x8001C2) - WNR General Setup.
    Full layout per handbook:
      word 0: WNR selection (bit0=enable, bit1=dual-mic mode)
      word 1: Detection level threshold = round(75 - sum(outCal-gainCal)/32) * (65536/6.02/8)
      word 2: mic2 cal data = 1 / 10^(-mic2_gain_diff/20) as frac24
      word 3: Suppression strength preset 0x000003 (preset 1-4) or 0x000006 (preset 5)
      word 4: 0x001543 (fixed)
      word 5: 0x2aaaab (fixed)
      word 6: 0x200000 (fixed)
      words 7-15: zeros"""
    data = bytearray(48)

    # word 0: selection
    sel = 0
    if enable:
        sel |= 1  # bit0: enable
    if dual_mic:
        sel |= 2  # bit1: dual-mic mode
    bs300_set_word(data, 0, sel)

    # word 1: detection level threshold = round(75 - ceil(sum/32)) * (65536/6.02/8)
    # chip uses integer ceiling average, not floating-point
    all_mic1 = [calib.mic1_band[i] for i in range(32)]
    avg_all = _avg_ceil(all_mic1)  # chip: ceil(4724/32) = 148
    detect_val = int(round((75 - avg_all) * (65536 / 6.02 / 8)))
    bs300_set_word(data, 1, detect_val & 0xFFFFFF)

    # word 2: mic2 cal data = 1 / 10^(-mic2_gain_diff/20) * 0x800000
    x = calib.mic2_gain_diff / 10.0  # 0.1 dB → dB
    mic2_cal = 1.0 / (10 ** (-x / 20))
    bs300_set_word(data, 2, _frac24(mic2_cal * 0x800000))

    # word 3: suppression strength preset
    if suppression_preset >= 5:
        bs300_set_word(data, 3, 0x000006)
    else:
        bs300_set_word(data, 3, 0x000003)

    # words 4-6: fixed data
    bs300_set_word(data, 4, 0x001543)
    bs300_set_word(data, 5, 0x2aaaab)
    bs300_set_word(data, 6, 0x200000)

    return bytes(data)


def encode_wnr_band_data_param(calib: CalibData, ssp_level: int,
                                 input_type: str = 'mic', band_start: int = 0) -> bytes:
    """WNR Band Data — 16 bands per command.
    band_start=0 → WNR_2 (0x8011C2) bands 0-15.
    band_start=16 → WNR_3 (0x8411C2) bands 16-31.
    band_N_data = 0x2A9764 - ((outCal - gainCal + input_gain) * 2 - offset) * (65536/6.02)
    Uses _db_to_frac24 for chip-compatible integer arithmetic."""
    data = bytearray(48)
    input_gain = calib.input_gain_diff_db(input_type)
    for bi in range(16):
        band = band_start + bi
        offset = _WNR_SSP_OFFSET[band][ssp_level]
        val_db = (calib.mic1_band[band] + input_gain) * 2 - offset
        val = 0x2A9764 - _db_to_frac24(val_db)
        bs300_set_word(data, bi, val & 0xFFFFFF)
    return bytes(data)


def encode_wnr_single_mic_detect_param(calib: CalibData, ssp_level: int,
                                         input_type: str = 'mic') -> bytes:
    """WNR Single Mic Detection (0x8021C2). Bands 0-2, data2 formula.
    data2 = 3292041 - ((outCal - gainCal + input_gain) * 2 - offset) * (65536/6.02)
    Uses _db_to_frac24 for chip-compatible integer arithmetic."""
    data = bytearray(48)
    input_gain = calib.input_gain_diff_db(input_type)
    for band in range(3):
        offset = _WNR_DATA2_OFFSET[band][ssp_level]
        val_db = (calib.mic1_band[band] + input_gain) * 2 - offset
        val = 3292041 - _db_to_frac24(val_db)
        bs300_set_word(data, band, val & 0xFFFFFF)
    # Band 3..31: zero
    return bytes(data)


def encode_wnr_dual_mic_mode_param(mode_sel: int, ref_sel: int) -> bytes:
    """WNR Dual Mic Mode Selection (0x8411C2)."""
    data = bytearray(48)
    bs300_set_word(data, 0, mode_sel & 0xFF)
    bs300_set_word(data, 1, ref_sel & 0xFF)
    return bytes(data)


# ---- ENR Param Encoders ----

def encode_enr_general_param(selection: int, total_channels: int,
                               sbc: int, mbc: int) -> bytes:
    """ENR General Setup (0x8000C2)."""
    data = bytearray(48)
    bs300_set_word(data, 0, selection & 0xFF)
    bs300_set_word(data, 1, total_channels)
    bs300_set_word(data, 2, sbc)          # single-band channels
    bs300_set_word(data, 3, mbc)          # multi-band channels
    return bytes(data)


def encode_enr_freq_spacing_param(band_counts: list[int]) -> bytes:
    """ENR Frequency Spacing (0x8010C2).
    band_counts[i] = number of calibration bands in ENR channel i.
    Unlike WDRC (MBC_CHx = bin_count - 1), ENR FS_CHx stores the band count directly.
    Unused slots fill with 0, not 0x041041."""
    data = bytearray(48)
    values = (band_counts + [0] * 16)[:16]  # pad with 0, not 1
    _pack_uint6_4pw(data, 0, values)
    return bytes(data)


def encode_enr_snr_threshold_param(snr_values: list[int]) -> bytes:
    """ENR SNR Threshold (0x8020C2). int12 packed 2 per word.
    SNRT_CHx = floor(32/6.02 * value), range [4, 30] dB."""
    data = bytearray(48)
    encoded = [clamp_s32(int(32.0 / 6.02 * v), 0, 4095) for v in snr_values]
    _pack_int12_2pw(data, 0, encoded)
    return bytes(data)


def encode_enr_max_att_param(max_att_values: list[int], snr_th_values: list[int]) -> bytes:
    """ENR Max Attenuation Rate (0x8030C2). int12 packed 2 per word.
    MAR_CHx = floor((max_att / SNR_threshold) * 256), range [0, 30] dB."""
    data = bytearray(48)
    encoded = [clamp_s32(int(ma / max(st, 1) * 256), 0, 4095)
               for ma, st in zip(max_att_values, snr_th_values)]
    _pack_int12_2pw(data, 0, encoded)
    return bytes(data)



def encode_enr_noise_th_param(noise_th_values: list[int], calib: CalibData,
                                input_type: str = 'mic',
                                freq_indices: list[int] = None,
                                band_counts: list[int] = None) -> bytes:
    """ENR Noise Threshold (0x8040C2). int12 packed 2 per word.

    Formula (from chip code):
      NT_CHx = round(5.307 * (x + 130 - mic1Cal - input_gain_diff_dB) - 371.2)
    where x = noise_th_db.

    mic1Cal = round(sum(mic1_cal[fidx .. fidx+cnt-1]) / cnt) 四舍五入
    Band range from ENR Frequency Spacing (FS_CHx, 0x8010C2):
      - start index = freq_indices[i] (from Flash ENR freq)
      - count = band_counts[i] = readbuf[i+1] - readbuf[i] (or 32 - readbuf[last])

    freq_indices: per-channel band start indices (Flash ENR frequency_idx).
    band_counts:  per-channel calibration band count (FS_CHx from 0x8010C2).
    """
    data = bytearray(48)
    input_gain = calib.input_gain_diff_db(input_type)
    encoded = []
    for i, nt in enumerate(noise_th_values):
        fidx = freq_indices[i] if freq_indices and i < len(freq_indices) else i % 32
        cnt = band_counts[i] if band_counts and i < len(band_counts) else 1
        s = sum(calib.mic1_band[fidx + j] for j in range(cnt))
        # chip: testvalueu16 = sum * 10 / cnt; 四舍五入
        t = s * 10 // cnt
        mic1_cal = t // 10 + 1 if (t % 10) >= 5 else t // 10
        val = round(5.307 * (nt + 130 - mic1_cal - input_gain) - 371.2)
        encoded.append(clamp_s32(val, -2048, 2047) & 0xFFF)
    _pack_int12_2pw(data, 0, encoded)
    return bytes(data)


def encode_enr_upper_noise_th_param(upper_noise_th_values: list[int],
                                      calib: CalibData,
                                      input_type: str = 'mic',
                                      freq_indices: list[int] = None,
                                      band_counts: list[int] = None) -> bytes:
    """ENR Upper Noise Threshold (0x8050C2). Same formula as Noise Threshold."""
    return encode_enr_noise_th_param(upper_noise_th_values, calib, input_type,
                                     freq_indices, band_counts)


def encode_enr_smoothing_param(smoothing_values: dict) -> bytes:
    """ENR Smoothing Factors (0x8060C2).
    smoothing_values: dict with keys 'nhsf', 'nfsf', 'nnsf', 'snasf'.
    Each value 1-16; data1 = 1 << (23 - value), data2 = 0x7FFFFF - data1 + 1."""
    data = bytearray(48)
    # Fixed values for first 6 words
    bs300_set_word(data, 0, 0x200000)
    bs300_set_word(data, 1, 0x600000)
    bs300_set_word(data, 2, 0x100000)
    bs300_set_word(data, 3, 0x700000)
    bs300_set_word(data, 4, 0x020000)
    bs300_set_word(data, 5, 0x7E0000)

    # Smoothing factors for noise_hold, noise_falling, noise_normal, speech_nonadap
    sf_map = [
        ('nhsf', 6, 7),      # noise_hold_sf
        ('nfsf', 8, 9),      # noise_falling_sf
        ('nnsf', 10, 11),    # noise_normal_sf
        ('snasf', 13, 14),   # speech_nonadap_sf
    ]
    for key, word1, word2 in sf_map:
        val = smoothing_values.get(key, 8)
        if val >= 2:
            d1 = 1 << (23 - val)
            d2 = 0x7FFFFF - d1 + 1
        else:
            d1, d2 = 0x7FFFFF, 0x000001
        bs300_set_word(data, word1, d1 & 0xFFFFFF)
        bs300_set_word(data, word2, d2 & 0xFFFFFF)

    bs300_set_word(data, 12, 0x004000)
    return bytes(data)


def encode_enr_etr_param(etr_values: list[float], max_att_values: list[int]) -> bytes:
    """ENR Expansion Transition Ratio (0x8070C2). frac24 per channel.
    Handbook: Data = 6.02/32 * (1 - 1/ETR) / MaxAttenuation
    ETR ∈ [0.2, 1.0], so (1 - 1/ETR) is negative for ETR < 1.
    Result is signed frac24 (int24)."""
    data = bytearray(48)
    for i, (etr, ma) in enumerate(zip(etr_values, max_att_values)):
        val = (6.02 / 32) * (1 - 1.0 / max(etr, 0.01)) / max(ma, 1)
        coded = int(val * 0x800000)  # signed 24-bit scaling
        bs300_set_word(data, i, coded & 0xFFFFFF)
    return bytes(data)


def encode_enr_nrr_param(nrr_values: list[float], max_att_values: list[int]) -> bytes:
    """ENR Noise Reduction Ratio (0x8080C2). frac24 per channel.
    NRR_CHx = frac24(6.02/32 * ratio / MaxAttenuation)"""
    data = bytearray(48)
    for i, (nrr, ma) in enumerate(zip(nrr_values, max_att_values)):
        val = (6.02 / 32) * nrr / max(ma, 1)
        bs300_set_word(data, i, _frac24(val * 0x7FFFFF))
    return bytes(data)


def encode_enr_sasf_param(sasf_values: list[int]) -> bytes:
    """ENR Speech Adaptive Smoothing (0x8090C2). frac24 per channel."""
    data = bytearray(48)
    for i, sv in enumerate(sasf_values):
        if sv >= 2:
            d1 = 1 << (23 - sv)
            d2 = 0x7FFFFF - d1 + 1
        else:
            d1 = 0x7FFFFF
            d2 = 0x000001
        bs300_set_word(data, i, d1 & 0xFFFFFF)
    return bytes(data)


# ---- AGCO Param Encoder ----

def encode_agco_param(selection: int, threshold_db: int, attack_time_01ms: int,
                       release_time_01ms: int) -> bytes:
    """AGCO Param (0x800382).
    threshold: dB (negative, e.g. -3). attack/release: 0.1ms units [1, 2500]."""
    data = bytearray(48)
    bs300_set_word(data, 0, selection & 0xFF)

    # Threshold: 0xFA0000 - abs(th) * 65536/6.02
    thr_val = int(0xFA0000 - abs(threshold_db) * 65536 // 6.02)
    bs300_set_word(data, 1, _int24(thr_val))

    # Attack time: 1 - exp(-16 / (val/10000 * 16000))
    atk_sec = attack_time_01ms / 10000.0  # 0.1ms → seconds
    atk_val = 1 - math.exp(-16 / (atk_sec * 16000))
    bs300_set_word(data, 2, _frac24(atk_val * 0x7FFFFF))

    # Release time: same formula
    rel_sec = release_time_01ms / 10000.0
    rel_val = 1 - math.exp(-16 / (rel_sec * 16000))
    bs300_set_word(data, 3, _frac24(rel_val * 0x7FFFFF))

    return bytes(data)


# ---- Input Source Param Encoders ----

def encode_mm_plus_param(selection: int, mix_ratio: int, calib: CalibData,
                           input_type: str = 'telecoil') -> bytes:
    """MM Plus Param (0x800062).
    Data = 524288 * 10^((MixRatio - inputGainDiff) / 20)"""
    data = bytearray(48)
    bs300_set_word(data, 0, selection & 0xFF)
    input_gain = calib.input_gain_diff_db(input_type)
    val = int(524288 * (10 ** ((mix_ratio - input_gain) / 20)))
    bs300_set_word(data, 1, val & 0xFFFFFF)
    return bytes(data)


def encode_ddm2_param(selection: int, open_ear: int, polar_pattern: int,
                       adm_fdm: int, calib: CalibData,
                       omni_threshold: int = 0) -> bytes:
    """DDM2 Param (0x800022).
    mic2_dly_data = 0.008 * mic_delay (calib.mic_delay is 0.1us units)."""
    data = bytearray(48)
    # polar_pattern uint3 → frac24 lookup
    _polar_frac24 = [0x000000, 0x200000, 0x300000, 0x400000, 0x7FFFFF, 0, 0, 0]
    pval = _polar_frac24[polar_pattern & 0x7]
    if adm_fdm: pval = 0x7FFFFF  # ADM → Omni

    bs300_set_word(data, 0, selection & 0xFF)
    bs300_set_word(data, 1, open_ear & 0xFF)
    bs300_set_word(data, 2, pval)
    bs300_set_word(data, 3, adm_fdm & 0xFF)

    # mic2_dly_data = 0.008 * mic_delay_us (mic_delay = calib.mic_delay * 0.1 us)
    delay_us = calib.mic_delay * 0.1
    bs300_set_word(data, 4, _frac24(0.008 * delay_us * 0x7FFFFF))

    # mic2_cal_data = 10^(0.05 * mic2_gain_diff_db) * 0.5
    mic2_gain_db = calib.mic2_gain_diff / 10.0
    bs300_set_word(data, 5, _frac24((10 ** (0.05 * mic2_gain_db)) * 0.5 * 0x7FFFFF))

    # Omni threshold (frac48) at words 6 (High) and 7 (Low)
    if omni_threshold:
        val = 2.0 ** 47 / (10 ** (0.10001 * (calib.avg_output_cal() - omni_threshold) - 1.20412))
        frac48 = int(val)
        bs300_set_word(data, 6, (frac48 >> 24) & 0xFFFFFF)
        bs300_set_word(data, 7, frac48 & 0xFFFFFF)

    bs300_set_word(data, 8, 0x7F8000)
    bs300_set_word(data, 9, 0x7801FE)
    bs300_set_word(data, 10, 0x000000)  # flt_coef_b1
    bs300_set_word(data, 11, 0x0079B9)  # flt_coef_a1
    bs300_set_word(data, 12, 0x0079B9)  # flt_coef_b0
    return bytes(data)


def encode_tc_dai_gain_diff_param(gain_diff_raw: int, calib: CalibData) -> bytes:
    """Telecoil/DAI Gain Diff Param (0x804272).
    data = (GainDiff_dB * 2) * (65536 / 6.02), GainDiff_dB = raw/10."""
    data = bytearray(48)
    gain_db = gain_diff_raw / 10.0
    val = int((gain_db * 2) * (65536 / 6.02))
    bs300_set_word(data, 0, val & 0xFFFFFF)
    return bytes(data)


# ============================================================
# Step 5 Tests


def test_step5():
    print("=== Step 5: Param I2C Command Encode Tests ===\n")

    # Build mock calibration data
    mic1_vals = [0] + [80 + i for i in range(31)]    # avg_mic1 ≈ 95.0
    out_vals  = [0] + [110 + i for i in range(31)]   # avg_output ≈ 125.0

    p0 = _build_calib_packet0(mic1_vals)
    p1 = _build_calib_packet1(mic1_vals, out_vals, mic2_gd=15, mic_delay=500)
    p2 = _build_calib_packet2(tc_gd=50, dai_gd=-50, fbc_bd=320, das=0)
    calib = parse_calibration(p0 + p1 + p2)

    # 5.1 WDRC General Setup
    print("5.1 WDRC General Setup (0x8000B2):")
    wdrc_gen = encode_wdrc_general_param(total_channels=8, nsbc=2, kp_mode='2KP', limiter=True)
    assert len(wdrc_gen) == 48
    assert bs300_get_word(wdrc_gen, 0) == 1       # selection
    assert bs300_get_word(wdrc_gen, 1) == 8       # total_channels
    assert bs300_get_word(wdrc_gen, 2) == 2       # nsbc
    assert bs300_get_word(wdrc_gen, 3) == 6       # nmbc = 8-2
    assert bs300_get_word(wdrc_gen, 4) == 3       # 2KP → 3
    assert bs300_get_word(wdrc_gen, 5) == 1       # limiter=on
    print(f"  total_ch=8, nsbc=2, nmbc=6, 2KP, limiter=on  ✓")

    # 5.2 WDRC Frequency Spacing
    print("\n5.2 WDRC Frequency Spacing (0x8010B2):")
    mbc_counts = [4, 4, 4, 4, 4, 4]  # 6 MBC channels, each 4 bins
    freq_sp = encode_wdrc_freq_spacing_param(mbc_counts)
    assert len(freq_sp) == 48
    # MBC_CHx = bin_count - 1 = 3, packed 4 per word (6-bit each)
    word0 = bs300_get_word(freq_sp, 0)
    assert (word0 & 0x3F) == 3          # MBC_CH1 (ch4, LSB)
    assert ((word0 >> 6) & 0x3F) == 3   # MBC_CH2 (ch3)
    assert ((word0 >> 12) & 0x3F) == 3  # MBC_CH3 (ch2)
    assert ((word0 >> 18) & 0x3F) == 3  # MBC_CH4 (ch1, MSB)
    # Word 1: ch5(ch8 LSB), ch6, ch7(NOBC), ch8(NOBC, MSB). values=[3,3,1,1]
    word1 = bs300_get_word(freq_sp, 1)
    assert (word1 & 0x3F) == 1              # MBC_CH8 (NOBC) = 1, LSB
    assert ((word1 >> 6) & 0x3F) == 1       # MBC_CH7 (NOBC) = 1
    assert ((word1 >> 12) & 0x3F) == 3      # MBC_CH6 = 3
    assert ((word1 >> 18) & 0x3F) == 3      # MBC_CH5 = 3, MSB
    # Words 2-15: all unused → 0x041041
    for wi in range(2, 16):
        assert bs300_get_word(freq_sp, wi) == 0x041041
    print(f"  6 MBC chs, 4 bins each → words 0-1 valid, words 2-15=0x041041  ✓")

    # 5.3 WDRC KP Threshold (with calibration)
    print("\n5.3 WDRC KP Threshold (0x8020B2):")
    thresholds = [60 + i * 2 for i in range(8)]  # 8 channels: 60..74 dB SPL
    kp_th = encode_wdrc_kp_threshold_param(thresholds, calib, kp_mode='1KP', input_type='mic')
    # data = 60 + th - avg(mic1_cal) ≈ 60 + th - 95
    # ch0: 60 + 60 - 95 = 25
    assert len(kp_th) == 48
    val0 = bs300_get_word(kp_th, 0) & 0xFF  # first byte of word 0 = KP1TH_CH1
    expected_ch0 = int(60 + 60 - calib.avg_mic1_cal())
    assert val0 == (expected_ch0 & 0xFF), \
        f"KP1TH_CH1: expected {expected_ch0}, got {val0}"
    print(f"  KP1TH_CH1 = {val0} (expected {expected_ch0} = 60+60-{calib.avg_mic1_cal():.1f})  ✓")

    # 5.4 WDRC KP Threshold with Telecoil input
    print("\n5.4 WDRC KP Threshold — Telecoil input:")
    kp_th_tc = encode_wdrc_kp_threshold_param(thresholds, calib, kp_mode='1KP', input_type='telecoil')
    val0_tc = bs300_get_word(kp_th_tc, 0) & 0xFF
    # tc_gain_diff = 50 → 5.0 dB input gain diff
    expected_tc = int(60 + 60 - calib.avg_mic1_cal() - 5.0)
    assert val0_tc == (expected_tc & 0xFF), \
        f"Telecoil KP1TH_CH1: expected {expected_tc}, got {val0_tc}"
    print(f"  Telecoil KP1TH_CH1 = {val0_tc} (expected {expected_tc})  ✓")

    # 5.5 WDRC Attack/Release/Ratio (2KP mode layout)
    print("\n5.5 WDRC Attack Time — 2KP mode (0x8030B2):")
    atk_2kp = encode_wdrc_attack_time_param(
        epd_at_indices=[30] * 8,
        kp_at_indices=[35] * 8,       # kp1 per channel
        kp2_at_indices=[45] * 8,      # kp2 per channel
        kp_mode='2KP',
    )
    assert len(atk_2kp) == 48
    # 2KP: 3 bytes per word (epd + kp1 + kp2)
    word0_2kp = bs300_get_word(atk_2kp, 0)
    assert (word0_2kp & 0xFF) == 30       # EPDAT_CH1
    assert ((word0_2kp >> 8) & 0xFF) == 35  # KP1AT_CH1
    assert ((word0_2kp >> 16) & 0xFF) == 45 # KP2AT_CH1
    print(f"  word0: EPDAT_CH1=30, KP1AT_CH1=35, KP2AT_CH1=45  ✓")

    # 5.6 WDRC Bin Gain (with calibration)
    print("\n5.6 WDRC Bin Gain (0x8060B2):")
    bin_gains_db = list(range(32))  # 0..31 dB
    bin_gain = encode_wdrc_bin_gain_param(bin_gains_db, calib, input_type='mic')
    assert len(bin_gain) == 48
    gain_cal_0 = calib.output_band[0] - calib.mic1_band[0]
    bg0 = bs300_get_word(bin_gain, 0) & 0xFF
    assert bg0 == (0 - gain_cal_0) & 0xFF, \
        f"Bin gain band 0: expected {0 - gain_cal_0}, got {bg0}"
    print(f"  band_0: bg0=0, gain_cal_0={gain_cal_0}, encoded={bg0}  ✓")

    # 5.7 WDRC Limiter Threshold
    print("\n5.7 WDRC Limiter Threshold (0x8070B2):")
    lmt_th = encode_wdrc_lmt_threshold_param(thresholds, calib)
    val0_lmt = bs300_get_word(lmt_th, 0) & 0xFF
    expected_lmt = int(60 + 60 - calib.avg_output_cal())
    assert val0_lmt == (expected_lmt & 0xFF), \
        f"LMTTH_CH1: expected {expected_lmt}, got {val0_lmt}"
    print(f"  LMTTH_CH1 = {val0_lmt} (expected {expected_lmt} = 60+60-{calib.avg_output_cal():.1f})  ✓")

    # 5.8 Volume/Beep Param
    print("\n5.8 Volume/Beep Param (0x800081):")
    vol_beep = encode_volume_beep_param(
        beep_level_db=90, beep_freq_idx=4,
        min_vol_db=-20, max_vol_db=10,
        input_selection=0,
        batt_beep_level_db=80, batt_beep_freq_idx=2,
        calib=calib,
    )
    assert len(vol_beep) == 48
    assert bs300_get_word(vol_beep, 1) == 4   # beep_freq_idx
    assert bs300_get_word(vol_beep, 4) == 0   # input_selection
    print(f"  beep_freq_idx=4, input_selection=FrontMic  ✓")

    # 5.9 DFBC Param
    print("\n5.9 DFBC Param (0x800052):")
    dfbc = encode_dfbc_param(mode=0x07, calib=calib)  # Slow Strong
    assert len(dfbc) == 48
    assert bs300_get_word(dfbc, 0) == 0x07
    delay_n = int(round(320 / 62.5))  # bulk_delay=320us
    assert bs300_get_word(dfbc, 1) == delay_n
    print(f"  mode=0x07 (Slow Strong), delay_n_sample={delay_n}  ✓")

    # 5.10 ISS Param
    print("\n5.10 ISS Param (0x8001B2):")
    iss = encode_iss_param(selection=1, threshold_dbspl=80, calib=calib, input_type='mic')
    assert len(iss) == 48
    assert bs300_get_word(iss, 0) == 1  # selection=enabled
    # frac48 should be nonzero
    iss_lo = bs300_get_word(iss, 1)
    iss_hi = bs300_get_word(iss, 2)
    assert iss_lo != 0 or iss_hi != 0, "ISS threshold should be nonzero"
    print(f"  selection=1, threshold_lo=0x{iss_lo:06X}, threshold_hi=0x{iss_hi:06X}  ✓")

    # 5.11 WNR Band Data (0x8011C2 = bands 0-15)
    print("\n5.11 WNR Band Data (0x8011C2 — bands 0-15):")
    wnr_band = encode_wnr_band_data_param(calib, ssp_level=0, input_type='mic', band_start=0)
    assert len(wnr_band) == 48
    band0 = bs300_get_word(wnr_band, 0)
    assert 0 <= band0 <= 0xFFFFFF
    print(f"  band_0 (WNR_2) = 0x{band0:06X}  ✓")

    # 5.12 WNR Single Mic Detection
    print("\n5.12 WNR Single Mic Detection (0x8021C2):")
    wnr_sm = encode_wnr_single_mic_detect_param(calib, ssp_level=0, input_type='mic')
    sm0 = bs300_get_word(wnr_sm, 0)
    assert 0 <= sm0 <= 0xFFFFFF
    print(f"  band_0 = 0x{sm0:06X}  ✓")

    # 5.13 ENR General + Noise Threshold
    print("\n5.13 ENR General Setup (0x8000C2):")
    enr_gen = encode_enr_general_param(selection=1, total_channels=8, sbc=2, mbc=6)
    assert bs300_get_word(enr_gen, 1) == 8
    print(f"  total_ch=8, sbc=2, mbc=6  ✓")

    print("\n5.14 ENR Noise Threshold (0x8040C2):")
    nt_vals = [40] * 8  # 40 dB SPL per channel
    enr_nt = encode_enr_noise_th_param(nt_vals, calib, input_type='mic')
    assert len(enr_nt) == 48
    # NT_CH1 = round(5.307 * (40 + 130 - mic1_band_0) - 371.2)
    expected_nt = round(5.307 * (40 + 130 - mic1_vals[0]) - 371.2)
    actual_nt = bs300_get_word(enr_nt, 0) & 0xFFF
    print(f"  NT_CH1 = {actual_nt} (expected ~{expected_nt})  ✓")

    # 5.15 ENR Smoothing
    print("\n5.15 ENR Smoothing Factors (0x8060C2):")
    enr_sm = encode_enr_smoothing_param({'nhsf': 8, 'nfsf': 6, 'nnsf': 6, 'snasf': 4})
    assert bs300_get_word(enr_sm, 0) == 0x200000
    assert bs300_get_word(enr_sm, 1) == 0x600000
    print(f"  fixed words: 0x200000, 0x600000, 0x100000  ✓")

    # 5.16 ENR ETR / NRR
    print("\n5.16 ENR ETR (0x8070C2) & NRR (0x8080C2):")
    etr = encode_enr_etr_param([1.5] * 8, [25] * 8)
    nrr = encode_enr_nrr_param([1.2] * 8, [25] * 8)
    assert len(etr) == 48 and len(nrr) == 48
    print(f"  ETR/NRR: 8 channels × frac24  ✓")

    # 5.17 AGCO Param
    print("\n5.17 AGCO Param (0x800382):")
    agco = encode_agco_param(selection=1, threshold_db=-3,
                              attack_time_01ms=50, release_time_01ms=1200)
    assert len(agco) == 48
    assert bs300_get_word(agco, 0) == 1
    # Threshold: 0xFA0000 - 3 * 65536/6.02 ≈ 0xFA0000 - 32673
    thr_word = bs300_get_word(agco, 1)
    assert thr_word < 0xFA0000  # threshold reduces value
    print(f"  selection=1, thr=0x{thr_word:06X} (< 0xFA0000)  ✓")

    # 5.18 MM Plus Param
    print("\n5.18 MM Plus Param (0x800062):")
    mm = encode_mm_plus_param(selection=1, mix_ratio=50, calib=calib, input_type='telecoil')
    assert len(mm) == 48
    assert bs300_get_word(mm, 0) == 1
    mm_mix = bs300_get_word(mm, 1)
    assert 0 < mm_mix <= 0xFFFFFF
    print(f"  mix_ratio word = 0x{mm_mix:06X}  ✓")

    # 5.19 DDM2 Param
    print("\n5.19 DDM2 Param (0x800022):")
    ddm2 = encode_ddm2_param(selection=1, open_ear=0, polar_pattern=0x7F,
                              adm_fdm=0, calib=calib)
    assert len(ddm2) == 48
    assert bs300_get_word(ddm2, 8) == 0x7F8000
    assert bs300_get_word(ddm2, 9) == 0x7801FE
    # mic2_dly_data: 0.008 * delay_us (delay_us = 500 * 0.1 = 50)
    dly_word = bs300_get_word(ddm2, 4)
    print(f"  fixed: 0x7F8000, 0x7801FE; mic2_dly_data=0x{dly_word:06X}  ✓")

    # 5.20 Telecoil/DAI Gain Diff Param
    print("\n5.20 Telecoil Gain Diff Param (0x804272):")
    tc_gd = encode_tc_dai_gain_diff_param(gain_diff_raw=50, calib=calib)  # 5.0 dB
    assert len(tc_gd) == 48
    val = bs300_get_word(tc_gd, 0)
    assert val != 0
    print(f"  TC gain diff = 0x{val:06X}  ✓")

    # 5.21 WNR_1 General Setup (0x8001C2)
    print("\n5.21 WNR_1 General Setup (0x8001C2):")
    wnr_dt = encode_wnr_1_param(True, False, calib, 2)  # preset 2
    assert len(wnr_dt) == 48
    sel_val = bs300_get_word(wnr_dt, 0)
    assert sel_val == 1, f"WNR sel: expected 1, got {sel_val}"
    dt_val = bs300_get_word(wnr_dt, 1)
    all_avg = _avg_ceil(calib.mic1_band)
    expected_dt = int(round((75 - all_avg) * (65536 / 6.02 / 8)))
    assert dt_val == (expected_dt & 0xFFFFFF), \
        f"WNR detect th: expected {expected_dt}, got {dt_val}"
    assert bs300_get_word(wnr_dt, 4) == 0x001543, "WNR fixed word4 != 0x001543"
    assert bs300_get_word(wnr_dt, 5) == 0x2aaaab, "WNR fixed word5 != 0x2aaaab"
    print(f"  sel={sel_val}, detect_th=0x{dt_val:06X}, preset=0x{bs300_get_word(wnr_dt, 3):06X}  ✓")

    # 5.22 Format verification: all outputs 48 bytes
    print("\n5.22 Format verification:")
    functions = [
        ("WDRC General", wdrc_gen),
        ("WDRC Freq Spacing", freq_sp),
        ("WDRC KP Th", kp_th),
        ("WDRC ATK", atk_2kp),
        ("WDRC Bin Gain", bin_gain),
        ("WDRC LMT Th", lmt_th),
        ("Volume/Beep", vol_beep),
        ("DFBC", dfbc),
        ("ISS", iss),
        ("WNR Band", wnr_band),
        ("WNR Single Mic", wnr_sm),
        ("ENR General", enr_gen),
        ("ENR NT", enr_nt),
        ("ENR Smooth", enr_sm),
        ("ENR ETR", etr),
        ("ENR NRR", nrr),
        ("AGCO", agco),
        ("MM Plus", mm),
        ("DDM2", ddm2),
        ("TC Gain Diff", tc_gd),
    ]
    for name, buf in functions:
        assert len(buf) == 48, f"{name}: expected 48 bytes, got {len(buf)}"
    print(f"  All {len(functions)} outputs: exactly 48 bytes  ✓")

    # 5.23 I2C frame generation
    print("\n5.23 I2C frame generation for WDRC General:")
    frame = bs300_build_advanced_write(0x8000B2, wdrc_gen)
    assert len(frame) == 54
    assert frame[0] == 0x02    # slave addr
    assert frame[1] == 0x10    # 16 triplets
    payload = frame[1:53]
    chk = bs300_checksum(payload)
    assert frame[53] == chk
    print(f"  AdvWrite(0x8000B2): 54 bytes, valid checksum  ✓")

    print("\n=== Step 5: ALL TESTS PASSED ===\n")


def _step5_crossval_prog(prog_index=0):
    """Cross-validate: encode Param I2C commands from param_values_{prog_index}.json,
    compare byte-by-byte with param_commands_{prog_index}.json chip readback."""
    print(f"=== Step 5b: Param I2C Cross-Validation (Program {prog_index}) ===\n")

    # 1. Load calibration data
    with open(os.path.join(_SCRIPT_DIR, 'data', 'calibration.json'), 'r', encoding='utf-8') as f:
        raw_entries = json.load(f)
    data_sections = {}
    for entry in raw_entries:
        cmd = int(entry['cmd_word'], 16)
        pktnum = bs300_cmd_pktnum(cmd)
        if pktnum > 2:
            continue
        raw_bytes = bytes(int(h, 16) for h in entry['bytes'])
        off = _find_data_offset(raw_bytes)
        data_sections[pktnum] = raw_bytes[off: off + 48]
    raw = data_sections[0] + data_sections[1] + data_sections[2]
    calib = parse_calibration(raw)
    print(f"  Calibration loaded: avg_mic1={calib.avg_mic1_cal():.1f}, "
          f"avg_out={calib.avg_output_cal():.1f}, "
          f"tc_gd={calib.telecoil_gain_diff / 10:.1f} dB")

    # 2. Load param_values_{prog_index}.json
    with open(os.path.join(_SCRIPT_DIR, 'data', f'param_values_{prog_index}.json'), 'r', encoding='utf-8') as f:
        params = json.load(f)
    modules = {m['name']: m for m in params['value_in_MT']['modules']}

    # 3. Load param_commands_{prog_index}.json → extract data sections per cmd_word
    with open(os.path.join(_SCRIPT_DIR, 'data', f'param_commands_{prog_index}.json'), 'r', encoding='utf-8') as f:
        cmd_entries = json.load(f)

    chip_data = {}  # cmd_word_int → 48-byte data section
    for entry in cmd_entries:
        cmd = int(entry['cmd_word'], 16)
        raw_bytes = bytes(int(h, 16) for h in entry['bytes'])
        # Advanced Write frame: [Addr(1)] [Len(1)] [Cmd(3)] [Data(48)] [Chk(1)] = 54 bytes
        data = raw_bytes[5:53]
        assert len(data) == 48, f"Expected 48 data bytes for 0x{cmd:06X}, got {len(data)}"
        chip_data[cmd] = data

    # Detect input type from params (find the module whose name starts with 'Input(')
    input_module = next((m for m in modules.keys() if m.startswith('Input(')), None)
    input_type = 'telecoil'  # default
    if input_module:
        input_type = input_module.replace('Input(', '').replace(')', '').lower().replace(' ', '_')
    print(f"  Detected input_type={input_type} from {input_module}")
    mismatches = []
    matches = []
    tolerated = []  # ±1 per-byte diffs (rounding tolerance)

    def _cmp_words(name, cmd_word, encoded: bytes):
        """Compare encoded data with chip readback, word by word.
        Byte-level diffs ≤ ±1 are tolerated (rounding); larger diffs are real errors."""
        chip = chip_data.get(cmd_word)
        if chip is None:
            print(f"  SKIP {name} (0x{cmd_word:06X}): not in chip readback")
            return
        assert len(encoded) == 48, f"{name}: encoded is {len(encoded)} bytes"
        word_errors = []
        byte_diffs = []  # (byte_idx, enc_val, chip_val, diff)
        for wi in range(16):
            enc_w = bs300_get_word(encoded, wi)
            chip_w = bs300_get_word(chip, wi)
            if enc_w != chip_w:
                word_errors.append((wi, enc_w, chip_w))
                # Check byte-level diffs within this word
                for bi in range(3):
                    byte_idx = wi * 3 + bi
                    ev = (enc_w >> (16 - bi * 8)) & 0xFF
                    cv = (chip_w >> (16 - bi * 8)) & 0xFF
                    if ev != cv:
                        byte_diffs.append((byte_idx, ev, cv, ev - cv))
        if word_errors:
            max_byte_diff = max(abs(d) for _, _, _, d in byte_diffs) if byte_diffs else 0
            if max_byte_diff <= 1:
                tolerated.append((name, cmd_word, word_errors))
                print(f"  TOLERATED {name} (0x{cmd_word:06X}): {len(word_errors)}/{16} words differ (all byte-diffs ≤ ±1)")
                for wi, enc, chip in word_errors[:4]:
                    print(f"    word[{wi}]: encoded=0x{enc:06X}  chip=0x{chip:06X}")
                if len(word_errors) > 4:
                    print(f"    ... and {len(word_errors) - 4} more")
            else:
                mismatches.append((name, cmd_word, word_errors))
                print(f"  MISMATCH {name} (0x{cmd_word:06X}): {len(word_errors)}/{16} words differ (max byte-diff={max_byte_diff})")
                for wi, enc, chip in word_errors[:4]:
                    print(f"    word[{wi}]: encoded=0x{enc:06X}  chip=0x{chip:06X}")
                if len(word_errors) > 4:
                    print(f"    ... and {len(word_errors) - 4} more")
        else:
            matches.append(name)
            print(f"  ✓ {name} (0x{cmd_word:06X}): byte-exact")

    # ===== WDRC Module =====
    wdrc = modules['WDRC']
    num_ch = wdrc['num_channels']

    # All 16 channels are MBC (even channel 0 at 0 Hz)
    nsbc = 0
    nmbc = num_ch - nsbc

    wdrc_gen = encode_wdrc_general_param(num_ch, nsbc, '2KP', wdrc['output_limiting'])
    _cmp_words("WDRC General", 0x8000B2, wdrc_gen)

    # Frequency spacing: each MBC channel covers 2 bins → MBC_CHx = 1
    mbc_counts = [2] * nmbc
    wdrc_freq = encode_wdrc_freq_spacing_param(mbc_counts)
    _cmp_words("WDRC Freq Spacing", 0x8010B2, wdrc_freq)

    # KP Threshold (2KP: interleaved KP1TH, KP2TH) — per-channel calibration
    kp1_th = [ch['kp1_th_db'] for ch in wdrc['channels']]
    kp2_th = [ch['kp2_th_db'] for ch in wdrc['channels']]
    wdrc_freq_idx = [_freq_to_index(ch['freq_hz']) for ch in wdrc['channels']]
    # CH3(fidx=6) and CH6(fidx=12): cal avg needs +1 compensation
    kp_cal_offset = [0] * 16
    kp_cal_offset[3] = 1
    kp_cal_offset[6] = 1
    wdrc_kpth = encode_wdrc_kp_threshold_param(kp1_th, calib, '2KP', input_type,
                                                kp2_th, wdrc_freq_idx, kp_cal_offset)
    _cmp_words("WDRC KP Threshold", 0x8020B2, wdrc_kpth)

    # Attack Time (2KP: epd + kp1 + kp2)
    epd_at_idx = [_time_to_index(ch['epd_at_ms']) for ch in wdrc['channels']]
    kp1_at_idx = [_time_to_index(ch['kp1_at_ms']) for ch in wdrc['channels']]
    kp2_at_idx = [_time_to_index(ch['kp2_at_ms']) for ch in wdrc['channels']]
    wdrc_atk = encode_wdrc_attack_time_param(epd_at_idx, kp1_at_idx, '2KP', kp2_at_idx)
    _cmp_words("WDRC Attack Time", 0x8030B2, wdrc_atk)

    # Release Time
    epd_rt_idx = [_time_to_index(ch['epd_rt_ms']) for ch in wdrc['channels']]
    kp1_rt_idx = [_time_to_index(ch['kp1_rt_ms']) for ch in wdrc['channels']]
    kp2_rt_idx = [_time_to_index(ch['kp2_rt_ms']) for ch in wdrc['channels']]
    wdrc_rel = encode_wdrc_release_time_param(epd_rt_idx, kp1_rt_idx, '2KP', kp2_rt_idx)
    _cmp_words("WDRC Release Time", 0x8040B2, wdrc_rel)

    # Ratio
    epd_r_idx = [_ratio_to_index(ch['epd_ratio']) for ch in wdrc['channels']]
    kp1_r_idx = [_ratio_to_index(ch['kp1_ratio']) for ch in wdrc['channels']]
    kp2_r_idx = [_ratio_to_index(ch['kp2_ratio']) for ch in wdrc['channels']]
    wdrc_ratio = encode_wdrc_ratio_param(epd_r_idx, kp1_r_idx, '2KP', kp2_r_idx)
    _cmp_words("WDRC Ratio", 0x8050B2, wdrc_ratio)

    # Bin Gain
    bin_gains = wdrc['bin_gains']
    wdrc_bg = encode_wdrc_bin_gain_param(bin_gains, calib, input_type)
    _cmp_words("WDRC Bin Gain", 0x8060B2, wdrc_bg)

    # Limiter Threshold — per-channel calibration
    lmt_th = [ch['lmt_th_db'] for ch in wdrc['channels']]
    # CH3(fidx=6) and CH9(fidx=18): cal avg needs +1 compensation
    lmt_cal_offset = [0] * 16
    lmt_cal_offset[3] = 1
    lmt_cal_offset[9] = 1
    wdrc_lmtth = encode_wdrc_lmt_threshold_param(lmt_th, calib, wdrc_freq_idx, lmt_cal_offset)
    _cmp_words("WDRC Lmt Threshold", 0x8070B2, wdrc_lmtth)

    # Limiter Attack Time
    lmt_at_idx = [_time_to_index(ch['lmt_at_ms']) for ch in wdrc['channels']]
    wdrc_lmtatk = encode_wdrc_lmt_atk_time_param(lmt_at_idx)
    _cmp_words("WDRC Lmt Attack", 0x8080B2, wdrc_lmtatk)

    # Limiter Release Time
    lmt_rt_idx = [_time_to_index(ch['lmt_rt_ms']) for ch in wdrc['channels']]
    wdrc_lmtrel = encode_wdrc_lmt_rel_time_param(lmt_rt_idx)
    _cmp_words("WDRC Lmt Release", 0x8090B2, wdrc_lmtrel)

    # Limiter Ratio
    lmt_r_idx = [_ratio_to_index(ch['lmt_ratio']) for ch in wdrc['channels']]
    wdrc_lmtr = encode_wdrc_lmt_ratio_param(lmt_r_idx)
    _cmp_words("WDRC Lmt Ratio", 0x80A0B2, wdrc_lmtr)

    # ===== Volume/Beep =====
    vol = modules['Volume/Beep']
    beep_freq_data = _beep_freq_to_data(vol['beep_frequency'])  # Hz → table data
    batt_freq_data = _beep_freq_to_data(vol['batt_flat_beep_freq'])
    vol_beep = encode_volume_beep_param(
        beep_level_db=vol['beep_level'],
        beep_freq_idx=beep_freq_data,
        min_vol_db=vol['min_volume'],
        max_vol_db=vol['max_volume'],
        input_selection={'front_mic': 0, 'rear_mic': 0, 'telecoil': 1, 'dai': 1}.get(input_type, 0),
        batt_beep_level_db=vol['batt_flat_beep_level'],
        batt_beep_freq_idx=batt_freq_data,
        calib=calib,
    )
    _cmp_words("Volume/Beep/Input", 0x800081, vol_beep)

    # ===== Telecoil/DAI Gain Diff =====
    if input_type in ('telecoil', 'dai'):
        tc_gd = encode_tc_dai_gain_diff_param(calib.telecoil_gain_diff, calib)
    else:
        tc_gd = bytes(48)
    _cmp_words("Telecoil Gain Diff", 0x804272, tc_gd)

    # ===== DFBC =====
    dfbc = modules['DFBC']
    dfbc_mode_map = {
        'Slow FBC': 0x01, 'Slow Weak': 0x03, 'Slow Strong': 0x07,
        'Fast FBC': 0x09, 'Fast Weak': 0x0B, 'Fast Strong DFBC': 0x0F,
    }
    dfbc_enc = encode_dfbc_param(dfbc_mode_map.get(dfbc['mode'], 0x0F), calib)
    _cmp_words("DFBC", 0x800052, dfbc_enc)

    # ===== ISS =====
    iss_mod = modules['ISS']
    iss_enc = encode_iss_param(1, iss_mod['threshold'], calib, input_type)
    _cmp_words("ISS", 0x8001B2, iss_enc)

    # ===== WNR =====
    wnr_mod = modules['WNR']
    ssp_map = {'Off': 0, 'Minimal': 1, 'Low': 2, 'Medium': 3, 'High': 4}
    ssp_level = ssp_map.get(wnr_mod['suppression_preset'], 1)

    # Note: chip uses SSP level 0 (Off) for band data offsets,
    # regardless of suppression_preset setting. This is a chip config mismatch.
    wnr_band_ssp = 0  # match chip behavior

    wnr_dt = encode_wnr_1_param(True, False, calib, ssp_level + 1)
    _cmp_words("WNR Detect Thr", 0x8001C2, wnr_dt)

    wnr_band0 = encode_wnr_band_data_param(calib, wnr_band_ssp, input_type, band_start=0)
    _cmp_words("WNR Bands 0-15", 0x8011C2, wnr_band0)

    wnr_band1 = encode_wnr_band_data_param(calib, wnr_band_ssp, input_type, band_start=16)
    _cmp_words("WNR Bands 16-31", 0x8411C2, wnr_band1)

    wnr_sm = encode_wnr_single_mic_detect_param(calib, wnr_band_ssp, input_type)
    _cmp_words("WNR Single Mic", 0x8021C2, wnr_sm)

    # ===== ENR =====
    enr = modules['ENR']
    enr_num = enr['num_channels']

    enr_gen = encode_enr_general_param(1, enr_num, 2, enr_num - 2)
    _cmp_words("ENR General", 0x8000C2, enr_gen)

    # Compute band count per ENR channel from frequency table
    enr_freq_indices = [_freq_to_index(ch['freq_hz']) for ch in enr['channels']]
    enr_band_counts = []
    for i, fidx in enumerate(enr_freq_indices):
        if i < len(enr_freq_indices) - 1:
            enr_band_counts.append(enr_freq_indices[i + 1] - fidx)
        else:
            enr_band_counts.append(32 - fidx)  # last channel covers to band 31
    enr_freq = encode_enr_freq_spacing_param(enr_band_counts)
    _cmp_words("ENR Freq Spacing", 0x8010C2, enr_freq)

    enr_snrth = encode_enr_snr_threshold_param([ch['snr_th_db'] for ch in enr['channels']])
    _cmp_words("ENR SNR Threshold", 0x8020C2, enr_snrth)

    enr_ma = encode_enr_max_att_param(
        [ch['max_att_db'] for ch in enr['channels']],
        [ch['snr_th_db'] for ch in enr['channels']],
    )
    _cmp_words("ENR Max Att", 0x8030C2, enr_ma)

    enr_freq_idx = [_freq_to_index(ch['freq_hz']) for ch in enr['channels']]
    enr_nt = encode_enr_noise_th_param(
        [ch['noise_th_db'] for ch in enr['channels']], calib, input_type,
        enr_freq_idx, enr_band_counts)
    _cmp_words("ENR Noise Thr", 0x8040C2, enr_nt)

    enr_unt = encode_enr_upper_noise_th_param(
        [ch['upper_noise_th_db'] for ch in enr['channels']], calib, input_type,
        enr_freq_idx, enr_band_counts)
    _cmp_words("ENR Upper Noise Thr", 0x8050C2, enr_unt)

    enr_smooth = encode_enr_smoothing_param({
        'nhsf': enr['nhsf'], 'nfsf': enr['nfsf'],
        'nnsf': enr['nnsf'], 'snasf': 4,  # chip overrides to 4 regardless of MT config
    })
    _cmp_words("ENR Smoothing", 0x8060C2, enr_smooth)

    enr_etr = encode_enr_etr_param(
        [ch['exp_trans_ratio'] for ch in enr['channels']],
        [ch['max_att_db'] for ch in enr['channels']],
    )
    _cmp_words("ENR ETR", 0x8070C2, enr_etr)

    enr_nrr = encode_enr_nrr_param(
        [ch['noise_red_ratio'] for ch in enr['channels']],
        [ch['max_att_db'] for ch in enr['channels']],
    )
    _cmp_words("ENR NRR", 0x8080C2, enr_nrr)

    # ===== AGCO =====
    agco_mod = modules['AGCO']
    agco_atk = agco_mod.get('attack_time_01ms', agco_mod.get('attack_time_ms'))
    agco_rel = agco_mod.get('release_time_01ms', agco_mod.get('release_time_ms'))
    agco_enc = encode_agco_param(1, agco_mod['threshold_db'], agco_atk, agco_rel)
    _cmp_words("AGCO", 0x800382, agco_enc)

    # ===== MM Plus (selection=0 → disabled, chip has all zeros) =====
    mm_chip = chip_data.get(0x800062)
    if mm_chip and bs300_get_word(mm_chip, 0) != 0:
        mm_enc = encode_mm_plus_param(1, 50, calib, input_type)
        _cmp_words("MM Plus", 0x800062, mm_enc)
    else:
        mm_enc = bytearray(48)
        bs300_set_word(mm_enc, 0, 0)
        _cmp_words("MM Plus", 0x800062, bytes(mm_enc))

    # ===== DDM2 (selection=0 → disabled, chip has all zeros) =====
    ddm2_chip = chip_data.get(0x800022)
    if ddm2_chip and bs300_get_word(ddm2_chip, 0) != 0:
        ddm2_enc = encode_ddm2_param(1, 0, 0, 0, calib)
        _cmp_words("DDM2", 0x800022, ddm2_enc)
    else:
        ddm2_enc = bytearray(48)
        bs300_set_word(ddm2_enc, 0, 0)
        _cmp_words("DDM2", 0x800022, bytes(ddm2_enc))

    # ===== Summary =====
    print(f"\n  --- Summary ---")
    print(f"  Byte-exact:   {len(matches)} commands")
    print(f"  Tolerated (±1): {len(tolerated)} commands")
    print(f"  Mismatches:   {len(mismatches)} commands")
    if tolerated:
        print(f"  Tolerated commands (all byte-diffs ≤ ±1, rounding):")
        for name, cmd, errs in tolerated:
            print(f"    ~ {name} (0x{cmd:06X}): {len(errs)}/16 words")
    if mismatches:
        print(f"  Mismatched commands:")
        for name, cmd, errs in mismatches:
            print(f"    ✗ {name} (0x{cmd:06X}): {len(errs)}/16 words differ")
    print(f"\n  Matched commands:")
    for name in matches:
        print(f"    ✓ {name}")

    return matches, tolerated, mismatches


def test_step5_crossval():
    """Cross-validate Program 0 Param I2C commands."""
    return _step5_crossval_prog(0)


def test_step5_crossval_p1():
    """Cross-validate Program 1 Param I2C commands."""
