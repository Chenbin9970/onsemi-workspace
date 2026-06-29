"""Dump clean I2C hex commands for full sync and switch."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from bs300_codegen import *
from calib import _find_data_offset
from proto import bs300_cmd_pktnum, bs300_set_word
from math_utils import _freq_to_index, _time_to_index, _ratio_to_index

_BEEP_FREQ_MAP = {250:1,500:2,750:3,1000:4,1250:5,1500:6,1750:7,2000:8,2250:9,2500:10,2750:11,3000:12,3250:13}
def _beep_freq_to_data(hz): return _BEEP_FREQ_MAP.get(hz, 0)

def build_frame(cmd_word, data_48b):
    payload = bytearray([0x10, cmd_word & 0xFF, (cmd_word >> 8) & 0xFF, (cmd_word >> 16) & 0xFF])
    payload.extend(data_48b)
    chk = (0xFF - sum(payload) % 256) & 0xFF
    frame = bytearray([0x10, chk, cmd_word & 0xFF, (cmd_word >> 8) & 0xFF, (cmd_word >> 16) & 0xFF])
    frame.extend(data_48b)
    return bytes(frame)

def encode_program(prog_idx):
    PARENT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with open(os.path.join(PARENT, 'data', f'param_values_{prog_idx}.json'), 'r', encoding='utf-8') as f:
        params = json.load(f)
    with open(os.path.join(PARENT, 'data', 'calibration.json'), 'r', encoding='utf-8') as f:
        raw_entries = json.load(f)
    data_sections = {}
    for entry in raw_entries:
        cmd = int(entry['cmd_word'], 16); pktnum = bs300_cmd_pktnum(cmd)
        if pktnum > 2: continue
        raw_bytes = bytes(int(h, 16) for h in entry['bytes'])
        off = _find_data_offset(raw_bytes)
        data_sections[pktnum] = raw_bytes[off: off + 48]
    raw = data_sections[0] + data_sections[1] + data_sections[2]
    calib = parse_calibration(raw)
    modules = {m['name']: m for m in params['value_in_MT']['modules']}
    im = next((m for m in modules.keys() if m.startswith('Input(')), None)
    it = 'telecoil'
    if im: it = im.replace('Input(', '').replace(')', '').lower().replace(' ', '_')
    wdrc = modules['WDRC']; enr = modules['ENR']; num_ch = wdrc['num_channels']

    R = {}
    def add(cmd, data): R[cmd] = bytes(data)

    add(0x8000B2, encode_wdrc_general_param(num_ch, 0, '2KP', wdrc['output_limiting']))
    add(0x8010B2, encode_wdrc_freq_spacing_param([2] * num_ch))
    kp1=[ch['kp1_th_db'] for ch in wdrc['channels']]; kp2=[ch['kp2_th_db'] for ch in wdrc['channels']]
    wfi=[_freq_to_index(ch['freq_hz']) for ch in wdrc['channels']]
    kpo=[0]*16; kpo[3]=1; kpo[6]=1
    add(0x8020B2, encode_wdrc_kp_threshold_param(kp1, calib, '2KP', it, kp2, wfi, kpo))
    eat=[_time_to_index(ch['epd_at_ms']) for ch in wdrc['channels']]; k1at=[_time_to_index(ch['kp1_at_ms']) for ch in wdrc['channels']]; k2at=[_time_to_index(ch['kp2_at_ms']) for ch in wdrc['channels']]
    add(0x8030B2, encode_wdrc_attack_time_param(eat, k1at, '2KP', k2at))
    ert=[_time_to_index(ch['epd_rt_ms']) for ch in wdrc['channels']]; k1rt=[_time_to_index(ch['kp1_rt_ms']) for ch in wdrc['channels']]; k2rt=[_time_to_index(ch['kp2_rt_ms']) for ch in wdrc['channels']]
    add(0x8040B2, encode_wdrc_release_time_param(ert, k1rt, '2KP', k2rt))
    eri=[_ratio_to_index(ch['epd_ratio']) for ch in wdrc['channels']]; k1ri=[_ratio_to_index(ch['kp1_ratio']) for ch in wdrc['channels']]; k2ri=[_ratio_to_index(ch['kp2_ratio']) for ch in wdrc['channels']]
    add(0x8050B2, encode_wdrc_ratio_param(eri, k1ri, '2KP', k2ri))
    add(0x8060B2, encode_wdrc_bin_gain_param(wdrc['bin_gains'], calib, it))
    lmt=[ch['lmt_th_db'] for ch in wdrc['channels']]; lmo=[0]*16; lmo[3]=1; lmo[9]=1
    add(0x8070B2, encode_wdrc_lmt_threshold_param(lmt, calib, wfi, lmo))
    add(0x8080B2, encode_wdrc_lmt_atk_time_param([_time_to_index(ch['lmt_at_ms']) for ch in wdrc['channels']]))
    add(0x8090B2, encode_wdrc_lmt_rel_time_param([_time_to_index(ch['lmt_rt_ms']) for ch in wdrc['channels']]))
    add(0x80A0B2, encode_wdrc_lmt_ratio_param([_ratio_to_index(ch['lmt_ratio']) for ch in wdrc['channels']]))

    vol = modules['Volume/Beep']
    bfd = _beep_freq_to_data(vol['beep_frequency']); bfd2 = _beep_freq_to_data(vol['batt_flat_beep_freq'])
    add(0x800081, encode_volume_beep_param(vol['beep_level'], bfd, vol['min_volume'], vol['max_volume'],
        {'front_mic':0,'rear_mic':0,'telecoil':1,'dai':1}.get(it,0), vol['batt_flat_beep_level'], bfd2, calib))
    if it in ('telecoil','dai'):
        add(0x804272, encode_tc_dai_gain_diff_param(calib.telecoil_gain_diff, calib))
    else:
        add(0x804272, bytes(48))

    dfbc = modules['DFBC']
    dm = {'Slow FBC':0x01,'Slow Weak':0x03,'Slow Strong':0x07,'Fast FBC':0x09,'Fast Weak':0x0B,'Fast Strong DFBC':0x0F}
    add(0x800052, encode_dfbc_param(dm.get(dfbc['mode'],0x0F), calib))
    iss = modules['ISS']
    add(0x8001B2, encode_iss_param(1, iss['threshold'], calib, it))
    wnr = modules['WNR']
    sm = {'Off':0,'Minimal':1,'Low':2,'Medium':3,'High':4}
    sl = sm.get(wnr['suppression_preset'],1)
    add(0x8001C2, encode_wnr_1_param(True, False, calib, sl+1))
    add(0x8011C2, encode_wnr_band_data_param(calib, 0, it, 0))
    add(0x8411C2, encode_wnr_band_data_param(calib, 0, it, 16))
    add(0x8021C2, encode_wnr_single_mic_detect_param(calib, 0, it))

    en = enr['num_channels']
    add(0x8000C2, encode_enr_general_param(1, en, 2, en-2))
    efi = [_freq_to_index(ch['freq_hz']) for ch in enr['channels']]
    ebc = [];
    for i,f in enumerate(efi):
        if i<len(efi)-1: ebc.append(efi[i+1]-f)
        else: ebc.append(32-f)
    add(0x8010C2, encode_enr_freq_spacing_param(ebc))
    add(0x8020C2, encode_enr_snr_threshold_param([ch['snr_th_db'] for ch in enr['channels']]))
    mal = [ch['max_att_db'] for ch in enr['channels']]; snrl = [ch['snr_th_db'] for ch in enr['channels']]
    add(0x8030C2, encode_enr_max_att_param(mal, snrl))
    add(0x8040C2, encode_enr_noise_th_param([ch['noise_th_db'] for ch in enr['channels']], calib, it, efi, ebc))
    add(0x8050C2, encode_enr_upper_noise_th_param([ch['upper_noise_th_db'] for ch in enr['channels']], calib, it, efi, ebc))
    add(0x8060C2, encode_enr_smoothing_param({'nhsf':enr['nhsf'],'nfsf':enr['nfsf'],'nnsf':enr['nnsf'],'snasf':4}))
    add(0x8070C2, encode_enr_etr_param([ch['exp_trans_ratio'] for ch in enr['channels']], mal))
    add(0x8080C2, encode_enr_nrr_param([ch['noise_red_ratio'] for ch in enr['channels']], mal))
    add(0x8090C2, encode_enr_sasf_param([8]*en))
    agco = modules['AGCO']
    atk = agco.get('attack_time_01ms', agco.get('attack_time_ms')); rel = agco.get('release_time_01ms', agco.get('release_time_ms'))
    add(0x800382, encode_agco_param(1, agco['threshold_db'], atk, rel))

    # Disabled modules — always send
    z = bytearray(48); bs300_set_word(z, 0, 0); zb = bytes(z)
    add(0x800062, zb); add(0x800022, zb); add(0x800172, zb)
    return R


NAMES = {
    0x800022:'DDM2',0x800052:'DFBC',0x800062:'MM Plus',0x800081:'Volume/Beep',
    0x8000B2:'WDRC General',0x8000C2:'ENR General',0x800172:'Noise Gen2',
    0x8001B2:'ISS',0x8001C2:'WNR Setup',0x800382:'AGCO',
    0x8010B2:'WDRC Freq',0x8010C2:'ENR Freq',0x8011C2:'WNR Band 0-15',
    0x8020B2:'WDRC KP Thr',0x8020C2:'ENR SNR Thr',0x8021C2:'WNR Single Mic',
    0x8030B2:'WDRC Attack',0x8030C2:'ENR Max Att',
    0x8040B2:'WDRC Release',0x8040C2:'ENR Noise Thr',0x804272:'TC/DAI',
    0x8050B2:'WDRC Ratio',0x8050C2:'ENR Up Noise',
    0x8060B2:'WDRC Bin Gain',0x8060C2:'ENR Smooth',
    0x8070B2:'WDRC Lmt Thr',0x8070C2:'ENR ETR',
    0x8080B2:'WDRC Lmt Atk',0x8080C2:'ENR NRR',
    0x8090B2:'WDRC Lmt Rel',0x8090C2:'ENR SASF',
    0x80A0B2:'WDRC Lmt Ratio',0x8411C2:'WNR Band 16-31',
}

# ===== Full Sync =====
for prog_idx in [0, 1]:
    R = encode_program(prog_idx)
    it = 'telecoil' if prog_idx == 0 else 'front_mic'
    print(f'===== Program {prog_idx} 全量同步 ({it})  {len(R)} 帧 =====')
    for cmd in sorted(R.keys()):
        frm = build_frame(cmd, R[cmd])
        print(f'  {NAMES.get(cmd,"?"):22s} 0x{cmd:06X} | {frm.hex(" ").upper()}')
    print()

# ===== Switch =====
p0 = encode_program(0); p1 = encode_program(1)
print(f'===== Program 0 → 1 增量切换 =====')
n = 0
for cmd in sorted(set(list(p0.keys()) + list(p1.keys()))):
    d0 = p0.get(cmd, bytes(48)); d1 = p1.get(cmd, bytes(48))
    if d0 == d1: continue
    frm = build_frame(cmd, d1)
    name = NAMES.get(cmd, '?')
    p0a = any(d0[i] != 0 for i in range(3)); p1a = any(d1[i] != 0 for i in range(3))
    tag = 'DIS' if (p0a and not p1a) else 'SND'
    n += 1
    print(f'  [{tag}] #{n:2d} {name:22s} 0x{cmd:06X} | {frm.hex(" ").upper()}')
print(f'  共 {n} 帧 (全量 {len(p0)} 条)')
