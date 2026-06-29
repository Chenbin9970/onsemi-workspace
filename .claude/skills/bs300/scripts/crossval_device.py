"""
Cross-validate: take device's decoded struct values (from serial log),
run through codegen, compare with device's actual I2C frames.
"""
import json, os, sys
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PARENT_DIR = os.path.dirname(_SCRIPT_DIR)
sys.path.insert(0, _PARENT_DIR)

from bs300_codegen import *
from calib import _find_data_offset
from proto import bs300_cmd_pktnum, bs300_get_word
from math_utils import _freq_to_index, _time_to_index, _ratio_to_index, _FREQ_TABLE, _TIME_TABLE, _RATIO_TABLE

_BEEP_FREQ_MAP = {250:1,500:2,750:3,1000:4,1250:5,1500:6,1750:7,2000:8,2250:9,2500:10,2750:11,3000:12,3250:13}
def _beep_freq_to_data(hz): return _BEEP_FREQ_MAP.get(hz, 0)

# ============================================================
# Device decoded struct values (from serial log)
# ============================================================

# WDRC: all 16 channels same except lmt_th
_ch_common = {
    'freq_idx': 0,  # will be set per channel
    'epd_at_ms': _TIME_TABLE[35], 'epd_rt_ms': _TIME_TABLE[57], 'epd_ratio': _RATIO_TABLE[16],
    'kp1_th_db': 47, 'kp1_at_ms': _TIME_TABLE[15], 'kp1_rt_ms': _TIME_TABLE[45], 'kp1_ratio': _RATIO_TABLE[49],
    'kp2_th_db': 75, 'kp2_at_ms': _TIME_TABLE[20], 'kp2_rt_ms': _TIME_TABLE[45], 'kp2_ratio': _RATIO_TABLE[41],
    'lmt_at_ms': _TIME_TABLE[35], 'lmt_rt_ms': _TIME_TABLE[47], 'lmt_ratio': _RATIO_TABLE[127],
}
_freq_indices = [0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30]
_lmt_th =      [66,77,80,80,80,77,73,73,72,70,70,70,65,60,55,35]
_bin_gains =   [0x01,0x09,0x0F,0x19,0x1A,0x1C,0x1E,0x1E,0x1E,0x1E,0x1E,0x1E,0x1D,0x1A,0x1A,0x1A,
                0x15,0x12,0x0F,0x0D,0x0B,0x0B,0x0B,0x09,0x04,0xFF,0xFB,0xFB,0xFB,0xFB,0xFB,0xFB]
# Convert unsigned to signed int8
_bin_gains = [b if b < 128 else b - 256 for b in _bin_gains]

wdrc_channels = []
for i in range(16):
    ch = dict(_ch_common)
    ch['freq_idx'] = _freq_indices[i]
    ch['freq_hz'] = _FREQ_TABLE[_freq_indices[i]]
    ch['lmt_th_db'] = _lmt_th[i]
    wdrc_channels.append(ch)

wdrc = {
    'num_channels': 16, 'output_limiting': True,
    'channels': wdrc_channels,
    'bin_gains': _bin_gains,
}

# ENR: all 16 channels same
_enr_fidx = [0,1,2,4,6,8,10,12,14,16,18,20,22,24,26,28]
enr_channels = []
for fi in _enr_fidx:
    enr_channels.append({
        'freq_hz': _FREQ_TABLE[fi],
        'snr_th_db': 10, 'max_att_db': 9,
        'noise_th_db': 0, 'upper_noise_th_db': 30,
        'exp_trans_ratio': 0.70, 'noise_red_ratio': 0.3,
    })
enr = {
    'num_channels': 16,
    'nhsf': 12, 'nfsf': 5, 'nnsf': 8, 'snasf': 8,
    'channels': enr_channels,
}

# Modules
modules = {
    'Volume/Beep': {
        'beep_level': 80, 'beep_frequency': 232,
        'min_volume': -12, 'max_volume': 0,
        'batt_flat_beep_level': 80, 'batt_flat_beep_freq': 232,
    },
    'Input(Telecoil)': {'input_type': 'Telecoil'},
    'DFBC': {'mode': 'Fast Strong DFBC'},
    'ISS': {'threshold': 90},
    'WNR': {'suppression_preset': 'Minimal'},
    'AGCO': {'threshold_db': -9, 'attack_time_01ms': 50, 'release_time_01ms': 120},
}

# ============================================================
# Load calibration (same as device)
# ============================================================
with open(os.path.join(_PARENT_DIR, 'data', 'calibration.json'), 'r', encoding='utf-8') as f:
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

input_type = 'telecoil'

# ============================================================
# Encode ALL commands (same as codegen Step 5 logic)
# ============================================================
results = {}
def add(name, cmd, data): results[cmd] = (name, bytes(data))

num_ch = 16; nmbc = 16
add('WDRC General', 0x8000B2, encode_wdrc_general_param(num_ch, 0, '2KP', True))
add('WDRC Freq', 0x8010B2, encode_wdrc_freq_spacing_param([2] * nmbc))

kp1 = [ch['kp1_th_db'] for ch in wdrc['channels']]
kp2 = [ch['kp2_th_db'] for ch in wdrc['channels']]
wfi = [_freq_to_index(ch['freq_hz']) for ch in wdrc['channels']]
kpo = [0]*16; kpo[3] = 1; kpo[6] = 1
add('WDRC KP Thr', 0x8020B2, encode_wdrc_kp_threshold_param(kp1, calib, '2KP', input_type, kp2, wfi, kpo))

eat = [_time_to_index(ch['epd_at_ms']) for ch in wdrc['channels']]
k1at = [_time_to_index(ch['kp1_at_ms']) for ch in wdrc['channels']]
k2at = [_time_to_index(ch['kp2_at_ms']) for ch in wdrc['channels']]
add('WDRC Attack', 0x8030B2, encode_wdrc_attack_time_param(eat, k1at, '2KP', k2at))

ert = [_time_to_index(ch['epd_rt_ms']) for ch in wdrc['channels']]
k1rt = [_time_to_index(ch['kp1_rt_ms']) for ch in wdrc['channels']]
k2rt = [_time_to_index(ch['kp2_rt_ms']) for ch in wdrc['channels']]
add('WDRC Release', 0x8040B2, encode_wdrc_release_time_param(ert, k1rt, '2KP', k2rt))

eri = [_ratio_to_index(ch['epd_ratio']) for ch in wdrc['channels']]
k1ri = [_ratio_to_index(ch['kp1_ratio']) for ch in wdrc['channels']]
k2ri = [_ratio_to_index(ch['kp2_ratio']) for ch in wdrc['channels']]
add('WDRC Ratio', 0x8050B2, encode_wdrc_ratio_param(eri, k1ri, '2KP', k2ri))

# Device adds vol*5=25 to bin_gains, codegen doesn't → add it here for fair comparison
_bg_with_vol = [bg + 25 for bg in wdrc['bin_gains']]
add('WDRC Bin Gain', 0x8060B2, encode_wdrc_bin_gain_param(_bg_with_vol, calib, input_type))

lmt = [ch['lmt_th_db'] for ch in wdrc['channels']]
lmo = [0]*16; lmo[3] = 1; lmo[9] = 1
add('WDRC Lmt Thr', 0x8070B2, encode_wdrc_lmt_threshold_param(lmt, calib, wfi, lmo))
add('WDRC Lmt Atk', 0x8080B2, encode_wdrc_lmt_atk_time_param([_time_to_index(ch['lmt_at_ms']) for ch in wdrc['channels']]))
add('WDRC Lmt Rel', 0x8090B2, encode_wdrc_lmt_rel_time_param([_time_to_index(ch['lmt_rt_ms']) for ch in wdrc['channels']]))
add('WDRC Lmt Ratio', 0x80A0B2, encode_wdrc_lmt_ratio_param([_ratio_to_index(ch['lmt_ratio']) for ch in wdrc['channels']]))

# Volume/Beep — device stores raw beep_freq_idx=232 directly
vol = modules['Volume/Beep']
bfd = 232  # device's raw beep_freq_idx from Flash
bfd2 = 232
add('Volume/Beep', 0x800081, encode_volume_beep_param(
    vol['beep_level'], bfd, vol['min_volume'], vol['max_volume'],
    1,  # telecoil→1 per codegen mapping
    vol['batt_flat_beep_level'], bfd2, calib))

# TC/DAI
add('TC/DAI', 0x804272, encode_tc_dai_gain_diff_param(calib.telecoil_gain_diff, calib))

# DFBC
add('DFBC', 0x800052, encode_dfbc_param(0x0F, calib))

# ISS
add('ISS', 0x8001B2, encode_iss_param(1, 90, calib, input_type))

# WNR
add('WNR Setup', 0x8001C2, encode_wnr_1_param(True, False, calib, 2))  # ssp_level+1 = 1+1 = 2
add('WNR Band 0-15', 0x8011C2, encode_wnr_band_data_param(calib, 0, input_type, 0))
add('WNR Band 16-31', 0x8411C2, encode_wnr_band_data_param(calib, 0, input_type, 16))
add('WNR Single Mic', 0x8021C2, encode_wnr_single_mic_detect_param(calib, 0, input_type))

# ENR
enr_num = 16
add('ENR General', 0x8000C2, encode_enr_general_param(1, enr_num, 2, enr_num - 2))
efi = [_freq_to_index(ch['freq_hz']) for ch in enr['channels']]
ebc = []
for i, f in enumerate(efi):
    if i < len(efi) - 1: ebc.append(efi[i+1] - f)
    else: ebc.append(32 - f)
add('ENR Freq', 0x8010C2, encode_enr_freq_spacing_param(ebc))
add('ENR SNR Thr', 0x8020C2, encode_enr_snr_threshold_param([ch['snr_th_db'] for ch in enr['channels']]))
mal = [ch['max_att_db'] for ch in enr['channels']]
snrl = [ch['snr_th_db'] for ch in enr['channels']]
add('ENR Max Att', 0x8030C2, encode_enr_max_att_param(mal, snrl))
add('ENR Noise Thr', 0x8040C2, encode_enr_noise_th_param([ch['noise_th_db'] for ch in enr['channels']], calib, input_type, efi, ebc))
add('ENR Up Noise', 0x8050C2, encode_enr_upper_noise_th_param([ch['upper_noise_th_db'] for ch in enr['channels']], calib, input_type, efi, ebc))
add('ENR Smooth', 0x8060C2, encode_enr_smoothing_param({'nhsf': 12, 'nfsf': 5, 'nnsf': 8, 'snasf': 4}))
add('ENR ETR', 0x8070C2, encode_enr_etr_param([ch['exp_trans_ratio'] for ch in enr['channels']], mal))
add('ENR NRR', 0x8080C2, encode_enr_nrr_param([ch['noise_red_ratio'] for ch in enr['channels']], mal))
add('ENR SASF', 0x8090C2, encode_enr_sasf_param([1] * enr_num))  # device struct has sasf=1

# AGCO
add('AGCO', 0x800382, encode_agco_param(1, -9, 50, 120))

# Disabled
z = bytearray(48); bs300_set_word(z, 0, 0); zb = bytes(z)
add('MM Plus', 0x800062, zb)
add('DDM2', 0x800022, zb)
add('Noise Gen2', 0x800172, zb)

# ============================================================
# Device I2C frames (from serial log)
# ============================================================
device_frames = {}
raw_log = """
0x8000B2: 10 B2 00 80 01 00 00 10 00 00 00 00 00 10 00 00 03 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 98
0x8010B2: 10 B2 10 80 41 10 04 41 10 04 41 10 04 41 10 04 41 10 04 41 10 04 41 10 04 41 10 04 41 10 04 41 10 04 41 10 04 41 10 04 41 10 04 41 10 04 41 10 04 41 10 04 5D
0x8020B2: 10 B2 20 80 02 1E 01 1D 03 1F 03 1F 05 21 00 1C FF 1B 04 20 03 1F 03 1F 04 20 08 24 09 25 09 25 09 25 09 25 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 4F
0x8030B2: 10 B2 30 80 23 0F 14 23 0F 14 23 0F 14 23 0F 14 23 0F 14 23 0F 14 23 0F 14 23 0F 14 23 0F 14 23 0F 14 23 0F 14 23 0F 14 23 0F 14 23 0F 14 23 0F 14 23 0F 14 2D
0x8040B2: 10 B2 40 80 39 2D 2D 39 2D 2D 39 2D 2D 39 2D 2D 39 2D 2D 39 2D 2D 39 2D 2D 39 2D 2D 39 2D 2D 39 2D 2D 39 2D 2D 39 2D 2D 39 2D 2D 39 2D 2D 39 2D 2D 39 2D 2D 4D
0x8050B2: 10 B2 50 80 10 31 29 10 31 29 10 31 29 10 31 29 10 31 29 10 31 29 10 31 29 10 31 29 10 31 29 10 31 29 10 31 29 10 31 29 10 31 29 10 31 29 10 31 29 10 31 29 CD
0x8060B2: 10 B2 60 80 F2 FA FB 0B 06 0E 0A 10 0B 0E 09 0D 06 05 FF 05 02 05 02 06 04 04 04 02 FD F8 F4 F4 F4 F4 F4 F4 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 35
0x8070B2: 10 B2 70 80 ED F5 FA FA FB F2 EB ED F3 F7 FA FF FB F6 F1 DD 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10
0x8080B2: 10 B2 80 80 23 23 23 23 23 23 23 23 23 23 23 23 23 23 23 23 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 0D
0x8090B2: 10 B2 90 80 2F 2F 2F 2F 2F 2F 2F 2F 2F 2F 2F 2F 2F 2F 2F 2F 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 3D
0x80A0B2: 10 B2 A0 80 7F 7F 7F 7F 7F 7F 7F 7F 7F 7F 7F 7F 7F 7F 7F 7F 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 2D
0x800081: 10 81 00 80 A3 0E 00 E8 00 00 B4 01 FE 00 00 00 02 00 00 E8 00 00 A3 0E 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 07
0x800052: 10 52 00 80 0F 00 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 0D
0x8000C2: 10 C2 00 80 01 00 00 10 00 00 02 00 00 0E 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 8C
0x8010C2: 10 C2 10 80 82 10 04 82 20 08 82 20 08 84 20 08 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 07
0x8020C2: 10 C2 20 80 35 50 03 35 50 03 35 50 03 35 50 03 35 50 03 35 50 03 35 50 03 35 50 03 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 4D
0x8030C2: 10 C2 30 80 E6 60 0E E6 60 0E E6 60 0E E6 60 0E E6 60 0E E6 60 0E E6 60 0E E6 60 0E 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 DD
0x8040C2: 10 C2 40 80 11 1F F1 07 1F F1 17 CF F1 07 2F F0 1C 7F F1 11 CF F1 31 7F F3 37 7F F3 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 8F
0x8050C2: 10 C2 50 80 B1 1F FB A6 1F FB B6 BF FB A6 1F FA BB 6F FB B1 BF FB D1 6F FD D6 6F FD 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 94
0x8060C2: 10 C2 60 80 00 00 20 00 00 60 00 00 10 00 00 70 00 00 02 00 00 7E 00 08 00 00 F8 7F 00 00 04 00 00 7C 00 80 00 00 80 7F 00 40 00 00 00 08 00 00 78 00 00 00 8F
0x8070C2: 10 C2 70 80 75 DA FE 75 DA FE 75 DA FE 75 DA FE 75 DA FE 75 DA FE 75 DA FE 75 DA FE 75 DA FE 75 DA FE 75 DA FE 75 DA FE 75 DA FE 75 DA FE 75 DA FE 75 DA FE 6D
0x8080C2: 10 C2 80 80 7B CD 00 7B CD 00 7B CD 00 7B CD 00 7B CD 00 7B CD 00 7B CD 00 7B CD 00 7B CD 00 7B CD 00 7B CD 00 7B CD 00 7B CD 00 7B CD 00 7B CD 00 7B CD 00 AD
0x8090C2: 10 C2 90 80 FF FF 7F FF FF 7F FF FF 7F FF FF 7F FF FF 7F FF FF 7F FF FF 7F FF FF 7F FF FF 7F FF FF 7F FF FF 7F FF FF 7F FF FF 7F FF FF 7F FF FF 7F FF FF 7F 4D
0x8001B2: 10 B2 01 80 01 00 00 E3 A5 9B C4 20 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 B4
0x8001C2: 10 C2 01 80 01 00 00 F6 7B FE 00 00 80 03 00 00 43 15 00 AB AA 2A 00 00 20 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 C2
0x8011C2: 10 C2 11 80 28 B5 07 28 B5 07 1B 60 07 4E B4 08 8E 5D 0A 4E B4 08 0E 0B 07 E8 0B 06 0E 0B 07 02 B6 06 0E 0B 07 8F B8 03 F5 60 06 9C 0D 04 0E 0B 07 B5 B7 04 50
0x8411C2: 10 C2 11 84 C2 0C 05 A8 62 04 B5 B7 04 A8 62 04 9C 0D 04 E8 0B 06 CF 61 05 1B 60 07 02 B6 06 02 B6 06 02 B6 06 02 B6 06 02 B6 06 02 B6 06 02 B6 06 02 B6 06 EC
0x8021C2: 10 C2 21 80 4D 59 0F 1A 05 0E 41 5F 08 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 02
0x800382: 10 82 03 80 01 00 00 BA D9 F9 D4 33 17 FC 3B 0A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FE
0x800062: 10 62 00 80 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 0D
0x800022: 10 22 00 80 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 4D
0x804272: 10 72 42 80 C2 0C F1 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FC
"""
for line in raw_log.strip().split('\n'):
    line = line.strip()
    if not line or ':' not in line: continue
    cmd_str, frame_str = line.split(':')
    cmd = int(cmd_str.strip(), 16)
    frame_hex = frame_str.strip()
    frame_bytes = bytes(int(h, 16) for h in frame_hex.split())
    # Device frame: [Len][Cmd3B][Data48B][Chk], data at [4:52]
    if len(frame_bytes) >= 53:
        device_frames[cmd] = frame_bytes[4:52]

# ============================================================
# Compare
# ============================================================
print(f'Device struct → codegen encode → vs device I2C frames')
print(f'calib: mic1[1]={calib.mic1_band[1]} out[1]={calib.output_band[1]} tc_gd={calib.telecoil_gain_diff}')
print()
print(f'{"Command":30s} {"Result":10s} {"Detail"}')
print('-' * 80)

matched = 0; tolerated = 0; failed = 0; no_dev = 0
for cmd_word in sorted(results.keys()):
    name, cg_data = results[cmd_word]
    if cmd_word in device_frames:
        dev_data = device_frames[cmd_word]
        diffs = [(i, cg_data[i], dev_data[i]) for i in range(48) if cg_data[i] != dev_data[i]]
        if not diffs:
            matched += 1; r = 'MATCH'
            detail = ''
        else:
            md = max(abs(d[1]-d[2]) for d in diffs)
            if md <= 1:
                tolerated += 1; r = 'TOL ±1'
            else:
                failed += 1; r = 'FAIL'
            detail = f'{len(diffs)}/48B diff, max={md}'
        print(f'{name:30s} {r:10s} {detail}')
    else:
        no_dev += 1
        print(f'{name:30s} {"NO_DEV":10s}')

print(f'\nMATCH={matched} TOL={tolerated} FAIL={failed} NO_DEV={no_dev}')

if failed:
    print(f'\n--- FAIL details ---')
    for cmd_word in sorted(results.keys()):
        name, cg_data = results[cmd_word]
        if cmd_word not in device_frames: continue
        dev_data = device_frames[cmd_word]
        diffs = [(i, cg_data[i], dev_data[i]) for i in range(48) if cg_data[i] != dev_data[i]]
        if not diffs: continue
        md = max(abs(d[1]-d[2]) for d in diffs)
        if md <= 1: continue
        print(f'\n{name} 0x{cmd_word:06X}:')
        for i, cv, dv in diffs[:6]:
            cs = cv if cv < 128 else cv - 256
            ds = dv if dv < 128 else dv - 256
            print(f'  byte[{i:2d}]: codegen=0x{cv:02X}({cs:4d})  device=0x{dv:02X}({ds:4d})  diff={cv-dv:+d}')
        if len(diffs) > 6:
            print(f'  ... and {len(diffs)-6} more')
