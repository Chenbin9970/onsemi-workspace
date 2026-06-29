"""
Show complete I2C frames for:
  1) First-time full sync (all Param commands for Program 0)
  2) Program 0 → Program 1 switch (incremental, only changed commands)

Each frame: [Len] [Checksum] [Cmd0 Cmd1 Cmd2] [Data 48B]
"""
import json, os, sys

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PARENT_DIR = os.path.dirname(_SCRIPT_DIR)
sys.path.insert(0, _PARENT_DIR)

from flash_read import parse_program_data, WdrcFlash, EnrFlash
from flash_write import _extract_readback_data
from crossval_c_vs_py import (
    C, load_calib, _FREQ_TABLE_C, _WDRC_CAL_OFFSET_C,
    _BEEP_IDX_TO_HZ_C,
)

BS300_I2C_ADDR = 0x02
BS300_LEN_HAS_DATA = 0x10

def bs300_checksum(payload):
    s = sum(payload) & 0xFF
    return 0xFF - s

def build_advanced_write(cmd_word, data_48b):
    """Build complete I2C Advanced Write frame.
    Frame layout: [Len] [Chk] [Cmd0 Cmd1 Cmd2] [Data 48B]
    Checksum is computed over Len+Cmd+Data, then inserted before Cmd."""
    # First pass: compute checksum over Len + Cmd + Data
    payload = bytearray()
    payload.append(BS300_LEN_HAS_DATA)
    payload.append(cmd_word & 0xFF)
    payload.append((cmd_word >> 8) & 0xFF)
    payload.append((cmd_word >> 16) & 0xFF)
    payload.extend(data_48b)
    chk = bs300_checksum(payload)

    # Build frame: Len, Chk, Cmd0, Cmd1, Cmd2, Data[48]
    frame = bytearray()
    frame.append(BS300_LEN_HAS_DATA)
    frame.append(chk)
    frame.append(cmd_word & 0xFF)
    frame.append((cmd_word >> 8) & 0xFF)
    frame.append((cmd_word >> 16) & 0xFF)
    frame.extend(data_48b)
    return bytes(frame), chk

def freq_to_cal_band(hz):
    best, best_dist = 0, 999999
    for i in range(1, 32):
        d = abs(hz - _FREQ_TABLE_C[i])
        if d < best_dist:
            best_dist, best = d, i
    return best

def hz_to_beep_idx(hz):
    for idx, f in enumerate(_BEEP_IDX_TO_HZ_C):
        if f == hz: return idx
    return 3

def input_type_to_selection(it):
    return {'front_mic': 0, 'rear_mic': 1, 'telecoil': 2, 'dai': 3,
            'mm_plus': 4, 'ddm2': 5, 'dual_mic': 6}.get(it, 0)

# ============================================================
# Flash decode helpers (same as simulate_switch.py)
# ============================================================
def wdrc_flash_to_struct(wf):
    nch = wf.num_channels
    s = {
        'total_channels': nch, 'nsbc': 0,
        'kp_mode': 2 if wf.kneepoints_per_channel == 1 else 1,
        'limiter': wf.output_limiting_sel,
        'freq_idx': [0]*16,
        'kp1_th_db': [0]*16, 'kp2_th_db': [0]*16,
        'epd_at_idx': [0]*16, 'epd_rt_idx': [0]*16, 'epd_r_idx': [0]*16,
        'kp1_at_idx': [0]*16, 'kp2_at_idx': [0]*16,
        'kp1_rt_idx': [0]*16, 'kp2_rt_idx': [0]*16,
        'kp1_r_idx': [0]*16, 'kp2_r_idx': [0]*16,
        'lmt_th_db': [0]*16,
        'lmt_at_idx': [0]*16, 'lmt_rt_idx': [0]*16, 'lmt_r_idx': [0]*16,
        'bin_gains': [bg - 27 for bg in wf.bin_gain],  # Flash: 27+value_in_MT → VM: value_in_MT
        'num_channels': nch,
    }
    for i, ch in enumerate(wf.channels):
        if i >= 16: break
        s['freq_idx'][i] = ch.frequency_idx
        s['kp1_th_db'][i] = ch.kp1_th
        s['kp2_th_db'][i] = ch.kp2_th
        s['epd_at_idx'][i] = ch.epd_at
        s['epd_rt_idx'][i] = ch.epd_rt
        s['epd_r_idx'][i] = ch.epd_r
        s['kp1_at_idx'][i] = ch.kp1_at
        s['kp2_at_idx'][i] = ch.kp2_at
        s['kp1_rt_idx'][i] = ch.kp1_rt
        s['kp2_rt_idx'][i] = ch.kp2_rt
        s['kp1_r_idx'][i] = ch.kp1_r
        s['kp2_r_idx'][i] = ch.kp2_r
        s['lmt_th_db'][i] = ch.lmt_th + 30
        s['lmt_at_idx'][i] = ch.lmt_at
        s['lmt_rt_idx'][i] = ch.lmt_rt
        s['lmt_r_idx'][i] = ch.lmt_r
    return s

def enr_flash_to_struct(ef):
    nch = ef.num_channels
    s = {
        'enable_num_ch': 0x80 | (nch & 0x3F),
        'nfsf': ef.nfsf + 1, 'nhsf': ef.nhsf + 1,
        'nnsf': ef.nnsf + 1, 'snasf': ef.snasf + 1,
        'freq_idx': [0]*16,
        'snr_th_db': [0]*16, 'max_att_db': [0]*16,
        'noise_th_db': [0]*16, 'upper_noise_th_db': [0]*16,
        'etr_x100': [0]*16, 'nrr_x10': [0]*16, 'sasf': [8]*16,
        'num_channels': nch,
    }
    for i, ch in enumerate(ef.channels):
        if i >= 16: break
        s['freq_idx'][i] = ch.frequency_idx
        s['snr_th_db'][i] = ch.snrth
        s['max_att_db'][i] = ch.ma
        s['noise_th_db'][i] = ch.nt + 10
        s['upper_noise_th_db'][i] = ch.unt + 40
        s['etr_x100'][i] = ch.etr
        s['nrr_x10'][i] = ch.nrr
    return s

def program_flash_to_dict(prog):
    wdrc = wdrc_flash_to_struct(prog.wdrc) if prog.wdrc else None
    enr = enr_flash_to_struct(prog.enr) if prog.enr else None
    inp = prog.inputs; vol = prog.volume
    iss = prog.iss; wnr = prog.wnr; agco = prog.agco; dfbc = prog.dfbc
    input_sel = input_type_to_selection(inp.input_type) if inp else 0

    modules = {
        'vol_enable': 1,
        'beep_level': vol.beep_level if vol else 80,
        'beep_freq_idx': hz_to_beep_idx(vol.beep_frequency) if vol else 3,
        'min_vol': vol.min_volume if vol else -12,
        'max_vol': vol.max_volume if vol else 0,
        'input_selection': input_sel,
        'batt_beep_level': vol.battery_flat_beep_level if vol else 80,
        'batt_beep_freq_idx': hz_to_beep_idx(vol.battery_flat_beep_frequency) if vol else 3,
        'dfbc_enable_mode': (0x80 | dfbc.dfbc_mode) if dfbc else 0,
        'iss_enable': 1 if iss else 0,
        'iss_threshold': iss.iss_threshold if iss else 0,
        'wnr_enable_dual': 0x01 if wnr else 0,  # C decode: data[0] | 0x01 → always 0x01
        'wnr_preset': wnr.suppression_strength_preset if wnr else 0,
        'agco_enable': 1 if agco else 0,
        'agco_threshold_db': -(agco.threshold) if agco else 0,
        'agco_attack_01ms': agco.attack_time if agco else 0,
        'agco_release_01ms': agco.release_time if agco else 0,
        'mm_plus_enable': 1 if (inp and inp.input_type == 'mm_plus') else 0,
        'mix_ratio': inp.mic_mixing_ratio - 50 if inp else 50,
        'ddm2_enable': 1 if (inp and inp.input_type == 'ddm2') else 0,
        'open_ear': inp.open_ear_mode_sel if inp else 0,
        'polar_pattern': inp.fixed_polar_pattern if inp else 0,
        'adm_fdm': inp.mode if inp else 0,
        'volume_level': 5, 'eq_low': 0, 'eq_mid': 0, 'eq_high': 0,
    }
    return {'wdrc': wdrc, 'enr': enr, 'modules': modules}

def make_wdrc_wrapper(s):
    """Convert Flash struct indices → human values for C port roundtrip."""
    from math_utils import _TIME_TABLE, _RATIO_TABLE
    nch = s['total_channels']
    ch_list = []
    def idx_to_ms(idx): return _TIME_TABLE[idx] if idx < len(_TIME_TABLE) else 5
    def idx_to_ratio(idx): return _RATIO_TABLE[idx] if idx < len(_RATIO_TABLE) else 1.0
    for i in range(nch):
        ch_list.append({
            'freq_hz': _FREQ_TABLE_C[s['freq_idx'][i]],
            'kp1_th_db': s['kp1_th_db'][i], 'kp2_th_db': s['kp2_th_db'][i],
            'epd_at_ms': idx_to_ms(s['epd_at_idx'][i]),
            'epd_rt_ms': idx_to_ms(s['epd_rt_idx'][i]),
            'epd_ratio': idx_to_ratio(s['epd_r_idx'][i]),
            'kp1_at_ms': idx_to_ms(s['kp1_at_idx'][i]),
            'kp2_at_ms': idx_to_ms(s['kp2_at_idx'][i]),
            'kp1_rt_ms': idx_to_ms(s['kp1_rt_idx'][i]),
            'kp2_rt_ms': idx_to_ms(s['kp2_rt_idx'][i]),
            'kp1_ratio': idx_to_ratio(s['kp1_r_idx'][i]),
            'kp2_ratio': idx_to_ratio(s['kp2_r_idx'][i]),
            'lmt_th_db': s['lmt_th_db'][i],
            'lmt_at_ms': idx_to_ms(s['lmt_at_idx'][i]),
            'lmt_rt_ms': idx_to_ms(s['lmt_rt_idx'][i]),
            'lmt_ratio': idx_to_ratio(s['lmt_r_idx'][i]),
        })
    return {'num_channels': nch, 'channels': ch_list, 'bin_gains': s['bin_gains']}

def make_enr_wrapper(s):
    """Convert Flash struct → C port format."""
    nch = s['num_channels']
    ch_list = []
    for i in range(nch):
        ch_list.append({
            'freq_hz': _FREQ_TABLE_C[s['freq_idx'][i]],
            'snr_th_db': s['snr_th_db'][i],
            'max_att_db': s['max_att_db'][i],
            'noise_th_db': s['noise_th_db'][i],
            'upper_noise_th_db': s['upper_noise_th_db'][i],
            'exp_trans_ratio': s['etr_x100'][i] / 100.0,
            'noise_red_ratio': s['nrr_x10'][i] / 10.0,
        })
    return {'num_channels': nch, 'channels': ch_list,
            'nfsf': s['nfsf'], 'nhsf': s['nhsf'],
            'nnsf': s['nnsf'], 'snasf': s['snasf']}


# ============================================================
# Encode all commands for full sync
# ============================================================
def encode_full_sync(p, calib, it):
    """Return list of (cmd_name, cmd_word, data_48B) for full sync."""
    w = make_wdrc_wrapper(p['wdrc'])
    e = make_enr_wrapper(p['enr'])
    m = p['modules']
    nch = p['wdrc']['total_channels']
    lmo = [0]*16; lmo[3]=1; lmo[9]=1
    result = []

    def enc(name, cmd, data):
        result.append((name, cmd, bytes(data)))

    data = bytearray(48)

    # WDRC (11)
    C.wdrc_general(w, data); enc('WDRC General Setup',     0x8000B2, data)
    C.wdrc_freq(w, data);    enc('WDRC Freq Spacing',      0x8010B2, data)
    C.wdrc_kp_th(w, calib, it, _WDRC_CAL_OFFSET_C, data)
    enc('WDRC KP Threshold',       0x8020B2, data)
    C.wdrc_atk(w, data);    enc('WDRC Attack Time',        0x8030B2, data)
    C.wdrc_rel(w, data);    enc('WDRC Release Time',       0x8040B2, data)
    C.wdrc_rat(w, data);    enc('WDRC Ratio',              0x8050B2, data)
    C.wdrc_bg(w, calib, it, m['volume_level'], m['eq_low'], m['eq_mid'], m['eq_high'], data)
    enc('WDRC Bin Gain',           0x8060B2, data)
    C.wdrc_lmt_th(w, calib, lmo, data)
    enc('WDRC Lmt Threshold',      0x8070B2, data)
    C.wdrc_lmt_atk(w, data); enc('WDRC Lmt Attack',        0x8080B2, data)
    C.wdrc_lmt_rel(w, data); enc('WDRC Lmt Release',       0x8090B2, data)
    C.wdrc_lmt_rat(w, data); enc('WDRC Lmt Ratio',         0x80A0B2, data)

    # Volume/Beep/Input (1)
    C.volume_beep({
        'beep_level': m['beep_level'], 'beep_freq_idx': m['beep_freq_idx'],
        'min_vol': m['min_vol'], 'max_vol': m['max_vol'],
        'input_selection': m['input_selection'],
        'batt_beep_level': m['batt_beep_level'],
        'batt_beep_freq_idx': m['batt_beep_freq_idx'],
    }, calib, data)
    enc('Volume/Beep/Input',       0x800081, data)

    # ENR (10) — only if enabled
    if p['enr']['enable_num_ch'] & 0x80:
        C.enr_general(e, data);    enc('ENR General Setup',     0x8000C2, data)
        C.enr_freq(e, data);       enc('ENR Freq Spacing',      0x8010C2, data)
        C.enr_snr_th(e, data);     enc('ENR SNR Threshold',     0x8020C2, data)
        C.enr_max_att(e, data);    enc('ENR Max Attenuation',   0x8030C2, data)
        C.enr_noise_th(e, calib, it, data)
        enc('ENR Noise Threshold',      0x8040C2, data)
        C.enr_upper_noise_th(e, calib, it, data)
        enc('ENR Upper Noise Thr',      0x8050C2, data)
        C.enr_smoothing(e, data);  enc('ENR Smoothing',         0x8060C2, data)
        C.enr_etr(e, data);        enc('ENR ETR',               0x8070C2, data)
        C.enr_nrr(e, data);        enc('ENR NRR',               0x8080C2, data)
        C.enr_sasf(e, data);       enc('ENR SASF',              0x8090C2, data)

    # DFBC (1) — C code uses mode value directly: set_word(data, 0, mode & 0x0F)
    data = bytearray(48)
    import struct as _struct
    mode_val = m['dfbc_enable_mode'] & 0x0F
    data[0] = mode_val & 0xFF
    data[1] = (mode_val >> 8) & 0xFF
    data[2] = (mode_val >> 16) & 0xFF
    # delay_n = round(fbc_bulk_delay_us / 62.5) — same as C.dfbc
    from bs300_codegen import bs300_set_word
    delay_n = (calib.fbc_bulk_delay * 10 + 312) // 625
    if delay_n > 524: delay_n = 524
    bs300_set_word(data, 1, delay_n)
    enc('DFBC',                    0x800052, data)

    # ISS (1)
    C.iss({'threshold': m['iss_threshold']}, calib, it, data)
    enc('ISS',                     0x8001B2, data)

    # WNR (4) — C port uses dual_mic_mode for BOTH enable+dual; pass True for enable
    wnr_dual = (m['wnr_enable_dual'] & 0x02) != 0
    wnr_en = {'dual_mic_mode': True, 'suppression_preset':
        {0: 'Minimal', 1: 'Light', 2: 'Medium', 3: 'Strong', 4: 'Maximal'}.get(m['wnr_preset'], 'Minimal')}
    C.wnr_setup(wnr_en, calib, data)
    data[0] = (m['wnr_enable_dual'] & 0x03)  # override selection to match stored value
    enc('WNR Setup',               0x8001C2, data)
    C.wnr_band(wnr_en, calib, it, 0, data)
    enc('WNR Band 0-15',           0x8011C2, data)
    C.wnr_band(wnr_en, calib, it, 16, data)
    enc('WNR Band 16-31',          0x8411C2, data)
    C.wnr_single_mic(wnr_en, calib, it, data)
    enc('WNR Single Mic',          0x8021C2, data)

    # AGCO (1)
    agco_c = {'_threshold_tenth_db': m['agco_threshold_db'] * 10,
               '_atk_01ms': m['agco_attack_01ms'],
               '_rel_01ms': m['agco_release_01ms']}
    C.agco(agco_c, data)
    enc('AGCO',                    0x800382, data)

    # Input source (conditional)
    if m['input_selection'] == 4:
        C.mm_plus({'enabled': True, 'mix_ratio': m['mix_ratio']}, calib, it, data)
        enc('MM Plus',             0x800062, data)
    elif m['input_selection'] == 5:
        ddm2_mock = {'enabled': True, 'open_ear': m['open_ear'],
                      'polar_pattern': m['polar_pattern'], 'adm_fdm': m['adm_fdm']}
        C.ddm2(ddm2_mock, calib, data)
        enc('DDM2',                0x800022, data)
    if m['input_selection'] in (2, 3):
        C.tc_dai(calib, it, data)
        enc('TC/DAI Gain Diff',    0x804272, data)

    return result


# ============================================================
# Simulate switch (same logic as bs300_switch_program in C)
# ============================================================
def simulate_switch(old_p, new_p, old_it, new_it, calib):
    ow = old_p['wdrc']; nw = new_p['wdrc']
    om = old_p['modules']; nm = new_p['modules']
    oe = old_p['enr']; ne = new_p['enr']
    igd_changed = (old_it != new_it)

    sent = []
    dis = []

    def send(name, cmd, data):
        sent.append((name, cmd, bytes(data)))

    def disable(name, cmd):
        dis.append((name, cmd))

    data = bytearray(48)

    # WDRC
    hdr_changed = (ow['total_channels'] != nw['total_channels']) \
               or (ow['kp_mode'] != nw['kp_mode']) \
               or (ow['limiter'] != nw['limiter'])
    freq_changed = (ow['freq_idx'] != nw['freq_idx'])
    kp1_changed = (ow['kp1_th_db'] != nw['kp1_th_db'])
    kp2_changed = (ow['kp2_th_db'] != nw['kp2_th_db'])
    at_changed = (ow['epd_at_idx'] != nw['epd_at_idx']) \
              or (ow['kp1_at_idx'] != nw['kp1_at_idx']) or (ow['kp2_at_idx'] != nw['kp2_at_idx'])
    rt_changed = (ow['epd_rt_idx'] != nw['epd_rt_idx']) \
              or (ow['kp1_rt_idx'] != nw['kp1_rt_idx']) or (ow['kp2_rt_idx'] != nw['kp2_rt_idx'])
    ratio_changed = (ow['epd_r_idx'] != nw['epd_r_idx']) \
                 or (ow['kp1_r_idx'] != nw['kp1_r_idx']) or (ow['kp2_r_idx'] != nw['kp2_r_idx'])
    bg_changed = (ow['bin_gains'] != nw['bin_gains'])
    lmt_th_changed = (ow['lmt_th_db'] != nw['lmt_th_db'])
    lmt_at_changed = (ow['lmt_at_idx'] != nw['lmt_at_idx'])
    lmt_rt_changed = (ow['lmt_rt_idx'] != nw['lmt_rt_idx'])
    lmt_r_changed  = (ow['lmt_r_idx']  != nw['lmt_r_idx'])

    w = make_wdrc_wrapper(nw)
    lmo = [0]*16; lmo[3]=1; lmo[9]=1

    if hdr_changed or freq_changed or kp1_changed or kp2_changed or igd_changed:
        C.wdrc_kp_th(w, calib, new_it, _WDRC_CAL_OFFSET_C, data)
        send('WDRC KP Threshold', 0x8020B2, data)

    if bg_changed or igd_changed:
        C.wdrc_bg(w, calib, new_it, nm['volume_level'], nm['eq_low'], nm['eq_mid'], nm['eq_high'], data)
        send('WDRC Bin Gain', 0x8060B2, data)

    if ow['limiter'] == 1 and nw['limiter'] == 1:
        if freq_changed or lmt_th_changed:
            C.wdrc_lmt_th(w, calib, lmo, data)
            send('WDRC Lmt Threshold', 0x8070B2, data)

    # Volume/Beep
    vol_changed = (om['beep_level'] != nm['beep_level']) \
               or (om['beep_freq_idx'] != nm['beep_freq_idx']) \
               or (om['min_vol'] != nm['min_vol']) or (om['max_vol'] != nm['max_vol']) \
               or (om['input_selection'] != nm['input_selection']) \
               or (om['batt_beep_level'] != nm['batt_beep_level']) \
               or (om['batt_beep_freq_idx'] != nm['batt_beep_freq_idx'])
    if vol_changed or igd_changed:
        C.volume_beep({
            'beep_level': nm['beep_level'], 'beep_freq_idx': nm['beep_freq_idx'],
            'min_vol': nm['min_vol'], 'max_vol': nm['max_vol'],
            'input_selection': nm['input_selection'],
            'batt_beep_level': nm['batt_beep_level'],
            'batt_beep_freq_idx': nm['batt_beep_freq_idx'],
        }, calib, data)
        send('Volume/Beep/Input', 0x800081, data)

    # ENR
    e = make_enr_wrapper(ne)
    oe_ena = (oe['enable_num_ch'] & 0x80) != 0
    ne_ena = (ne['enable_num_ch'] & 0x80) != 0

    if oe_ena and ne_ena:
        enr_freq_changed = (oe['freq_idx'] != ne['freq_idx'])
        snr_changed = (oe['snr_th_db'] != ne['snr_th_db'])
        ma_changed = (oe['max_att_db'] != ne['max_att_db'])
        nt_changed = (oe['noise_th_db'] != ne['noise_th_db'])
        unt_changed = (oe['upper_noise_th_db'] != ne['upper_noise_th_db'])
        sf_changed = (oe['nfsf'] != ne['nfsf']) or (oe['nhsf'] != ne['nhsf']) \
                  or (oe['nnsf'] != ne['nnsf']) or (oe['snasf'] != ne['snasf'])
        etr_changed = (oe['etr_x100'] != ne['etr_x100'])
        nrr_changed = (oe['nrr_x10'] != ne['nrr_x10'])

        if snr_changed or ma_changed:
            C.enr_max_att(e, data)
            send('ENR Max Att', 0x8030C2, data)
        if enr_freq_changed or nt_changed or igd_changed:
            C.enr_noise_th(e, calib, new_it, data)
            send('ENR Noise Thr', 0x8040C2, data)
        if enr_freq_changed or unt_changed or igd_changed:
            C.enr_upper_noise_th(e, calib, new_it, data)
            send('ENR Upper Noise Thr', 0x8050C2, data)
        if snr_changed or ma_changed or etr_changed:
            C.enr_etr(e, data)
            send('ENR ETR', 0x8070C2, data)
        if snr_changed or ma_changed or nrr_changed:
            C.enr_nrr(e, data)
            send('ENR NRR', 0x8080C2, data)
    elif oe_ena and not ne_ena:
        disable('ENR General Setup', 0x8000C2)

    # ISS
    if om['iss_enable'] and nm['iss_enable']:
        if om['iss_threshold'] != nm['iss_threshold'] or igd_changed:
            C.iss({'threshold': nm['iss_threshold']}, calib, new_it, data)
            send('ISS', 0x8001B2, data)

    # WNR
    if (om['wnr_enable_dual'] & 0x01) and (nm['wnr_enable_dual'] & 0x01):
        if om['wnr_preset'] != nm['wnr_preset'] or igd_changed:
            wnr_dual = (nm['wnr_enable_dual'] & 0x02) != 0
            wnr_en = {'dual_mic_mode': True, 'suppression_preset':
                {0: 'Minimal', 1: 'Light', 2: 'Medium', 3: 'Strong', 4: 'Maximal'}.get(nm['wnr_preset'], 'Minimal')}
            C.wnr_setup(wnr_en, calib, data)
            data[0] = (nm['wnr_enable_dual'] & 0x03)
            send('WNR Setup', 0x8001C2, data)
            C.wnr_band(wnr_en, calib, new_it, 0, data)
            send('WNR Band 0-15', 0x8011C2, data)
            C.wnr_band(wnr_en, calib, new_it, 16, data)
            send('WNR Band 16-31', 0x8411C2, data)
            C.wnr_single_mic(wnr_en, calib, new_it, data)
            send('WNR Single Mic', 0x8021C2, data)

    # AGCO
    if om['agco_enable'] and nm['agco_enable']:
        if om['agco_threshold_db'] != nm['agco_threshold_db'] \
            or om['agco_attack_01ms'] != nm['agco_attack_01ms'] \
            or om['agco_release_01ms'] != nm['agco_release_01ms']:
            agco_c = {'_threshold_tenth_db': nm['agco_threshold_db'] * 10,
                       '_atk_01ms': nm['agco_attack_01ms'],
                       '_rel_01ms': nm['agco_release_01ms']}
            C.agco(agco_c, data)
            send('AGCO', 0x800382, data)

    # TC/DAI disable
    o_tc = om['input_selection'] in (2, 3)
    n_tc = nm['input_selection'] in (2, 3)
    if o_tc and not n_tc:
        disable('TC/DAI Gain Diff', 0x804272)

    return sent, dis


# ============================================================
# Display helpers
# ============================================================
def print_frame(cmd_name, cmd_word, data_48b, tag="SEND"):
    frame, chk = build_advanced_write(cmd_word, data_48b)
    hex_str = ' '.join(f'{b:02X}' for b in frame)
    print(f"  [{tag:>4}] {cmd_name:30s} | {hex_str}")
    # Also show decoded data section
    ds = ' '.join(f'{b:02X}' for b in data_48b)
    for row in range(3):
        start = row * 16
        print(f"         {'':30s} |   data[{row:1d}]: {ds[start*3:start*3+47]}")

def print_disable(cmd_name, cmd_word):
    data = bytes(48)
    frame, chk = build_advanced_write(cmd_word, data)
    hex_str = ' '.join(f'{b:02X}' for b in frame)
    print(f"  [DIS]  {cmd_name:30s} | {hex_str}")


# ============================================================
# Main
# ============================================================
def main():
    calib = load_calib()

    flash0 = _extract_readback_data(os.path.join(_PARENT_DIR, 'data', 'program_0.json'))
    flash1 = _extract_readback_data(os.path.join(_PARENT_DIR, 'data', 'program_1.json'))

    prog0 = parse_program_data(flash0)
    prog1 = parse_program_data(flash1)

    p0 = program_flash_to_dict(prog0)
    p1 = program_flash_to_dict(prog1)

    def get_it(sel):
        if sel == 2: return 'telecoil'
        elif sel == 3: return 'dai'
        return 'front_mic'

    it0 = get_it(p0['modules']['input_selection'])
    it1 = get_it(p1['modules']['input_selection'])

    print(f"Program 0: input={it0} (sel={p0['modules']['input_selection']}), "
          f"ch={p0['wdrc']['total_channels']}, kp={p0['wdrc']['kp_mode']}KP, "
          f"lim={'on' if p0['wdrc']['limiter'] else 'off'}")
    print(f"Program 1: input={it1} (sel={p1['modules']['input_selection']}), "
          f"ch={p1['wdrc']['total_channels']}, kp={p1['wdrc']['kp_mode']}KP, "
          f"lim={'on' if p1['wdrc']['limiter'] else 'off'}")

    # ================================================================
    # SCENARIO 1: Full sync — Program 0 first boot
    # ================================================================
    print()
    print("=" * 120)
    print("SCENARIO 1: 首次上电 — 全量同步 Program 0 所有参数到 BS300 RAM")
    print("=" * 120)
    print(f"Frame format: [Len] [Checksum] [Cmd0 Cmd1 Cmd2] [Data 48B]")
    print()

    full_cmds = encode_full_sync(p0, calib, it0)
    total_bytes = 0
    for i, (name, cmd, data) in enumerate(full_cmds):
        print(f"  #{i+1:2d}: ", end="")
        print_frame(name, cmd, data)
        total_bytes += 53  # frame size
    print(f"\n  Total: {len(full_cmds)} commands, {total_bytes} bytes over I2C")

    # ================================================================
    # SCENARIO 2: Switch Program 0 → Program 1
    # ================================================================
    print()
    print("=" * 120)
    print("SCENARIO 2: 程序切换 Program 0 → Program 1（增量同步）")
    print("=" * 120)

    # Show what changed
    print()
    print("  变化检测:")
    ow = p0['wdrc']; nw = p1['wdrc']
    om = p0['modules']; nm = p1['modules']
    oe = p0['enr']; ne = p1['enr']

    diffs = []
    if ow['bin_gains'] != nw['bin_gains']: diffs.append('WDRC bin_gains[32]')
    if om['input_selection'] != nm['input_selection']: diffs.append(f'input_selection: {om["input_selection"]}→{nm["input_selection"]}')
    if om['iss_threshold'] != nm['iss_threshold']: diffs.append(f'ISS threshold: {om["iss_threshold"]}→{nm["iss_threshold"]}')
    if om['agco_threshold_db'] != nm['agco_threshold_db']: diffs.append(f'AGCO thr: {om["agco_threshold_db"]}→{nm["agco_threshold_db"]}')
    if oe['max_att_db'] != ne['max_att_db']: diffs.append('ENR max_att_db[16]')
    igd_changed = (it0 != it1)
    if igd_changed: diffs.append(f'igd: {it0}→{it1} (input_type change)')

    for d in diffs:
        print(f"    * {d}")

    print()
    print(f"  I2C frames:")

    sent_cmds, disable_cmds = simulate_switch(p0, p1, it0, it1, calib)

    total_bytes = 0
    for i, (name, cmd, data) in enumerate(sent_cmds):
        print(f"  #{i+1:2d}: ", end="")
        print_frame(name, cmd, data)
        total_bytes += 53

    for name, cmd in disable_cmds:
        print(f"  #--: ", end="")
        print_disable(name, cmd)
        total_bytes += 53

    print(f"\n  Total: {len(sent_cmds)} send + {len(disable_cmds)} disable = "
          f"{len(sent_cmds)+len(disable_cmds)} frames, {total_bytes} bytes over I2C")
    print(f"  (vs full sync: {len(full_cmds)} frames, {len(full_cmds)*53} bytes)")

    reduction = (1 - (len(sent_cmds)+len(disable_cmds))/len(full_cmds)) * 100
    print(f"  节省: {reduction:.0f}% I2C 通信量")


if __name__ == '__main__':
    main()
