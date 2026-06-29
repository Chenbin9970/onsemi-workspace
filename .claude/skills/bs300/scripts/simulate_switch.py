"""
Simulate bs300_switch_program: read Program 0 + 1 Flash data,
decode to struct, run C switch logic, report which commands would be sent.
"""
import json, os, sys

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PARENT_DIR = os.path.dirname(_SCRIPT_DIR)
sys.path.insert(0, _PARENT_DIR)

from flash_read import parse_program_data, ProgramData, WdrcFlash, EnrFlash
from flash_write import _extract_readback_data
from crossval_c_vs_py import (
    C, load_calib, load_params, _FREQ_TABLE_C, _WDRC_CAL_OFFSET_C,
    _WNR_SSP_OFFSET_C, _WNR_DATA2_OFFSET_C, _WNR_PRESET_TO_SSP_C,
    BEEP_FRAC24, ISS_FRAC48, MIC2_CAL_FRAC24, AGCO_EXP, MM_PLUS_FRAC24,
    c_igd, c_freq_to_cal_band, c_db_to_frac24, c_db_to_int24,
    c_clamp, c_apply_igd_trunc, c_enr_nt_int, c_mic1_cal_avg,
    c_trunc_div, c_round_div,
    _BEEP_IDX_TO_HZ_C,
)
from bs300_codegen import _freq_to_index

# ============================================================
# Load Flash data
# ============================================================
def load_program_flash(pi):
    path = os.path.join(_PARENT_DIR, 'data', f'program_{pi}.json')
    return _extract_readback_data(path)

# ============================================================
# Convert Python flash_read types → C struct dicts
# ============================================================
def wdrc_flash_to_struct(wf: WdrcFlash):
    """Convert Python WdrcFlash → dict matching C bs300_wdrc_t."""
    nch = wf.num_channels
    s = {
        'total_channels': nch,
        'nsbc': 0,
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
        'bin_gains': list(wf.bin_gain) if len(wf.bin_gain) == 32 else list(wf.bin_gain) + [0]*(32-len(wf.bin_gain)),
        'channels': [],
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
        s['lmt_th_db'][i] = ch.lmt_th + 30   # Flash stores value-30
        s['lmt_at_idx'][i] = ch.lmt_at
        s['lmt_rt_idx'][i] = ch.lmt_rt
        s['lmt_r_idx'][i] = ch.lmt_r
    s['num_channels'] = nch
    return s

def enr_flash_to_struct(ef: EnrFlash):
    """Convert Python EnrFlash → dict matching C bs300_enr_t."""
    nch = ef.num_channels
    s = {
        'enable_num_ch': 0x80 | (nch & 0x3F),
        'nfsf': ef.nfsf + 1,
        'nhsf': ef.nhsf + 1,
        'nnsf': ef.nnsf + 1,
        'snasf': ef.snasf + 1,
        'freq_idx': [0]*16,
        'snr_th_db': [0]*16, 'max_att_db': [0]*16,
        'noise_th_db': [0]*16, 'upper_noise_th_db': [0]*16,
        'etr_x100': [0]*16, 'nrr_x10': [0]*16, 'sasf': [8]*16,
        'num_channels': nch,
        'channels': [],
    }
    for i, ch in enumerate(ef.channels):
        if i >= 16: break
        s['freq_idx'][i] = ch.frequency_idx
        s['snr_th_db'][i] = ch.snrth
        s['max_att_db'][i] = ch.ma
        s['noise_th_db'][i] = ch.nt + 10      # Flash stores value-10
        s['upper_noise_th_db'][i] = ch.unt + 40  # Flash stores value-40
        s['etr_x100'][i] = ch.etr
        s['nrr_x10'][i] = ch.nrr
    return s

def input_type_to_selection(it: str) -> int:
    """Convert InputsFlash input_type string → input_selection integer."""
    return {'front_mic': 0, 'rear_mic': 1, 'telecoil': 2, 'dai': 3,
            'mm_plus': 4, 'ddm2': 5, 'dual_mic': 6}.get(it, 0)

def program_flash_to_dict(prog: ProgramData):
    """Convert ProgramData → dict with 'wdrc', 'enr', 'modules' keys."""
    wdrc = wdrc_flash_to_struct(prog.wdrc) if prog.wdrc else None
    enr = enr_flash_to_struct(prog.enr) if prog.enr else None

    inp = prog.inputs
    vol = prog.volume
    iss = prog.iss
    wnr = prog.wnr
    agco = prog.agco
    dfbc = prog.dfbc

    input_sel = input_type_to_selection(inp.input_type) if inp else 0

    def hz_to_beep_idx(hz):
        for idx, f in enumerate(_BEEP_IDX_TO_HZ_C):
            if f == hz: return idx
        return 3  # default 1000Hz

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
        'wnr_enable_dual': (0x01 | (wnr.dual_mic_mode_sel << 1)) if wnr else 0,
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


# ============================================================
# Simulate switch logic
# ============================================================
def simulate_switch(old_p, new_p, calib, old_it, new_it):
    ow = old_p['wdrc']; nw = new_p['wdrc']
    om = old_p['modules']; nm = new_p['modules']
    oe = old_p['enr']; ne = new_p['enr']
    igd_changed = (old_it != new_it)

    sent_cmds = []
    disable_cmds = []
    skip_reasons = {}

    def send(cmd_name, cmd_hex):
        sent_cmds.append((cmd_name, cmd_hex))

    def disable(cmd_name, cmd_hex):
        disable_cmds.append((cmd_name, cmd_hex))

    # ==================== WDRC ====================
    hdr_changed = (ow['total_channels'] != nw['total_channels']) \
               or (ow['kp_mode'] != nw['kp_mode']) \
               or (ow['limiter'] != nw['limiter'])
    freq_changed = (ow['freq_idx'] != nw['freq_idx'])
    kp1_changed = (ow['kp1_th_db'] != nw['kp1_th_db'])
    kp2_changed = (ow['kp2_th_db'] != nw['kp2_th_db'])
    at_changed = (ow['epd_at_idx'] != nw['epd_at_idx']) \
              or (ow['kp1_at_idx'] != nw['kp1_at_idx']) \
              or (ow['kp2_at_idx'] != nw['kp2_at_idx'])
    rt_changed = (ow['epd_rt_idx'] != nw['epd_rt_idx']) \
              or (ow['kp1_rt_idx'] != nw['kp1_rt_idx']) \
              or (ow['kp2_rt_idx'] != nw['kp2_rt_idx'])
    ratio_changed = (ow['epd_r_idx'] != nw['epd_r_idx']) \
                 or (ow['kp1_r_idx'] != nw['kp1_r_idx']) \
                 or (ow['kp2_r_idx'] != nw['kp2_r_idx'])
    bg_changed = (ow['bin_gains'] != nw['bin_gains'])
    lmt_th_changed = (ow['lmt_th_db'] != nw['lmt_th_db'])
    lmt_at_changed = (ow['lmt_at_idx'] != nw['lmt_at_idx'])
    lmt_rt_changed = (ow['lmt_rt_idx'] != nw['lmt_rt_idx'])
    lmt_r_changed  = (ow['lmt_r_idx']  != nw['lmt_r_idx'])

    if hdr_changed:
        send('WDRC General Setup', '0x8000B2')
    else:
        skip_reasons['WDRC General Setup'] = 'hdr unchanged'

    if hdr_changed or freq_changed:
        send('WDRC Freq Spacing', '0x8010B2')
    else:
        skip_reasons['WDRC Freq Spacing'] = 'hdr+freq unchanged'

    if hdr_changed or freq_changed or kp1_changed or kp2_changed or igd_changed:
        send('WDRC KP Threshold', '0x8020B2')
    else:
        skip_reasons['WDRC KP Threshold'] = 'kp+igd unchanged'

    if hdr_changed or at_changed:
        send('WDRC Attack Time', '0x8030B2')
    else:
        skip_reasons['WDRC Attack Time'] = 'attack unchanged'

    if hdr_changed or rt_changed:
        send('WDRC Release Time', '0x8040B2')
    else:
        skip_reasons['WDRC Release Time'] = 'release unchanged'

    if hdr_changed or ratio_changed:
        send('WDRC Ratio', '0x8050B2')
    else:
        skip_reasons['WDRC Ratio'] = 'ratio unchanged'

    if bg_changed or igd_changed:
        send('WDRC Bin Gain', '0x8060B2')
    else:
        skip_reasons['WDRC Bin Gain'] = 'bg+igd unchanged'

    # Limiter ON/OFF state machine
    if ow['limiter'] == 0 and nw['limiter'] == 0:
        skip_reasons['WDRC Lmt (all)'] = 'both limiter=OFF'
    elif ow['limiter'] == 0 and nw['limiter'] == 1:
        send('WDRC Lmt Threshold', '0x8070B2')
        send('WDRC Lmt Attack', '0x8080B2')
        send('WDRC Lmt Release', '0x8090B2')
        send('WDRC Lmt Ratio', '0x80A0B2')
    elif ow['limiter'] == 1 and nw['limiter'] == 0:
        disable('WDRC Lmt Threshold', '0x8070B2')
        disable('WDRC Lmt Attack', '0x8080B2')
        disable('WDRC Lmt Release', '0x8090B2')
        disable('WDRC Lmt Ratio', '0x80A0B2')
    else:
        if freq_changed or lmt_th_changed:
            send('WDRC Lmt Threshold', '0x8070B2')
        else:
            skip_reasons['WDRC Lmt Threshold'] = 'freq+lmt_th unchanged'
        if lmt_at_changed:
            send('WDRC Lmt Attack', '0x8080B2')
        else:
            skip_reasons['WDRC Lmt Attack'] = 'lmt_at unchanged'
        if lmt_rt_changed:
            send('WDRC Lmt Release', '0x8090B2')
        else:
            skip_reasons['WDRC Lmt Release'] = 'lmt_rt unchanged'
        if lmt_r_changed:
            send('WDRC Lmt Ratio', '0x80A0B2')
        else:
            skip_reasons['WDRC Lmt Ratio'] = 'lmt_ratio unchanged'

    # ==================== Volume/Beep ====================
    o_vol_ena = om['vol_enable']; n_vol_ena = nm['vol_enable']
    if o_vol_ena == 0 and n_vol_ena == 0:
        skip_reasons['Volume/Beep'] = 'both OFF'
    elif o_vol_ena == 0 and n_vol_ena == 1:
        send('Volume/Beep/Input', '0x800081')
    elif o_vol_ena == 1 and n_vol_ena == 0:
        disable('Volume/Beep/Input', '0x800081')
    else:
        vol_changed = (om['beep_level'] != nm['beep_level']) \
                   or (om['beep_freq_idx'] != nm['beep_freq_idx']) \
                   or (om['min_vol'] != nm['min_vol']) \
                   or (om['max_vol'] != nm['max_vol']) \
                   or (om['input_selection'] != nm['input_selection']) \
                   or (om['batt_beep_level'] != nm['batt_beep_level']) \
                   or (om['batt_beep_freq_idx'] != nm['batt_beep_freq_idx'])
        if vol_changed or igd_changed:
            send('Volume/Beep/Input', '0x800081')
        else:
            skip_reasons['Volume/Beep'] = 'all fields+igd unchanged'

    # ==================== ENR ====================
    oe_ena = (oe['enable_num_ch'] & 0x80) != 0
    ne_ena = (ne['enable_num_ch'] & 0x80) != 0

    if oe_ena == 0 and ne_ena == 0:
        skip_reasons['ENR (all)'] = 'both OFF'
    elif oe_ena == 0 and ne_ena == 1:
        for cmd_name, cmd_hex in [
            ('ENR General Setup', '0x8000C2'), ('ENR Freq Spacing', '0x8010C2'),
            ('ENR SNR Thr', '0x8020C2'), ('ENR Max Att', '0x8030C2'),
            ('ENR Noise Thr', '0x8040C2'), ('ENR Upper Noise Thr', '0x8050C2'),
            ('ENR Smoothing', '0x8060C2'), ('ENR ETR', '0x8070C2'),
            ('ENR NRR', '0x8080C2'), ('ENR SASF', '0x8090C2'),
        ]:
            send(cmd_name, cmd_hex)
    elif oe_ena == 1 and ne_ena == 0:
        disable('ENR General Setup', '0x8000C2')
    else:
        enr_hdr_changed = (oe['enable_num_ch'] != ne['enable_num_ch'])
        enr_freq_changed = (oe['freq_idx'] != ne['freq_idx'])
        snr_changed = (oe['snr_th_db'] != ne['snr_th_db'])
        ma_changed = (oe['max_att_db'] != ne['max_att_db'])
        nt_changed = (oe['noise_th_db'] != ne['noise_th_db'])
        unt_changed = (oe['upper_noise_th_db'] != ne['upper_noise_th_db'])
        sf_changed = (oe['nfsf'] != ne['nfsf']) or (oe['nhsf'] != ne['nhsf']) \
                  or (oe['nnsf'] != ne['nnsf']) or (oe['snasf'] != ne['snasf'])
        etr_changed = (oe['etr_x100'] != ne['etr_x100'])
        nrr_changed = (oe['nrr_x10'] != ne['nrr_x10'])
        sasf_changed = (oe['sasf'] != ne['sasf'])

        if enr_hdr_changed: send('ENR General Setup', '0x8000C2')
        else: skip_reasons['ENR General Setup'] = 'hdr unchanged'
        if enr_hdr_changed or enr_freq_changed: send('ENR Freq Spacing', '0x8010C2')
        else: skip_reasons['ENR Freq Spacing'] = 'hdr+freq unchanged'
        if snr_changed: send('ENR SNR Thr', '0x8020C2')
        else: skip_reasons['ENR SNR Thr'] = 'snr unchanged'
        if snr_changed or ma_changed: send('ENR Max Att', '0x8030C2')
        else: skip_reasons['ENR Max Att'] = 'snr+ma unchanged'
        if enr_freq_changed or nt_changed or igd_changed: send('ENR Noise Thr', '0x8040C2')
        else: skip_reasons['ENR Noise Thr'] = 'freq+nt+igd unchanged'
        if enr_freq_changed or unt_changed or igd_changed: send('ENR Upper Noise Thr', '0x8050C2')
        else: skip_reasons['ENR Upper Noise Thr'] = 'freq+unt+igd unchanged'
        if sf_changed: send('ENR Smoothing', '0x8060C2')
        else: skip_reasons['ENR Smoothing'] = 'sf unchanged'
        if snr_changed or ma_changed or etr_changed: send('ENR ETR', '0x8070C2')
        else: skip_reasons['ENR ETR'] = 'snr+ma+etr unchanged'
        if snr_changed or ma_changed or nrr_changed: send('ENR NRR', '0x8080C2')
        else: skip_reasons['ENR NRR'] = 'snr+ma+nrr unchanged'
        if sasf_changed: send('ENR SASF', '0x8090C2')
        else: skip_reasons['ENR SASF'] = 'sasf unchanged'

    # ==================== DFBC ====================
    o_dfbc = (om['dfbc_enable_mode'] & 0x80) != 0
    n_dfbc = (nm['dfbc_enable_mode'] & 0x80) != 0
    if o_dfbc == 0 and n_dfbc == 0:
        skip_reasons['DFBC'] = 'both OFF'
    elif o_dfbc == 0 and n_dfbc == 1:
        send('DFBC', '0x800052')
    elif o_dfbc == 1 and n_dfbc == 0:
        disable('DFBC', '0x800052')
    else:
        if om['dfbc_enable_mode'] != nm['dfbc_enable_mode']:
            send('DFBC', '0x800052')
        else:
            skip_reasons['DFBC'] = 'mode unchanged'

    # ==================== ISS ====================
    if om['iss_enable'] == 0 and nm['iss_enable'] == 0:
        skip_reasons['ISS'] = 'both OFF'
    elif om['iss_enable'] == 0 and nm['iss_enable'] == 1:
        send('ISS', '0x8001B2')
    elif om['iss_enable'] == 1 and nm['iss_enable'] == 0:
        disable('ISS', '0x8001B2')
    else:
        if om['iss_threshold'] != nm['iss_threshold'] or igd_changed:
            send('ISS', '0x8001B2')
        else:
            skip_reasons['ISS'] = 'threshold+igd unchanged'

    # ==================== WNR ====================
    o_wnr = om['wnr_enable_dual'] & 0x01
    n_wnr = nm['wnr_enable_dual'] & 0x01
    if o_wnr == 0 and n_wnr == 0:
        skip_reasons['WNR (all)'] = 'both OFF'
    elif o_wnr == 0 and n_wnr == 1:
        for cmd_name, cmd_hex in [
            ('WNR Setup', '0x8001C2'), ('WNR Band 0-15', '0x8011C2'),
            ('WNR Band 16-31', '0x8411C2'), ('WNR Single Mic', '0x8021C2'),
        ]:
            send(cmd_name, cmd_hex)
    elif o_wnr == 1 and n_wnr == 0:
        disable('WNR Setup', '0x8001C2')
    else:
        if om['wnr_preset'] != nm['wnr_preset'] or igd_changed:
            for cmd_name, cmd_hex in [
                ('WNR Setup', '0x8001C2'), ('WNR Band 0-15', '0x8011C2'),
                ('WNR Band 16-31', '0x8411C2'), ('WNR Single Mic', '0x8021C2'),
            ]:
                send(cmd_name, cmd_hex)
        else:
            skip_reasons['WNR (all)'] = 'preset+igd unchanged'

    # ==================== AGCO ====================
    if om['agco_enable'] == 0 and nm['agco_enable'] == 0:
        skip_reasons['AGCO'] = 'both OFF'
    elif om['agco_enable'] == 0 and nm['agco_enable'] == 1:
        send('AGCO', '0x800382')
    elif om['agco_enable'] == 1 and nm['agco_enable'] == 0:
        disable('AGCO', '0x800382')
    else:
        agco_changed = (om['agco_threshold_db'] != nm['agco_threshold_db']) \
                    or (om['agco_attack_01ms'] != nm['agco_attack_01ms']) \
                    or (om['agco_release_01ms'] != nm['agco_release_01ms'])
        if agco_changed:
            send('AGCO', '0x800382')
        else:
            skip_reasons['AGCO'] = 'params unchanged'

    # ==================== Input Source ====================
    input_changed = (om['input_selection'] != nm['input_selection'])

    # MM Plus (input=4)
    o_mm = om['input_selection'] == 4
    n_mm = nm['input_selection'] == 4
    if o_mm == 0 and n_mm == 0:
        pass
    elif o_mm == 0 and n_mm == 1:
        send('MM Plus', '0x800062')
    elif o_mm == 1 and n_mm == 0:
        disable('MM Plus', '0x800062')
    else:
        if om['mix_ratio'] != nm['mix_ratio'] or igd_changed:
            send('MM Plus', '0x800062')
        else:
            skip_reasons['MM Plus'] = 'mix_ratio+igd unchanged'

    # DDM2 (input=5)
    o_dd = om['input_selection'] == 5
    n_dd = nm['input_selection'] == 5
    if o_dd == 0 and n_dd == 0:
        pass
    elif o_dd == 0 and n_dd == 1:
        send('DDM2', '0x800022')
    elif o_dd == 1 and n_dd == 0:
        disable('DDM2', '0x800022')
    else:
        ddm2_changed = (om['open_ear'] != nm['open_ear']) \
                    or (om['polar_pattern'] != nm['polar_pattern']) \
                    or (om['adm_fdm'] != nm['adm_fdm'])
        if ddm2_changed:
            send('DDM2', '0x800022')
        else:
            skip_reasons['DDM2'] = 'params unchanged'

    # TC/DAI (input=2 or 3)
    o_tc = om['input_selection'] in (2, 3)
    n_tc = nm['input_selection'] in (2, 3)
    if o_tc == 0 and n_tc == 0:
        pass
    elif o_tc == 0 and n_tc == 1:
        send('TC/DAI Gain Diff', '0x804272')
    elif o_tc == 1 and n_tc == 0:
        disable('TC/DAI Gain Diff', '0x804272')
    else:
        if input_changed or igd_changed:
            send('TC/DAI Gain Diff', '0x804272')
        else:
            skip_reasons['TC/DAI'] = 'input+igd unchanged'

    return sent_cmds, disable_cmds, skip_reasons


# ============================================================
# Main
# ============================================================
def main():
    calib = load_calib()
    print(f"Calibration: mic1_avg={calib.avg_mic1_cal():.1f} out_avg={calib.avg_output_cal():.1f} "
          f"mic2_gd={calib.mic2_gain_diff} mic_delay={calib.mic_delay} "
          f"tc_gd={calib.telecoil_gain_diff} dai_gd={calib.dai_gain_diff}")
    print()

    flash0 = load_program_flash(0)
    flash1 = load_program_flash(1)

    prog0_py = parse_program_data(flash0)
    prog1_py = parse_program_data(flash1)

    old_p = program_flash_to_dict(prog0_py)
    new_p = program_flash_to_dict(prog1_py)

    print("=" * 70)
    print("FLASH DATA COMPARISON")
    print("=" * 70)

    # Print key differences
    ow = old_p['wdrc']; nw = new_p['wdrc']
    om = old_p['modules']; nm = new_p['modules']
    oe = old_p['enr']; ne = new_p['enr']

    print(f"\n--- WDRC ---")
    print(f"  channels:       {ow['total_channels']:>3} → {nw['total_channels']:>3} {'*' if ow['total_channels'] != nw['total_channels'] else ''}")
    print(f"  kp_mode:        {ow['kp_mode']:>3} → {nw['kp_mode']:>3} {'*' if ow['kp_mode'] != nw['kp_mode'] else ''}")
    print(f"  limiter:        {ow['limiter']:>3} → {nw['limiter']:>3} {'*' if ow['limiter'] != nw['limiter'] else ''}")
    print(f"  freq_idx:       changed={ow['freq_idx'] != nw['freq_idx']}")
    print(f"  kp1_th_db:      changed={ow['kp1_th_db'] != nw['kp1_th_db']}")
    print(f"  kp2_th_db:      changed={ow['kp2_th_db'] != nw['kp2_th_db']}")
    print(f"  bin_gains:      changed={ow['bin_gains'] != nw['bin_gains']}")
    print(f"  lmt_th_db:      changed={ow['lmt_th_db'] != nw['lmt_th_db']}")
    print(f"  attack indices: changed={ow['epd_at_idx'] != nw['epd_at_idx'] or ow['kp1_at_idx'] != nw['kp1_at_idx']}")
    print(f"  release indices:changed={ow['epd_rt_idx'] != nw['epd_rt_idx'] or ow['kp1_rt_idx'] != nw['kp1_rt_idx']}")
    print(f"  ratio indices:  changed={ow['epd_r_idx'] != nw['epd_r_idx'] or ow['kp1_r_idx'] != nw['kp1_r_idx']}")

    print(f"\n--- Volume/Beep ---")
    for k in ['vol_enable', 'beep_level', 'beep_freq_idx', 'min_vol', 'max_vol',
              'input_selection', 'batt_beep_level', 'batt_beep_freq_idx']:
        ov = om[k]; nv = nm[k]
        ch = ' *' if ov != nv else ''
        print(f"  {k:25s}: {str(ov):>5} → {str(nv):>5}{ch}")

    print(f"\n--- ENR ---")
    oe_ena = (oe['enable_num_ch'] & 0x80) != 0
    ne_ena = (ne['enable_num_ch'] & 0x80) != 0
    print(f"  enable:         {oe_ena:>5} → {ne_ena:>5} {'*' if oe_ena != ne_ena else ''}")
    print(f"  num_ch:         {oe['enable_num_ch'] & 0x3F:>5} → {ne['enable_num_ch'] & 0x3F:>5}")
    print(f"  freq_idx:       changed={oe['freq_idx'] != ne['freq_idx']}")
    print(f"  snr_th_db:      changed={oe['snr_th_db'] != ne['snr_th_db']}")
    print(f"  max_att_db:     changed={oe['max_att_db'] != ne['max_att_db']}")
    print(f"  noise_th_db:    changed={oe['noise_th_db'] != ne['noise_th_db']}")
    print(f"  upper_noise_th: changed={oe['upper_noise_th_db'] != ne['upper_noise_th_db']}")

    print(f"\n--- Other Modules ---")
    for k in ['dfbc_enable_mode', 'iss_enable', 'iss_threshold', 'wnr_enable_dual',
              'wnr_preset', 'agco_enable', 'agco_threshold_db']:
        ov = om[k]; nv = nm[k]
        ch = ' *' if ov != nv else ''
        print(f"  {k:25s}: {str(ov):>5} → {str(nv):>5}{ch}")

    # Determine input types
    def get_input_type(input_sel):
        if input_sel == 2: return 'telecoil'
        elif input_sel == 3: return 'dai'
        return 'front_mic'

    old_it = get_input_type(om['input_selection'])
    new_it = get_input_type(nm['input_selection'])
    igd_changed = (old_it != new_it)
    print(f"\n  input_type:     {old_it} → {new_it} (igd_changed={igd_changed})")

    # ==================== SIMULATE SWITCH ====================
    print()
    print("=" * 70)
    print("SIMULATED SWITCH: Program 0 → Program 1")
    print("=" * 70)

    sent_cmds, disable_cmds, skip_reasons = simulate_switch(old_p, new_p, calib, old_it, new_it)

    print(f"\n>>> SEND ({len(sent_cmds)} commands):")
    for name, cmd in sent_cmds:
        print(f"    SEND  {name:30s}  {cmd}")

    if disable_cmds:
        print(f"\n>>> DISABLE ({len(disable_cmds)} commands):")
        for name, cmd in disable_cmds:
            print(f"    DIS   {name:30s}  {cmd}")

    if 0:
        print(f"\n>>> SKIPPED ({len(skip_reasons)} commands):")
        for name, reason in skip_reasons.items():
            print(f"    SKIP  {name:30s}  ({reason})")

    # ==================== SHOW ACTUAL DATA FOR SENT COMMANDS ====================
    # Build wrapper dicts that match crossval C port expected format
    # The C port uses 'channels' list with 'freq_hz', while our Flash struct uses arrays
    def make_wdrc_wrapper(s):
        """Convert Flash struct dict → crossval C port format."""
        nch = s['total_channels']
        ch_list = []
        for i in range(nch):
            ch_list.append({
                'freq_hz': _FREQ_TABLE_C[s['freq_idx'][i]],
                'kp1_th_db': s['kp1_th_db'][i],
                'kp2_th_db': s['kp2_th_db'][i],
                'epd_at_ms': s['epd_at_idx'][i],
                'epd_rt_ms': s['epd_rt_idx'][i],
                'epd_ratio': s['epd_r_idx'][i],
                'kp1_at_ms': s['kp1_at_idx'][i],
                'kp2_at_ms': s['kp2_at_idx'][i],
                'kp1_rt_ms': s['kp1_rt_idx'][i],
                'kp2_rt_ms': s['kp2_rt_idx'][i],
                'kp1_ratio': s['kp1_r_idx'][i],
                'kp2_ratio': s['kp2_r_idx'][i],
                'lmt_th_db': s['lmt_th_db'][i],
                'lmt_at_ms': s['lmt_at_idx'][i],
                'lmt_rt_ms': s['lmt_rt_idx'][i],
                'lmt_ratio': s['lmt_r_idx'][i],
            })
        return {'num_channels': nch, 'channels': ch_list, 'bin_gains': s['bin_gains']}

    def make_enr_wrapper(s):
        """Convert Flash ENR struct dict → crossval C port format."""
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

    w = make_wdrc_wrapper(nw)
    e = make_enr_wrapper(ne)

    print()
    print("=" * 70)
    print("ENCODED 48B DATA FOR SENT COMMANDS (from new Program 1)")
    print("=" * 70)

    for name, cmd_hex in sent_cmds:
        data = bytearray(48)

        if name == 'WDRC KP Threshold':
            C.wdrc_kp_th(w, calib, new_it, _WDRC_CAL_OFFSET_C, data)
        elif name == 'WDRC Bin Gain':
            C.wdrc_bg(w, calib, new_it, nm['volume_level'],
                       nm['eq_low'], nm['eq_mid'], nm['eq_high'], data)
        elif name == 'Volume/Beep/Input':
            C.volume_beep({
                'beep_level': nm['beep_level'],
                'beep_freq_idx': nm['beep_freq_idx'],
                'min_vol': nm['min_vol'],
                'max_vol': nm['max_vol'],
                'input_selection': nm['input_selection'],
                'batt_beep_level': nm['batt_beep_level'],
                'batt_beep_freq_idx': nm['batt_beep_freq_idx'],
            }, calib, data)
        elif name.startswith('ENR'):
            if name == 'ENR Max Att':
                C.enr_max_att(e, data)
            elif name == 'ENR Noise Thr':
                C.enr_noise_th(e, calib, new_it, data)
            elif name == 'ENR Upper Noise Thr':
                C.enr_upper_noise_th(e, calib, new_it, data)
            elif name == 'ENR ETR':
                C.enr_etr(e, data)
            elif name == 'ENR NRR':
                C.enr_nrr(e, data)
            else:
                data = None
        elif name == 'ISS':
            C.iss({'threshold': nm['iss_threshold']}, calib, new_it, data)
        elif name.startswith('WNR'):
            wnr_en = {'dual_mic_mode': True, 'suppression_preset':
                {0: 'Minimal', 1: 'Light', 2: 'Medium', 3: 'Strong', 4: 'Maximal'}.get(nm['wnr_preset'], 'Minimal')}
            if name == 'WNR Setup':
                C.wnr_setup(wnr_en, calib, data)
            elif name == 'WNR Band 0-15':
                C.wnr_band(wnr_en, calib, new_it, 0, data)
            elif name == 'WNR Band 16-31':
                C.wnr_band(wnr_en, calib, new_it, 16, data)
            elif name == 'WNR Single Mic':
                C.wnr_single_mic(wnr_en, calib, new_it, data)
        elif name == 'AGCO':
            agco_c = {'_threshold_tenth_db': nm['agco_threshold_db'] * 10,
                       '_atk_01ms': nm['agco_attack_01ms'],
                       '_rel_01ms': nm['agco_release_01ms']}
            C.agco(agco_c, data)
        else:
            data = None

        if data is not None:
            hex_str = ' '.join(f'{b:02X}' for b in data)
            print(f"\n  {name} ({cmd_hex}):")
            for row in range(3):
                start = row * 16
                print(f"    {hex_str[start*3:start*3+47]}")
        else:
            print(f"\n  {name} ({cmd_hex}): [simple byte-level, see field comparison above]")

    print()
    print(f"Total: SEND={len(sent_cmds)}, DISABLE={len(disable_cmds)}, "
          f"SKIPPED={len(skip_reasons)}")


if __name__ == '__main__':
    main()
