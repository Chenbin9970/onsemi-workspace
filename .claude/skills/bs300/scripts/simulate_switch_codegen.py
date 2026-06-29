"""
Program 0 → Program 1 switch: compare codegen outputs, show only changed commands.
"""
import json, os, sys
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PARENT_DIR = os.path.dirname(_SCRIPT_DIR)
sys.path.insert(0, _PARENT_DIR)

from gen_i2c_from_codegen import *
from math_utils import _freq_to_index, _time_to_index, _ratio_to_index
from calib import _find_data_offset
from proto import bs300_cmd_pktnum
from bs300_codegen import *
_BEEP_FREQ_MAP = {250:1,500:2,750:3,1000:4,1250:5,1500:6,1750:7,2000:8,2250:9,2500:10,2750:11,3000:12,3250:13}
def _beep_freq_to_data(hz): return _BEEP_FREQ_MAP.get(hz, 0)

# Generate P0 and P1
all_results = {}
for prog_idx in [0, 1]:
    with open(os.path.join(_PARENT_DIR, 'data', f'param_values_{prog_idx}.json'), 'r', encoding='utf-8') as f:
        params = json.load(f)
    with open(os.path.join(_PARENT_DIR, 'data', 'calibration.json'), 'r', encoding='utf-8') as f:
        raw_entries = json.load(f)

    from calib import _find_data_offset
    from proto import bs300_cmd_pktnum

    data_sections = {}
    for entry in raw_entries:
        cmd = int(entry['cmd_word'], 16)
        pktnum = bs300_cmd_pktnum(cmd)
        if pktnum > 2: continue
        raw_bytes = bytes(int(h, 16) for h in entry['bytes'])
        off = _find_data_offset(raw_bytes)
        data_sections[pktnum] = raw_bytes[off: off + 48]
    raw = data_sections[0] + data_sections[1] + data_sections[2]
    calib = parse_calibration(raw)

    modules = {m['name']: m for m in params['value_in_MT']['modules']}
    input_module = next((m for m in modules.keys() if m.startswith('Input(')), None)
    input_type = 'telecoil'
    if input_module:
        input_type = input_module.replace('Input(', '').replace(')', '').lower().replace(' ', '_')

    wdrc = modules['WDRC']; enr = modules['ENR']
    num_ch = wdrc['num_channels']
    results = {}

    def add(name, cmd, data):
        results[cmd] = (name, bytes(data))

    # WDRC
    add('WDRC General Setup', 0x8000B2, encode_wdrc_general_param(num_ch, 0, '2KP', wdrc['output_limiting']))
    add('WDRC Freq Spacing', 0x8010B2, encode_wdrc_freq_spacing_param([2] * num_ch))

    kp1 = [ch['kp1_th_db'] for ch in wdrc['channels']]
    kp2 = [ch['kp2_th_db'] for ch in wdrc['channels']]
    wfi = [_freq_to_index(ch['freq_hz']) for ch in wdrc['channels']]
    kpo = [0]*16; kpo[3] = 1; kpo[6] = 1
    add('WDRC KP Threshold', 0x8020B2, encode_wdrc_kp_threshold_param(kp1, calib, '2KP', input_type, kp2, wfi, kpo))

    eat = [_time_to_index(ch['epd_at_ms']) for ch in wdrc['channels']]
    k1at = [_time_to_index(ch['kp1_at_ms']) for ch in wdrc['channels']]
    k2at = [_time_to_index(ch['kp2_at_ms']) for ch in wdrc['channels']]
    add('WDRC Attack Time', 0x8030B2, encode_wdrc_attack_time_param(eat, k1at, '2KP', k2at))

    ert = [_time_to_index(ch['epd_rt_ms']) for ch in wdrc['channels']]
    k1rt = [_time_to_index(ch['kp1_rt_ms']) for ch in wdrc['channels']]
    k2rt = [_time_to_index(ch['kp2_rt_ms']) for ch in wdrc['channels']]
    add('WDRC Release Time', 0x8040B2, encode_wdrc_release_time_param(ert, k1rt, '2KP', k2rt))

    eri = [_ratio_to_index(ch['epd_ratio']) for ch in wdrc['channels']]
    k1ri = [_ratio_to_index(ch['kp1_ratio']) for ch in wdrc['channels']]
    k2ri = [_ratio_to_index(ch['kp2_ratio']) for ch in wdrc['channels']]
    add('WDRC Ratio', 0x8050B2, encode_wdrc_ratio_param(eri, k1ri, '2KP', k2ri))

    add('WDRC Bin Gain', 0x8060B2, encode_wdrc_bin_gain_param(wdrc['bin_gains'], calib, input_type))

    lmt = [ch['lmt_th_db'] for ch in wdrc['channels']]
    lmo = [0]*16; lmo[3] = 1; lmo[9] = 1
    add('WDRC Lmt Threshold', 0x8070B2, encode_wdrc_lmt_threshold_param(lmt, calib, wfi, lmo))
    lat = [_time_to_index(ch['lmt_at_ms']) for ch in wdrc['channels']]
    add('WDRC Lmt Attack', 0x8080B2, encode_wdrc_lmt_atk_time_param(lat))
    lrt = [_time_to_index(ch['lmt_rt_ms']) for ch in wdrc['channels']]
    add('WDRC Lmt Release', 0x8090B2, encode_wdrc_lmt_rel_time_param(lrt))
    lri = [_ratio_to_index(ch['lmt_ratio']) for ch in wdrc['channels']]
    add('WDRC Lmt Ratio', 0x80A0B2, encode_wdrc_lmt_ratio_param(lri))

    # Volume/Beep
    vol = modules['Volume/Beep']
    bfd = _beep_freq_to_data(vol['beep_frequency'])
    bfd2 = _beep_freq_to_data(vol['batt_flat_beep_freq'])
    add('Volume/Beep/Input', 0x800081, encode_volume_beep_param(
        vol['beep_level'], bfd, vol['min_volume'], vol['max_volume'],
        {'front_mic': 0, 'rear_mic': 0, 'telecoil': 1, 'dai': 1}.get(input_type, 0),
        vol['batt_flat_beep_level'], bfd2, calib))

    # TC/DAI
    if input_type in ('telecoil', 'dai'):
        add('TC/DAI Gain Diff', 0x804272, encode_tc_dai_gain_diff_param(calib.telecoil_gain_diff, calib))
    else:
        add('TC/DAI Gain Diff', 0x804272, bytes(48))

    # DFBC
    dfbc = modules['DFBC']
    dfbc_map = {'Slow FBC': 0x01, 'Slow Weak': 0x03, 'Slow Strong': 0x07,
                'Fast FBC': 0x09, 'Fast Weak': 0x0B, 'Fast Strong DFBC': 0x0F}
    add('DFBC', 0x800052, encode_dfbc_param(dfbc_map.get(dfbc['mode'], 0x0F), calib))

    # ISS
    iss = modules['ISS']
    add('ISS', 0x8001B2, encode_iss_param(1, iss['threshold'], calib, input_type))

    # WNR
    wnr = modules['WNR']
    ssp_map = {'Off': 0, 'Minimal': 1, 'Low': 2, 'Medium': 3, 'High': 4}
    ssp_level = ssp_map.get(wnr['suppression_preset'], 1)
    add('WNR Setup', 0x8001C2, encode_wnr_1_param(True, False, calib, ssp_level + 1))
    add('WNR Band 0-15', 0x8011C2, encode_wnr_band_data_param(calib, 0, input_type, 0))
    add('WNR Band 16-31', 0x8411C2, encode_wnr_band_data_param(calib, 0, input_type, 16))
    add('WNR Single Mic', 0x8021C2, encode_wnr_single_mic_detect_param(calib, 0, input_type))

    # ENR
    enr_num = enr['num_channels']
    add('ENR General Setup', 0x8000C2, encode_enr_general_param(1, enr_num, 2, enr_num - 2))
    efi = [_freq_to_index(ch['freq_hz']) for ch in enr['channels']]
    ebc = []
    for i, f in enumerate(efi):
        if i < len(efi) - 1: ebc.append(efi[i+1] - f)
        else: ebc.append(32 - f)
    add('ENR Freq Spacing', 0x8010C2, encode_enr_freq_spacing_param(ebc))
    add('ENR SNR Threshold', 0x8020C2, encode_enr_snr_threshold_param([ch['snr_th_db'] for ch in enr['channels']]))
    mal = [ch['max_att_db'] for ch in enr['channels']]
    snrl = [ch['snr_th_db'] for ch in enr['channels']]
    add('ENR Max Attenuation', 0x8030C2, encode_enr_max_att_param(mal, snrl))
    add('ENR Noise Threshold', 0x8040C2, encode_enr_noise_th_param(
        [ch['noise_th_db'] for ch in enr['channels']], calib, input_type, efi, ebc))
    add('ENR Upper Noise Thr', 0x8050C2, encode_enr_upper_noise_th_param(
        [ch['upper_noise_th_db'] for ch in enr['channels']], calib, input_type, efi, ebc))
    add('ENR Smoothing', 0x8060C2, encode_enr_smoothing_param(
        {'nhsf': enr['nhsf'], 'nfsf': enr['nfsf'], 'nnsf': enr['nnsf'], 'snasf': 4}))
    add('ENR ETR', 0x8070C2, encode_enr_etr_param([ch['exp_trans_ratio'] for ch in enr['channels']], mal))
    add('ENR NRR', 0x8080C2, encode_enr_nrr_param([ch['noise_red_ratio'] for ch in enr['channels']], mal))
    add('ENR SASF', 0x8090C2, encode_enr_sasf_param([8] * enr_num))

    # AGCO
    agco = modules['AGCO']
    atk = agco.get('attack_time_01ms', agco.get('attack_time_ms'))
    rel = agco.get('release_time_01ms', agco.get('release_time_ms'))
    add('AGCO', 0x800382, encode_agco_param(1, agco['threshold_db'], atk, rel))

    all_results[prog_idx] = results

# ================================================================
# Compare P0 vs P1
# ================================================================
p0 = all_results[0]
p1 = all_results[1]
all_cmds = sorted(set(list(p0.keys()) + list(p1.keys())))

print(f'Program 0 (telecoil) → Program 1 (front_mic) 增量切换 I2C 命令')
print(f'对比逻辑: 逐 byte 比较 P0/P1 编码后的 48B data，相同则跳过')
print(f'Frame: [Len] [Checksum] [Cmd0 Cmd1 Cmd2] [Data 48B]')
print(f'=' * 120)

sent = 0
disabled = 0
skipped = 0

for cmd_word in all_cmds:
    name0, data0 = p0.get(cmd_word, ('?', bytes(48)))
    name1, data1 = p1.get(cmd_word, ('?', bytes(48)))

    if data0 == data1:
        skipped += 1
        continue

    frame, chk = build_frame(cmd_word, data1)
    frame_hex = ' '.join(f'{b:02X}' for b in frame)

    p0_active = any(data0[i] != 0 for i in range(3))
    p1_active = any(data1[i] != 0 for i in range(3))

    if p0_active and not p1_active:
        disabled += 1
        print(f'  [DISABLE] {name1:30s} 0x{cmd_word:06X} | {frame_hex}')
    else:
        sent += 1
        diffs = sum(1 for i in range(48) if data0[i] != data1[i])
        print(f'  [SEND #{sent:2d}] {name1:30s} 0x{cmd_word:06X} | {frame_hex}')
        ds = ' '.join(f'{b:02X}' for b in data1)
        for row in range(3):
            start = row * 16
            end = start * 3 + 47
            pad = '               ' + ' ' * 30 + ' ' + ' ' * 10
            print(f'{pad} |   data[{row}]: {ds[start*3:end]}')
        print(f'{pad} |   changed: {diffs}/48 bytes vs P0')

print(f'\n  SEND={sent}  DISABLE={disabled}  SKIPPED={skipped}')
print(f'  Total I2C frames: {sent + disabled}  (全量 {len(all_cmds)} 条)')
