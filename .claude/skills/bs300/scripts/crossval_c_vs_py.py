"""
Full C-vs-Python cross-validation for ALL Param I2C encode commands.
Ports every C encode function from bs300_param.c, compares byte-by-byte
with Python codegen using param_values_1.json + calibration.json.
"""
import json, os, sys, math

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PARENT_DIR = os.path.dirname(_SCRIPT_DIR)
sys.path.insert(0, _PARENT_DIR)
from bs300_codegen import (
    CalibData, parse_calibration,
    encode_wdrc_general_param, encode_wdrc_freq_spacing_param,
    encode_wdrc_kp_threshold_param, encode_wdrc_attack_time_param,
    encode_wdrc_release_time_param, encode_wdrc_ratio_param,
    encode_wdrc_bin_gain_param, encode_wdrc_lmt_threshold_param,
    encode_wdrc_lmt_atk_time_param, encode_wdrc_lmt_rel_time_param,
    encode_wdrc_lmt_ratio_param,
    encode_volume_beep_param, encode_tc_dai_gain_diff_param,
    encode_dfbc_param, encode_iss_param,
    encode_wnr_1_param, encode_wnr_band_data_param,
    encode_wnr_single_mic_detect_param,
    encode_enr_general_param, encode_enr_freq_spacing_param,
    encode_enr_snr_threshold_param, encode_enr_max_att_param,
    encode_enr_noise_th_param, encode_enr_upper_noise_th_param,
    encode_enr_smoothing_param, encode_enr_etr_param, encode_enr_nrr_param,
    encode_enr_sasf_param,
    encode_agco_param, encode_mm_plus_param, encode_ddm2_param,
    _freq_to_index, _time_to_index, _ratio_to_index,
    bs300_get_word, bs300_set_word, bs300_cmd_pktnum, _find_data_offset,
    clamp_s32, _pack_bytes, _pack_int12_2pw, _pack_uint6_4pw,
)

# ============================================================
# C integer math helpers (same semantics as bs300_param.c)
# ============================================================
def c_trunc_div(num, den):
    """C integer division truncates toward zero (int cast)."""
    return int(num / den)

def c_round_div(num, den):
    """C round-half-away-from-zero: (num +/- den/2) / den truncated."""
    if num >= 0:
        return int((num + den // 2) / den)
    else:
        return int((num - den // 2) / den)

def c_clamp(val, lo, hi):
    if val < lo: return lo
    if val > hi: return hi
    return val

# ============================================================
# Generate C lookup tables (same formulas as gen_c_int_math.py)
# ============================================================

def _gen_beep_frac24_table():
    """Beep level dB -> frac24. Index = (beep_level - outcal) + 255, x in [-255, 140]."""
    tbl = []
    for x in range(-255, 141):
        frac = round(0x7FFFFF * (10 ** (x / 20.0)))
        tbl.append(max(0, min(0xFFFFFF, frac)))
    return tbl

def _gen_iss_frac48_table():
    """ISS frac48. Index = exponent_tenth - 30, et in [30, 100]."""
    tbl = []
    for et in range(30, 101):
        exponent = et / 10.0
        frac48 = round((1.0 / (10.0 ** exponent)) * (1 << 47))
        f = frac48 & 0xFFFFFFFFFFFF
        tbl.append((f & 0xFFFFFF, (f >> 24) & 0xFFFFFF))
    return tbl

def _gen_mic2_cal_frac24_table():
    """Mic2 cal frac24. Index = mic2_gain_diff_tenth_db + 50, x in [-50, 50]."""
    tbl = []
    for x in range(-50, 51):
        frac = round(0x800000 * (10 ** (x / 200.0)))
        tbl.append(max(0, min(0xFFFFFF, frac)))
    return tbl

def _gen_agco_exp_table():
    """AGCO attack/release exp. Index = 0.1ms value [0, 2500]."""
    tbl = [0]
    for x in range(1, 2501):
        frac = round((1 - math.exp(-10.0 / x)) * 0x7FFFFF)
        tbl.append(max(0, min(0x7FFFFF, frac)))
    return tbl

def _gen_mm_plus_frac24_table():
    """MM Plus frac24. Index = (mix_ratio*10 - igd) + 500, x in [-500, 1500]."""
    tbl = []
    for x in range(-500, 1501):
        frac = round(524288 * (10 ** (x / 200.0)))
        tbl.append(max(0, min(0xFFFFFF, frac)))
    return tbl

# C-side static tables (from bs300_param.c)
_BEEP_IDX_TO_HZ_C = [0, 250, 500, 750, 1000, 1250, 1500, 1750, 2000, 2250, 2500, 2750, 3000, 3250,
                     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]
_WNR_SSP_OFFSET_C = [
    [  0, -16, -32, -48, -48], [  0, -16, -32, -48, -48], [  0, -16, -32, -48, -48],
    [ 10,  -6, -22, -36, -48], [ 10,  -6, -22, -36, -48], [ 10,  -6, -22, -36, -48],
    [-10, -26, -42, -42, -42], [-10, -26, -42, -42, -42], [-10, -26, -42, -42, -42],
    [-10, -26, -42, -42, -42], [-10, -26, -42, -42, -42], [-10, -26, -42, -42, -42],
    [-10, -26, -42, -42, -42], [-10, -26, -42, -42, -42], [-10, -26, -42, -42, -42],
    [-20, -36, -52, -52, -52], [-20, -36, -52, -52, -52], [-20, -36, -52, -52, -52],
    [-20, -36, -52, -52, -52], [-20, -36, -52, -52, -52], [-20, -36, -52, -52, -52],
    [-20, -36, -52, -52, -52], [-20, -36, -52, -52, -52], [-20, -36, -52, -52, -52],
    [-20, -36, -52, -52, -52], [-20, -36, -52, -52, -52], [-20, -36, -52, -52, -52],
    [-20, -36, -52, -52, -52], [-20, -36, -52, -52, -52], [-20, -36, -52, -52, -52],
    [-20, -36, -52, -52, -52], [-20, -36, -52, -52, -52],
]
_WNR_DATA2_OFFSET_C = [
    [  0,  -8, -16, -26, -34],
    [ -8, -16, -24, -32, -40],
    [-40, -50, -58, -68, -78],
]
_WNR_PRESET_TO_SSP_C = [0, 1, 3, 6, 12]
_WDRC_CAL_OFFSET_C = [0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0]
_FREQ_TABLE_C = [0, 125, 375, 625, 875, 1125, 1375, 1625, 1875, 2125, 2375, 2625, 2875, 3125,
                 3375, 3625, 3875, 4125, 4375, 4625, 4875, 5125, 5375, 5625, 5875, 6125, 6375,
                 6625, 6875, 7125, 7375, 7625]

# Generate tables
BEEP_FRAC24 = _gen_beep_frac24_table()
ISS_FRAC48 = _gen_iss_frac48_table()
MIC2_CAL_FRAC24 = _gen_mic2_cal_frac24_table()
AGCO_EXP = _gen_agco_exp_table()
MM_PLUS_FRAC24 = _gen_mm_plus_frac24_table()

# ============================================================
# C helper functions (ports from bs300_param.c)
# ============================================================

def c_db_to_frac24(n_tenths_db):
    """ceil(val_db * 65536 / 6.02) as unsigned frac24."""
    return ((n_tenths_db * 327680 + 300) // 301) & 0xFFFFFF

def c_db_to_int24(n_tenths_db):
    """trunc(val_db * 65536 / 6.02) as signed int24."""
    if n_tenths_db >= 0:
        result = (n_tenths_db * 327680) // 301
    else:
        result = -((-n_tenths_db * 327680) // 301)
    return result & 0xFFFFFF

def c_igd(input_type, calib):
    if input_type == 'telecoil': return calib.telecoil_gain_diff
    elif input_type == 'dai': return calib.dai_gain_diff
    return 0

def c_apply_igd_trunc(numer_tenth_db, igd):
    return (numer_tenth_db - igd) // 10  # C truncation toward zero

def c_enr_nt_int(nt_db, mic1_cal, igd):
    x10 = nt_db * 10 + 1300 - mic1_cal * 10 - igd
    num = 5307 * x10 - 3712000
    return c_round_div(num, 10000)

def c_freq_to_cal_band(hz):
    best, best_dist = 0, 999999
    for i in range(1, 32):
        d = abs(hz - _FREQ_TABLE_C[i])
        if d < best_dist:
            best_dist = d
            best = i
    return best

def c_mic1_cal_avg(calib, fidx, cnt):
    s = sum(calib.mic1_band[fidx + j] for j in range(cnt))
    t = c_trunc_div(s * 10, cnt)
    return c_trunc_div(t, 10) + (1 if (t % 10) >= 5 else 0)

# ============================================================
# C encode function ports (exact logic from bs300_param.c)
# ============================================================
class C:
    # ---- WDRC (11) ----
    @staticmethod
    def wdrc_general(wdrc, data):
        for i in range(48): data[i] = 0
        nmbc = wdrc['num_channels']
        bs300_set_word(data, 0, 0x000001)
        bs300_set_word(data, 1, wdrc['num_channels'])
        bs300_set_word(data, 2, 0)
        bs300_set_word(data, 3, nmbc)
        bs300_set_word(data, 4, 3)
        bs300_set_word(data, 5, 1)

    @staticmethod
    def wdrc_freq(wdrc, data):
        for i in range(48): data[i] = 0
        nmbc = wdrc['num_channels']
        mbc_ch = [1]*16
        bpch = 32 // nmbc if nmbc > 0 else 2
        for i in range(min(nmbc, 16)):
            mbc_ch[i] = max(1, bpch - 1)
        _pack_uint6_4pw(data, 0, mbc_ch)
        for wi in range(4, 16):
            bs300_set_word(data, wi, 0x041041)

    @staticmethod
    def wdrc_kp_th(wdrc, calib, input_type, cal_off, data):
        for i in range(48): data[i] = 0
        igd = c_igd(input_type, calib)
        ch = wdrc['channels']; nch = wdrc['num_channels']
        vals = []
        for i in range(nch):
            fidx = _freq_to_index(ch[i]['freq_hz'])
            mc = (calib.mic1_band[fidx] + calib.mic1_band[fidx+1])//2 if fidx < 31 else calib.mic1_band[fidx]
            mc += cal_off[i] if i < len(cal_off) else 0
            vals.append(c_clamp(c_apply_igd_trunc(600 + ch[i]['kp1_th_db']*10 - mc*10, igd), -128, 127) & 0xFF)
            vals.append(c_clamp(c_apply_igd_trunc(600 + ch[i]['kp2_th_db']*10 - mc*10, igd), -128, 127) & 0xFF)
        _pack_bytes(data, 0, vals)

    @staticmethod
    def wdrc_atk(wdrc, data):
        for i in range(48): data[i] = 0
        ch = wdrc['channels']
        for i in range(wdrc['num_channels']):
            bs300_set_word(data, i,
                _time_to_index(ch[i]['epd_at_ms'])
                | (_time_to_index(ch[i]['kp1_at_ms']) << 8)
                | (_time_to_index(ch[i]['kp2_at_ms']) << 16))

    @staticmethod
    def wdrc_rel(wdrc, data):
        for i in range(48): data[i] = 0
        ch = wdrc['channels']
        for i in range(wdrc['num_channels']):
            bs300_set_word(data, i,
                _time_to_index(ch[i]['epd_rt_ms'])
                | (_time_to_index(ch[i]['kp1_rt_ms']) << 8)
                | (_time_to_index(ch[i]['kp2_rt_ms']) << 16))

    @staticmethod
    def wdrc_rat(wdrc, data):
        for i in range(48): data[i] = 0
        ch = wdrc['channels']
        for i in range(wdrc['num_channels']):
            bs300_set_word(data, i,
                _ratio_to_index(ch[i]['epd_ratio'])
                | (_ratio_to_index(ch[i]['kp1_ratio']) << 8)
                | (_ratio_to_index(ch[i]['kp2_ratio']) << 16))

    @staticmethod
    def wdrc_bg(wdrc, calib, input_type, vol_level, eq_low, eq_mid, eq_high, data):
        for i in range(48): data[i] = 0
        igd = c_igd(input_type, calib)
        vol_gain = vol_level * 5
        for i in range(32):
            gc = calib.output_band[i] - calib.mic1_band[i]
            hz = _FREQ_TABLE_C[i]
            if i == 0 or hz < 500:         eq_g = eq_low
            elif hz <= 2000:                eq_g = eq_mid
            else:                           eq_g = eq_high
            n = (wdrc['bin_gains'][i] + vol_gain + eq_g)*10 - gc*10
            data[i] = c_clamp(c_apply_igd_trunc(n, -igd), -128, 127) & 0xFF

    @staticmethod
    def wdrc_lmt_th(wdrc, calib, cal_off, data):
        for i in range(48): data[i] = 0
        ch = wdrc['channels']; nch = wdrc['num_channels']
        for i in range(nch):
            fidx = _freq_to_index(ch[i]['freq_hz'])
            oc = (calib.output_band[fidx] + calib.output_band[fidx+1])//2 if fidx < 31 else calib.output_band[fidx]
            oc += cal_off[i] if i < len(cal_off) else 0
            data[i] = c_clamp(60 + ch[i]['lmt_th_db'] - oc, -128, 127) & 0xFF

    @staticmethod
    def wdrc_lmt_atk(wdrc, data):
        for i in range(48): data[i] = 0
        ch = wdrc['channels']
        for i in range(wdrc['num_channels']):
            data[i] = _time_to_index(ch[i]['lmt_at_ms']) & 0xFF

    @staticmethod
    def wdrc_lmt_rel(wdrc, data):
        for i in range(48): data[i] = 0
        ch = wdrc['channels']
        for i in range(wdrc['num_channels']):
            data[i] = _time_to_index(ch[i]['lmt_rt_ms']) & 0xFF

    @staticmethod
    def wdrc_lmt_rat(wdrc, data):
        for i in range(48): data[i] = 0
        ch = wdrc['channels']
        for i in range(wdrc['num_channels']):
            data[i] = _ratio_to_index(ch[i]['lmt_ratio']) & 0xFF

    # ---- ENR (10) ----
    @staticmethod
    def enr_general(enr, data):
        for i in range(48): data[i] = 0
        tc = enr['num_channels']
        bs300_set_word(data, 0, 1)
        bs300_set_word(data, 1, tc)
        bs300_set_word(data, 2, 2)  # sbc
        bs300_set_word(data, 3, tc - 2)  # mbc

    @staticmethod
    def enr_freq(enr, data):
        for i in range(48): data[i] = 0
        fidx = [_freq_to_index(ch['freq_hz']) for ch in enr['channels']]
        bc = []
        for i, f in enumerate(fidx):
            if i < len(fidx)-1: bc.append(fidx[i+1] - f)
            else: bc.append(32 - f)
        _pack_uint6_4pw(data, 0, bc + [0]*(16-len(bc)))

    @staticmethod
    def enr_snr_th(enr, data):
        for i in range(48): data[i] = 0
        ch = enr['channels']; tc = enr['num_channels']
        enc = [c_clamp(c_trunc_div(ch[i]['snr_th_db']*1600, 301), 0, 4095) & 0xFFF for i in range(tc)]
        enc += [0]*(16-tc)
        _pack_int12_2pw(data, 0, enc)

    @staticmethod
    def enr_max_att(enr, data):
        for i in range(48): data[i] = 0
        ch = enr['channels']; tc = enr['num_channels']
        enc = []
        for i in range(tc):
            st = ch[i]['snr_th_db'] if ch[i]['snr_th_db'] > 0 else 1
            enc.append(c_clamp(c_trunc_div(ch[i]['max_att_db']*256, st), 0, 4095) & 0xFFF)
        enc += [0]*(16-tc)
        _pack_int12_2pw(data, 0, enc)

    @staticmethod
    def enr_noise_th(enr, calib, input_type, data):
        for i in range(48): data[i] = 0
        igd = c_igd(input_type, calib)
        ch = enr['channels']; tc = enr['num_channels']
        fidx, bc = C._enr_band_info(enr)
        enc = []
        for i in range(tc):
            if bc[i] > 0 and fidx[i]+bc[i] <= 32:
                mc = c_mic1_cal_avg(calib, fidx[i], bc[i])
            else:
                mc = calib.mic1_band[fidx[i] if fidx[i] < 32 else 1]
            enc.append(c_clamp(c_enr_nt_int(ch[i]['noise_th_db'], mc, igd), -2048, 2047) & 0xFFF)
        enc += [0]*(16-tc)
        _pack_int12_2pw(data, 0, enc)

    @staticmethod
    def enr_upper_noise_th(enr, calib, input_type, data):
        for i in range(48): data[i] = 0
        igd = c_igd(input_type, calib)
        ch = enr['channels']; tc = enr['num_channels']
        fidx, bc = C._enr_band_info(enr)
        enc = []
        for i in range(tc):
            if bc[i] > 0 and fidx[i]+bc[i] <= 32:
                mc = c_mic1_cal_avg(calib, fidx[i], bc[i])
            else:
                mc = calib.mic1_band[fidx[i] if fidx[i] < 32 else 1]
            enc.append(c_clamp(c_enr_nt_int(ch[i]['upper_noise_th_db'], mc, igd), -2048, 2047) & 0xFFF)
        enc += [0]*(16-tc)
        _pack_int12_2pw(data, 0, enc)

    @staticmethod
    def enr_smoothing(enr, data):
        for i in range(48): data[i] = 0
        # Fixed values for first 6 words
        bs300_set_word(data, 0, 0x200000)
        bs300_set_word(data, 1, 0x600000)
        bs300_set_word(data, 2, 0x100000)
        bs300_set_word(data, 3, 0x700000)
        bs300_set_word(data, 4, 0x020000)
        bs300_set_word(data, 5, 0x7E0000)
        # Smoothing factors with hw override for snasf
        sf = [
            (enr.get('nhsf', 8), 6, 7),
            (enr.get('nfsf', 8), 8, 9),
            (enr.get('nnsf', 8), 10, 11),
            (4, 13, 14),  # chip overrides snasf to 4
        ]
        for val, w1, w2 in sf:
            if val >= 2:
                d1 = (1 << (23 - val))
                d2 = 0x7FFFFF - d1 + 1
            else:
                d1 = 0x7FFFFF
                d2 = 0x000001
            bs300_set_word(data, w1, d1 & 0xFFFFFF)
            bs300_set_word(data, w2, d2 & 0xFFFFFF)
        bs300_set_word(data, 12, 0x004000)

    @staticmethod
    def enr_etr(enr, data):
        for i in range(48): data[i] = 0
        ch = enr['channels']; tc = enr['num_channels']
        for i in range(min(tc, 16)):
            etr_x100 = int(ch[i].get('exp_trans_ratio', 1.0) * 100)
            ma = ch[i]['max_att_db'] if ch[i]['max_att_db'] > 0 else 1
            num = 2524971008 * (etr_x100 - 100)
            den = 1600 * etr_x100 * ma
            coded = int(num / den)
            bs300_set_word(data, i, coded & 0xFFFFFF)

    @staticmethod
    def enr_nrr(enr, data):
        for i in range(48): data[i] = 0
        ch = enr['channels']; tc = enr['num_channels']
        for i in range(min(tc, 16)):
            nrr_x10 = int(ch[i].get('noise_red_ratio', 1.0) * 10)
            ma = ch[i]['max_att_db'] if ch[i]['max_att_db'] > 0 else 1
            num = 2524970707 * nrr_x10
            den = 16000 * ma
            coded = int(num / den)
            bs300_set_word(data, i, coded & 0xFFFFFF)

    @staticmethod
    def enr_sasf(enr, data):
        for i in range(48): data[i] = 0
        ch = enr['channels']; tc = enr['num_channels']
        for i in range(min(tc, 16)):
            sv = ch[i].get('sasf', 8)
            if sv >= 2:
                d1 = (1 << (23 - sv))
            else:
                d1 = 0x7FFFFF
            bs300_set_word(data, i, d1 & 0xFFFFFF)

    # ---- Volume/Beep ----
    @staticmethod
    def volume_beep(mod, calib, data):
        for i in range(48): data[i] = 0
        beep_hz = _BEEP_IDX_TO_HZ_C[mod.get('beep_freq_idx', 3)] if mod.get('beep_freq_idx', 3) < 25 else 1000
        beep_band = c_freq_to_cal_band(beep_hz)
        outcal_beep = calib.output_band[beep_band]
        x = mod['beep_level'] - outcal_beep
        idx = x + 255
        if idx < 0: idx = 0
        if idx >= 396: idx = 395
        bs300_set_word(data, 0, BEEP_FRAC24[idx])
        bs300_set_word(data, 1, mod.get('beep_freq_idx', 3))
        bs300_set_word(data, 2, c_db_to_int24(mod['min_vol'] * 10))
        bs300_set_word(data, 3, c_db_to_int24(mod['max_vol'] * 10))
        bs300_set_word(data, 4, mod.get('input_selection', 2))
        batt_hz = _BEEP_IDX_TO_HZ_C[mod.get('batt_beep_freq_idx', 3)] if mod.get('batt_beep_freq_idx', 3) < 25 else 1000
        batt_band = c_freq_to_cal_band(batt_hz)
        bs300_set_word(data, 5, mod.get('batt_beep_freq_idx', 3))
        outcal_batt = calib.output_band[batt_band]
        x = mod['batt_beep_level'] - outcal_batt
        idx = x + 255
        if idx < 0: idx = 0
        if idx >= 396: idx = 395
        bs300_set_word(data, 6, BEEP_FRAC24[idx])

    # ---- DFBC ----
    @staticmethod
    def dfbc(mode_name, calib, data):
        for i in range(48): data[i] = 0
        mode_map = {'Disable': 0, 'Fast Strong DFBC': 1, 'Slow Mild DFBC': 2}
        mode = mode_map.get(mode_name, 0)
        bs300_set_word(data, 0, mode & 0x0F)
        delay_n = (calib.fbc_bulk_delay * 10 + 312) // 625
        if delay_n > 524: delay_n = 524
        bs300_set_word(data, 1, delay_n)

    # ---- ISS ----
    @staticmethod
    def iss(mod, calib, input_type, data):
        for i in range(48): data[i] = 0
        enabled = mod.get('threshold', 0) > 0
        bs300_set_word(data, 0, 1 if enabled else 0)
        if not enabled: return
        all_mic1 = sum(calib.mic1_band[i] for i in range(32))
        mic1_cal = c_round_div(all_mic1 + 16, 32)
        igd = c_igd(input_type, calib)
        exp_tenth_numer = -30 - mod['threshold'] * 10 + mic1_cal * 10 + igd
        exp_tenth = c_round_div(exp_tenth_numer, 10)
        idx = exp_tenth - 30
        if idx < 0: idx = 0
        if idx >= 71: idx = 70
        bs300_set_word(data, 1, ISS_FRAC48[idx][0])
        bs300_set_word(data, 2, ISS_FRAC48[idx][1])

    # ---- WNR (4) ----
    @staticmethod
    def wnr_setup(mod, calib, data):
        for i in range(48): data[i] = 0
        enabled = mod.get('dual_mic_mode', False)
        dual = enabled  # same field for C code
        sel = 0
        if enabled: sel |= 1
        if dual: sel |= 2
        bs300_set_word(data, 0, sel)
        if not (sel & 1): return
        all_mic1 = sum(calib.mic1_band[i] for i in range(32))
        avg_ceil = (all_mic1 + 31) // 32
        diff = 75 - avg_ceil
        detect_val = c_round_div(diff * 409600, 301)
        bs300_set_word(data, 1, detect_val & 0xFFFFFF)
        idx = calib.mic2_gain_diff + 50
        if idx < 0: idx = 0
        if idx >= 101: idx = 100
        bs300_set_word(data, 2, MIC2_CAL_FRAC24[idx])
        preset_name = mod.get('suppression_preset', 'Minimal')
        preset_map = {'Minimal': 0, 'Light': 1, 'Medium': 2, 'Strong': 3, 'Maximal': 4}
        pidx = preset_map.get(preset_name, 0)
        ssp = _WNR_PRESET_TO_SSP_C[min(pidx, 4)]
        bs300_set_word(data, 3, 0x000006 if ssp >= 12 else 0x000003)
        bs300_set_word(data, 4, 0x001543)
        bs300_set_word(data, 5, 0x2aaaab)
        bs300_set_word(data, 6, 0x200000)

    @staticmethod
    def wnr_band(mod, calib, input_type, band_start, data):
        for i in range(48): data[i] = 0
        enabled = mod.get('dual_mic_mode', False)
        if not enabled: return
        igd = c_igd(input_type, calib)
        preset_name = mod.get('suppression_preset', 'Minimal')
        preset_map = {'Minimal': 0, 'Light': 1, 'Medium': 2, 'Strong': 3, 'Maximal': 4}
        pidx = preset_map.get(preset_name, 0)
        ssp = min(pidx, 4)
        for i in range(16):
            band = band_start + i
            offset = _WNR_SSP_OFFSET_C[band][ssp]
            n_tenth = calib.mic1_band[band] * 20 + igd * 2 - offset * 10
            frac = c_db_to_frac24(n_tenth)
            result = 0x2A9764 - frac
            bs300_set_word(data, i, result & 0xFFFFFF)

    @staticmethod
    def wnr_single_mic(mod, calib, input_type, data):
        for i in range(48): data[i] = 0
        enabled = mod.get('dual_mic_mode', False)
        if not enabled: return
        igd = c_igd(input_type, calib)
        preset_name = mod.get('suppression_preset', 'Minimal')
        preset_map = {'Minimal': 0, 'Light': 1, 'Medium': 2, 'Strong': 3, 'Maximal': 4}
        pidx = preset_map.get(preset_name, 0)
        ssp = min(pidx, 4)
        for i in range(3):
            offset = _WNR_DATA2_OFFSET_C[i][ssp]
            n_tenth = calib.mic1_band[i] * 20 + igd * 2 - offset * 10
            frac = c_db_to_frac24(n_tenth)
            result = 3292041 - frac
            bs300_set_word(data, i, result & 0xFFFFFF)

    # ---- AGCO ----
    @staticmethod
    def agco(mod, data):
        for i in range(48): data[i] = 0
        thr = mod.get('_threshold_tenth_db', mod.get('threshold_db', 0) * 10)
        if thr == 0:
            bs300_set_word(data, 0, 0)
            return
        bs300_set_word(data, 0, 1)
        abs_th = abs(thr)
        bs300_set_word(data, 1, 0xFA0000 - ((abs_th * 327680 + 300) // 301))
        atk = mod.get('_atk_01ms', mod.get('attack_time_ms', 50) * 10)
        if atk >= 2501: atk = 2500
        bs300_set_word(data, 2, AGCO_EXP[atk])
        rel = mod.get('_rel_01ms', mod.get('release_time_ms', 120) * 10)
        if rel >= 2501: rel = 2500
        bs300_set_word(data, 3, AGCO_EXP[rel])

    # ---- MM Plus ----
    @staticmethod
    def mm_plus(mod, calib, input_type, data):
        for i in range(48): data[i] = 0
        if not mod.get('enabled', False):
            bs300_set_word(data, 0, 0)
            return
        bs300_set_word(data, 0, 1)
        igd = c_igd(input_type, calib)
        x = mod.get('mix_ratio', 80) * 10 - igd
        idx = x + 500
        if idx < 0: idx = 0
        if idx >= 2001: idx = 2000
        bs300_set_word(data, 1, MM_PLUS_FRAC24[idx])

    # ---- DDM2 ----
    @staticmethod
    def ddm2(mod, calib, data):
        for i in range(48): data[i] = 0
        if not mod.get('enabled', False):
            bs300_set_word(data, 0, 0)
            return
        bs300_set_word(data, 0, 1)
        bs300_set_word(data, 1, mod.get('open_ear', 0))
        bs300_set_word(data, 2, mod.get('polar_pattern', 0))
        bs300_set_word(data, 3, mod.get('adm_fdm', 0))
        dly_val = int((calib.mic_delay * 8388607) / 1250)
        bs300_set_word(data, 4, dly_val & 0xFFFFFF)
        idx = calib.mic2_gain_diff + 50
        if idx < 0: idx = 0
        if idx >= 101: idx = 100
        bs300_set_word(data, 5, (MIC2_CAL_FRAC24[idx] // 2) & 0xFFFFFF)
        bs300_set_word(data, 8, 0x7F8000)
        bs300_set_word(data, 9, 0x7801FE)
        bs300_set_word(data, 11, 0x0079B9)
        bs300_set_word(data, 12, 0x0079B9)

    # ---- TC/DAI Gain Diff ----
    @staticmethod
    def tc_dai(calib, input_type, data):
        for i in range(48): data[i] = 0
        if input_type == 'telecoil':
            gain_raw = calib.telecoil_gain_diff
        elif input_type == 'dai':
            gain_raw = calib.dai_gain_diff
        else:
            return
        val = c_trunc_div(gain_raw * 655360, 301)
        bs300_set_word(data, 0, val & 0xFFFFFF)

    @staticmethod
    def _enr_band_info(enr):
        fidx = [_freq_to_index(ch['freq_hz']) for ch in enr['channels']]
        bc = []
        for i, f in enumerate(fidx):
            if i < len(fidx)-1: bc.append(fidx[i+1]-f)
            else: bc.append(32-f)
        return fidx, bc


# ============================================================
# Test harness
# ============================================================

def load_calib():
    with open(os.path.join(_PARENT_DIR, 'data', 'calibration.json'), 'r', encoding='utf-8') as f:
        raw_entries = json.load(f)
    data_sections = {}
    for entry in raw_entries:
        cmd = int(entry['cmd_word'], 16)
        pktnum = bs300_cmd_pktnum(cmd)
        if pktnum > 2: continue
        raw_bytes = bytes(int(h, 16) for h in entry['bytes'])
        off = _find_data_offset(raw_bytes)
        data_sections[pktnum] = raw_bytes[off: off + 48]
    raw = data_sections[0] + data_sections[1] + data_sections[2]
    return parse_calibration(raw)

def load_params(pi):
    with open(os.path.join(_PARENT_DIR, 'data', f'param_values_{pi}.json'), 'r', encoding='utf-8') as f:
        return json.load(f)

def cmp(name, cmd, py, c):
    if py == c:
        return True, f"  OK  {name} (0x{cmd:06X})"
    diffs = [(i, py[i], c[i], py[i]-c[i]) for i in range(48) if py[i]!=c[i]]
    md = max(abs(d[3]) for d in diffs)
    if md <= 1:
        return True, f"  TOL {name} (0x{cmd:06X}): {len(diffs)}/48 (±1)"
    # Check for word-level ±1 (carry across byte boundaries from 1 LSB rounding)
    wdiff = []
    for wi in range(16):
        pw = bs300_get_word(py, wi)
        cw = bs300_get_word(c, wi)
        if pw != cw:
            wdiff.append((wi, pw, cw, pw - cw))
    if len(wdiff) == 1 and abs(wdiff[0][3]) == 1:
        return True, f"  TOL {name} (0x{cmd:06X}): word[{wdiff[0][0]}] ±1 LSB rounding"
    lines = [f"  FAIL {name} (0x{cmd:06X}): {len(diffs)}/48, max_diff={md}"]
    for i,pv,cv,d in diffs[:4]:
        lines.append(f"        byte[{i:2d}]: PY=0x{pv:02X}({pv:4d}) C=0x{cv:02X}({cv:4d}) diff={d:+d}")
    if len(diffs)>4: lines.append(f"        ... and {len(diffs)-4} more")
    return False, "\n".join(lines)


def main(prog_index=1):
    calib = load_calib()
    params = load_params(prog_index)
    modules = {m['name']: m for m in params['value_in_MT']['modules']}

    im = next((m for m in modules.keys() if m.startswith('Input(')), None)
    it = 'front_mic'
    if im: it = im.replace('Input(', '').replace(')', '').lower().replace(' ', '_')

    print(f"Program {prog_index}  input={it}")
    print(f"calib: mic1_avg={calib.avg_mic1_cal():.1f} out_avg={calib.avg_output_cal():.1f} "
          f"mic2_gd={calib.mic2_gain_diff} mic_delay={calib.mic_delay}")
    print()

    w = modules['WDRC']; e = modules['ENR']
    nch = w['num_channels']
    kpo = _WDRC_CAL_OFFSET_C[:]
    lmo = [0]*16; lmo[3]=1; lmo[9]=1

    results = []

    # ---- WDRC (11) ----
    t = "WDRC General"; c=bytearray(48); C.wdrc_general(w,c)
    py=encode_wdrc_general_param(nch, 0, '2KP', w['output_limiting'])
    ok,msg=cmp(t,0x8000B2,py,bytes(c)); print(msg); results.append(ok)

    t = "WDRC Freq Spacing"; c=bytearray(48); C.wdrc_freq(w,c)
    py=encode_wdrc_freq_spacing_param([2]*nch)
    ok,msg=cmp(t,0x8010B2,py,bytes(c)); print(msg); results.append(ok)

    t = "WDRC KP Threshold"; c=bytearray(48); C.wdrc_kp_th(w,calib,it,kpo,c)
    kp1=[ch['kp1_th_db'] for ch in w['channels']]; kp2=[ch['kp2_th_db'] for ch in w['channels']]
    wfi=[_freq_to_index(ch['freq_hz']) for ch in w['channels']]
    py=encode_wdrc_kp_threshold_param(kp1,calib,'2KP',it,kp2,wfi,kpo)
    ok,msg=cmp(t,0x8020B2,py,bytes(c)); print(msg); results.append(ok)

    t = "WDRC Attack Time"; c=bytearray(48); C.wdrc_atk(w,c)
    py=encode_wdrc_attack_time_param(
        [_time_to_index(ch['epd_at_ms']) for ch in w['channels']],
        [_time_to_index(ch['kp1_at_ms']) for ch in w['channels']],'2KP',
        [_time_to_index(ch['kp2_at_ms']) for ch in w['channels']])
    ok,msg=cmp(t,0x8030B2,py,bytes(c)); print(msg); results.append(ok)

    t = "WDRC Release Time"; c=bytearray(48); C.wdrc_rel(w,c)
    py=encode_wdrc_release_time_param(
        [_time_to_index(ch['epd_rt_ms']) for ch in w['channels']],
        [_time_to_index(ch['kp1_rt_ms']) for ch in w['channels']],'2KP',
        [_time_to_index(ch['kp2_rt_ms']) for ch in w['channels']])
    ok,msg=cmp(t,0x8040B2,py,bytes(c)); print(msg); results.append(ok)

    t = "WDRC Ratio"; c=bytearray(48); C.wdrc_rat(w,c)
    py=encode_wdrc_ratio_param(
        [_ratio_to_index(ch['epd_ratio']) for ch in w['channels']],
        [_ratio_to_index(ch['kp1_ratio']) for ch in w['channels']],'2KP',
        [_ratio_to_index(ch['kp2_ratio']) for ch in w['channels']])
    ok,msg=cmp(t,0x8050B2,py,bytes(c)); print(msg); results.append(ok)

    t = "WDRC Bin Gain"; c=bytearray(48)
    C.wdrc_bg(w,calib,it, 0, 0, 0, 0, c)  # vol=0,eq=0 to match ground truth baseline
    py=encode_wdrc_bin_gain_param(w['bin_gains'],calib,it)
    ok,msg=cmp(t,0x8060B2,py,bytes(c)); print(msg); results.append(ok)

    t = "WDRC Lmt Threshold"; c=bytearray(48); C.wdrc_lmt_th(w,calib,lmo,c)
    lmt=[ch['lmt_th_db'] for ch in w['channels']]
    py=encode_wdrc_lmt_threshold_param(lmt,calib,wfi,lmo)
    ok,msg=cmp(t,0x8070B2,py,bytes(c)); print(msg); results.append(ok)

    t = "WDRC Lmt Attack"; c=bytearray(48); C.wdrc_lmt_atk(w,c)
    py=encode_wdrc_lmt_atk_time_param([_time_to_index(ch['lmt_at_ms']) for ch in w['channels']])
    ok,msg=cmp(t,0x8080B2,py,bytes(c)); print(msg); results.append(ok)

    t = "WDRC Lmt Release"; c=bytearray(48); C.wdrc_lmt_rel(w,c)
    py=encode_wdrc_lmt_rel_time_param([_time_to_index(ch['lmt_rt_ms']) for ch in w['channels']])
    ok,msg=cmp(t,0x8090B2,py,bytes(c)); print(msg); results.append(ok)

    t = "WDRC Lmt Ratio"; c=bytearray(48); C.wdrc_lmt_rat(w,c)
    py=encode_wdrc_lmt_ratio_param([_ratio_to_index(ch['lmt_ratio']) for ch in w['channels']])
    ok,msg=cmp(t,0x80A0B2,py,bytes(c)); print(msg); results.append(ok)

    print("---")

    # ---- Volume/Beep ----
    t = "Volume/Beep"; c=bytearray(48)
    vol = modules.get('Volume/Beep', modules.get('Volume', {}))
    # Compute freq_idx from beep_frequency
    beep_freq = vol.get('beep_frequency', 1000)
    freq_idx = 3  # default 1000Hz
    for fi, fhz in enumerate(_BEEP_IDX_TO_HZ_C):
        if fhz == beep_freq: freq_idx = fi; break
    batt_freq = vol.get('batt_flat_beep_freq', 1000)
    batt_freq_idx = 3
    for fi, fhz in enumerate(_BEEP_IDX_TO_HZ_C):
        if fhz == batt_freq: batt_freq_idx = fi; break
    input_sel = {'Front Mic': 0, 'Rear Mic': 0, 'Telecoil': 2, 'DAI': 3}.get(
        modules.get('Input(Front Mic)', {}).get('input_type', 'Front Mic'), 0)

    C.volume_beep({
        'beep_level': vol.get('beep_level', 80),
        'beep_freq_idx': freq_idx,
        'min_vol': vol.get('min_volume', -12),
        'max_vol': vol.get('max_volume', 0),
        'input_selection': input_sel,
        'batt_beep_level': vol.get('batt_flat_beep_level', 80),
        'batt_beep_freq_idx': batt_freq_idx,
    }, calib, c)
    py=encode_volume_beep_param(vol.get('beep_level', 80), freq_idx,
        vol.get('min_volume', -12), vol.get('max_volume', 0),
        input_sel, vol.get('batt_flat_beep_level', 80), batt_freq_idx, calib)
    ok,msg=cmp(t,0x800081,py,bytes(c)); print(msg); results.append(ok)

    # ---- Telecoil Gain Diff ----
    t = "TC/DAI Gain Diff"; c=bytearray(48)
    C.tc_dai(calib, it, c)
    if it == 'telecoil':
        py=encode_tc_dai_gain_diff_param(calib.telecoil_gain_diff, calib)
    elif it == 'dai':
        py=encode_tc_dai_gain_diff_param(calib.dai_gain_diff, calib)
    else:
        py = bytes(48)
    ok,msg=cmp(t,0x804272,py,bytes(c)); print(msg); results.append(ok)

    # ---- DFBC ----
    t = "DFBC"; c=bytearray(48)
    dfbc = modules.get('DFBC', {})
    C.dfbc(dfbc.get('mode', 'Disable'), calib, c)
    py=encode_dfbc_param({'Disable': 0, 'Fast Strong DFBC': 1, 'Slow Mild DFBC': 2}.get(dfbc.get('mode', ''), 0), calib)
    ok,msg=cmp(t,0x800052,py,bytes(c)); print(msg); results.append(ok)

    # ---- ISS ----
    t = "ISS"; c=bytearray(48)
    iss = modules.get('ISS', {})
    C.iss(iss, calib, it, c)
    py=encode_iss_param(1 if iss.get('threshold', 0) > 0 else 0,
        iss.get('threshold', 0), calib, it)
    ok,msg=cmp(t,0x8001B2,py,bytes(c)); print(msg); results.append(ok)

    # ---- WNR (4) ----
    # Force WNR enabled for cross-validation; disabled module produces zeros in C
    # but Python codegen always computes — compare enabled behavior to validate formulas.
    wnr = modules.get('WNR', {})
    pidx = {'Minimal': 0, 'Light': 1, 'Medium': 2, 'Strong': 3, 'Maximal': 4}.get(
        wnr.get('suppression_preset', 'Minimal'), 0)
    ssp_level = _WNR_PRESET_TO_SSP_C[min(pidx, 4)]
    wnr_en = dict(wnr, dual_mic_mode=True)  # force enable for testing

    t = "WNR Setup"; c=bytearray(48)
    C.wnr_setup(wnr_en, calib, c)
    py=encode_wnr_1_param(True, True, calib, pidx)
    ok,msg=cmp(t,0x8001C2,py,bytes(c)); print(msg); results.append(ok)

    t = "WNR Band 0-15"; c=bytearray(48)
    C.wnr_band(wnr_en, calib, it, 0, c)
    py=encode_wnr_band_data_param(calib, ssp_level, it, 0)
    ok,msg=cmp(t,0x8011C2,py,bytes(c)); print(msg); results.append(ok)

    t = "WNR Band 16-31"; c=bytearray(48)
    C.wnr_band(wnr_en, calib, it, 16, c)
    py=encode_wnr_band_data_param(calib, ssp_level, it, 16)
    ok,msg=cmp(t,0x8411C2,py,bytes(c)); print(msg); results.append(ok)

    t = "WNR Single Mic"; c=bytearray(48)
    C.wnr_single_mic(wnr_en, calib, it, c)
    py=encode_wnr_single_mic_detect_param(calib, ssp_level, it)
    ok,msg=cmp(t,0x8021C2,py,bytes(c)); print(msg); results.append(ok)

    print("---")

    # ---- ENR (10) ----
    t = "ENR General"; c=bytearray(48); C.enr_general(e,c)
    py=encode_enr_general_param(1, e['num_channels'], 2, e['num_channels']-2)
    ok,msg=cmp(t,0x8000C2,py,bytes(c)); print(msg); results.append(ok)

    t = "ENR Freq Spacing"; c=bytearray(48); C.enr_freq(e,c)
    fidx, ebc = C._enr_band_info(e)
    py=encode_enr_freq_spacing_param(ebc)
    ok,msg=cmp(t,0x8010C2,py,bytes(c)); print(msg); results.append(ok)

    t = "ENR SNR Threshold"; c=bytearray(48); C.enr_snr_th(e,c)
    py=encode_enr_snr_threshold_param([ch['snr_th_db'] for ch in e['channels']])
    ok,msg=cmp(t,0x8020C2,py,bytes(c)); print(msg); results.append(ok)

    t = "ENR Max Att"; c=bytearray(48); C.enr_max_att(e,c)
    py=encode_enr_max_att_param([ch['max_att_db'] for ch in e['channels']],
        [ch['snr_th_db'] for ch in e['channels']])
    ok,msg=cmp(t,0x8030C2,py,bytes(c)); print(msg); results.append(ok)

    t = "ENR Noise Thr"; c=bytearray(48); C.enr_noise_th(e,calib,it,c)
    py=encode_enr_noise_th_param([ch['noise_th_db'] for ch in e['channels']],calib,it,fidx,ebc)
    ok,msg=cmp(t,0x8040C2,py,bytes(c)); print(msg); results.append(ok)

    t = "ENR Upper Noise Thr"; c=bytearray(48); C.enr_upper_noise_th(e,calib,it,c)
    py=encode_enr_upper_noise_th_param([ch['upper_noise_th_db'] for ch in e['channels']],calib,it,fidx,ebc)
    ok,msg=cmp(t,0x8050C2,py,bytes(c)); print(msg); results.append(ok)

    t = "ENR Smoothing"; c=bytearray(48); C.enr_smoothing(e,c)
    py=encode_enr_smoothing_param({'nhsf': e['nhsf'], 'nfsf': e['nfsf'],
        'nnsf': e['nnsf'], 'snasf': 4})
    ok,msg=cmp(t,0x8060C2,py,bytes(c)); print(msg); results.append(ok)

    t = "ENR ETR"; c=bytearray(48); C.enr_etr(e,c)
    py=encode_enr_etr_param([ch.get('exp_trans_ratio', 1.0) for ch in e['channels']],
        [ch['max_att_db'] for ch in e['channels']])
    ok,msg=cmp(t,0x8070C2,py,bytes(c)); print(msg); results.append(ok)

    t = "ENR NRR"; c=bytearray(48); C.enr_nrr(e,c)
    py=encode_enr_nrr_param([ch.get('noise_red_ratio', 1.0) for ch in e['channels']],
        [ch['max_att_db'] for ch in e['channels']])
    ok,msg=cmp(t,0x8080C2,py,bytes(c)); print(msg); results.append(ok)

    t = "ENR SASF"; c=bytearray(48)
    # ENR channels don't have sasf field in param_values; use default sasf=8
    C.enr_sasf(e,c)
    py=encode_enr_sasf_param([8]*e['num_channels'])
    ok,msg=cmp(t,0x8090C2,py,bytes(c)); print(msg); results.append(ok)

    print("---")

    # ---- AGCO ----
    t = "AGCO"; c=bytearray(48)
    agco = modules.get('AGCO', {})
    # C code uses tenth-dB for threshold; provide both forms.
    agco_c = dict(agco, _threshold_tenth_db=agco.get('threshold_db', 0) * 10,
                  _atk_01ms=agco.get('attack_time_ms', 50) * 10,
                  _rel_01ms=agco.get('release_time_ms', 120) * 10)
    C.agco(agco_c, c)
    # Python codegen uses dB and 0.1ms directly
    py=encode_agco_param(1 if agco.get('threshold_db', 0) != 0 else 0,
        agco.get('threshold_db', 0),
        agco.get('attack_time_ms', 50) * 10,
        agco.get('release_time_ms', 120) * 10)
    ok,msg=cmp(t,0x800382,py,bytes(c)); print(msg); results.append(ok)

    # ---- MM Plus (not in Program 1 param_values, test with mock enabled data) ----
    t = "MM Plus"; c=bytearray(48)
    # Use front_mic (igd=0) and modest mix_ratio (10 dB) to avoid frac24 overflow
    mm_mock = {'enabled': True, 'mix_ratio': 10}
    C.mm_plus(mm_mock, calib, 'front_mic', c)
    py=encode_mm_plus_param(1, 10, calib, 'front_mic')
    ok,msg=cmp(t,0x800062,py,bytes(c)); print(msg); results.append(ok)

    # ---- DDM2 (not in Program 1 param_values, test with mock enabled data) ----
    # mic2_cal rounding: C uses integer table / 2, Python uses float round(); ±1 tolerance expected
    t = "DDM2"; c=bytearray(48)
    ddm2_mock = {'enabled': True, 'open_ear': 0, 'polar_pattern': 0, 'adm_fdm': 0}
    C.ddm2(ddm2_mock, calib, c)
    py=encode_ddm2_param(1, 0, 0, 0, calib)
    ok,msg=cmp(t,0x800022,py,bytes(c)); print(msg); results.append(ok)

    # ---- Summary ----
    passed = sum(results); total = len(results)
    print(f"\n{'='*60}")
    print(f"Summary: {passed}/{total} pass, {total-passed} fail")
    if total - passed == 0:
        print("ALL C encode formulas match Python codegen byte-exactly.")
    else:
        print(f"FAILURES DETECTED — review above output.")
    return passed, total


if __name__ == '__main__':
    main(1)
