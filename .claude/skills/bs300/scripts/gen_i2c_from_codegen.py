"""
Generate complete I2C Param commands — FAITHFUL copy of codegen Step 5 test logic.
See param.py:_step5_crossval_prog() for the reference implementation.
"""
import json, os, sys
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PARENT_DIR = os.path.dirname(_SCRIPT_DIR)
sys.path.insert(0, _PARENT_DIR)

from bs300_codegen import *
from calib import _find_data_offset
from proto import bs300_cmd_pktnum, bs300_get_word, bs300_set_word
from math_utils import _freq_to_index, _time_to_index, _ratio_to_index

# From param.py:106-113
_BEEP_FREQ_MAP = {
    250: 1, 500: 2, 750: 3, 1000: 4, 1250: 5, 1500: 6, 1750: 7, 2000: 8,
    2250: 9, 2500: 10, 2750: 11, 3000: 12, 3250: 13,
}
def _beep_freq_to_data(hz):
    return _BEEP_FREQ_MAP.get(hz, 0)


def load_chip_data(prog_index):
    with open(os.path.join(_PARENT_DIR, 'data', f'param_commands_{prog_index}.json'), 'r', encoding='utf-8') as f:
        entries = json.load(f)
    chip = {}
    for e in entries:
        cmd = int(e['cmd_word'], 16)
        raw = bytes(int(h, 16) for h in e['bytes'])
        data = raw[5:53]
        assert len(data) == 48
        chip[cmd] = data
    return chip


def build_frame(cmd_word, data_48b):
    payload = bytearray([0x10, cmd_word & 0xFF, (cmd_word >> 8) & 0xFF, (cmd_word >> 16) & 0xFF])
    payload.extend(data_48b)
    chk = (0xFF - sum(payload) % 256) & 0xFF
    frame = bytearray([0x10, chk, cmd_word & 0xFF, (cmd_word >> 8) & 0xFF, (cmd_word >> 16) & 0xFF])
    frame.extend(data_48b)
    return bytes(frame), chk


def main():
    for prog_index in [0, 1]:
        # ================================================================
        # 1. Load calibration (EXACT copy from param.py:1012-1024)
        # ================================================================
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
        calib = parse_calibration(raw)

        # ================================================================
        # 2. Load param_values (EXACT copy from param.py:1030-1032)
        # ================================================================
        with open(os.path.join(_PARENT_DIR, 'data', f'param_values_{prog_index}.json'), 'r', encoding='utf-8') as f:
            params = json.load(f)
        modules = {m['name']: m for m in params['value_in_MT']['modules']}

        # ================================================================
        # 3. Load chip readback (EXACT copy from param.py:1035-1045)
        # ================================================================
        chip_data = load_chip_data(prog_index)

        # ================================================================
        # 4. Detect input type (EXACT copy from param.py:1047-1052)
        # ================================================================
        input_module = next((m for m in modules.keys() if m.startswith('Input(')), None)
        input_type = 'telecoil'
        if input_module:
            input_type = input_module.replace('Input(', '').replace(')', '').lower().replace(' ', '_')

        # ================================================================
        # 5. Encode ALL commands (EXACT copy from param.py:1099-1296)
        # ================================================================
        results = {}
        def add(name, cmd, data):
            results[cmd] = (name, bytes(data))

        wdrc = modules['WDRC']
        num_ch = wdrc['num_channels']
        nmbc = num_ch  # nsbc=0

        # WDRC General
        add('WDRC General Setup', 0x8000B2,
            encode_wdrc_general_param(num_ch, 0, '2KP', wdrc['output_limiting']))

        # WDRC Freq Spacing (2 bins per channel)
        add('WDRC Freq Spacing', 0x8010B2,
            encode_wdrc_freq_spacing_param([2] * nmbc))

        # WDRC KP Threshold
        kp1_th = [ch['kp1_th_db'] for ch in wdrc['channels']]
        kp2_th = [ch['kp2_th_db'] for ch in wdrc['channels']]
        wdrc_freq_idx = [_freq_to_index(ch['freq_hz']) for ch in wdrc['channels']]
        kp_cal_offset = [0] * 16; kp_cal_offset[3] = 1; kp_cal_offset[6] = 1
        add('WDRC KP Threshold', 0x8020B2,
            encode_wdrc_kp_threshold_param(kp1_th, calib, '2KP', input_type,
                                            kp2_th, wdrc_freq_idx, kp_cal_offset))

        # WDRC Attack / Release / Ratio
        epd_at_idx = [_time_to_index(ch['epd_at_ms']) for ch in wdrc['channels']]
        kp1_at_idx = [_time_to_index(ch['kp1_at_ms']) for ch in wdrc['channels']]
        kp2_at_idx = [_time_to_index(ch['kp2_at_ms']) for ch in wdrc['channels']]
        add('WDRC Attack Time', 0x8030B2,
            encode_wdrc_attack_time_param(epd_at_idx, kp1_at_idx, '2KP', kp2_at_idx))

        epd_rt_idx = [_time_to_index(ch['epd_rt_ms']) for ch in wdrc['channels']]
        kp1_rt_idx = [_time_to_index(ch['kp1_rt_ms']) for ch in wdrc['channels']]
        kp2_rt_idx = [_time_to_index(ch['kp2_rt_ms']) for ch in wdrc['channels']]
        add('WDRC Release Time', 0x8040B2,
            encode_wdrc_release_time_param(epd_rt_idx, kp1_rt_idx, '2KP', kp2_rt_idx))

        epd_r_idx = [_ratio_to_index(ch['epd_ratio']) for ch in wdrc['channels']]
        kp1_r_idx = [_ratio_to_index(ch['kp1_ratio']) for ch in wdrc['channels']]
        kp2_r_idx = [_ratio_to_index(ch['kp2_ratio']) for ch in wdrc['channels']]
        add('WDRC Ratio', 0x8050B2,
            encode_wdrc_ratio_param(epd_r_idx, kp1_r_idx, '2KP', kp2_r_idx))

        # WDRC Bin Gain
        add('WDRC Bin Gain', 0x8060B2,
            encode_wdrc_bin_gain_param(wdrc['bin_gains'], calib, input_type))

        # WDRC Limiter
        lmt_th = [ch['lmt_th_db'] for ch in wdrc['channels']]
        lmt_cal_offset = [0] * 16; lmt_cal_offset[3] = 1; lmt_cal_offset[9] = 1
        add('WDRC Lmt Threshold', 0x8070B2,
            encode_wdrc_lmt_threshold_param(lmt_th, calib, wdrc_freq_idx, lmt_cal_offset))

        lmt_at_idx = [_time_to_index(ch['lmt_at_ms']) for ch in wdrc['channels']]
        add('WDRC Lmt Attack', 0x8080B2, encode_wdrc_lmt_atk_time_param(lmt_at_idx))

        lmt_rt_idx = [_time_to_index(ch['lmt_rt_ms']) for ch in wdrc['channels']]
        add('WDRC Lmt Release', 0x8090B2, encode_wdrc_lmt_rel_time_param(lmt_rt_idx))

        lmt_r_idx = [_ratio_to_index(ch['lmt_ratio']) for ch in wdrc['channels']]
        add('WDRC Lmt Ratio', 0x80A0B2, encode_wdrc_lmt_ratio_param(lmt_r_idx))

        # Volume/Beep (param.py:1177-1191)
        vol = modules['Volume/Beep']
        beep_freq_data = _beep_freq_to_data(vol['beep_frequency'])
        batt_freq_data = _beep_freq_to_data(vol['batt_flat_beep_freq'])
        add('Volume/Beep/Input', 0x800081,
            encode_volume_beep_param(
                beep_level_db=vol['beep_level'],
                beep_freq_idx=beep_freq_data,
                min_vol_db=vol['min_volume'],
                max_vol_db=vol['max_volume'],
                input_selection={'front_mic': 0, 'rear_mic': 0, 'telecoil': 1, 'dai': 1}.get(input_type, 0),
                batt_beep_level_db=vol['batt_flat_beep_level'],
                batt_beep_freq_idx=batt_freq_data,
                calib=calib,
            ))

        # Telecoil/DAI (param.py:1193-1198)
        if input_type in ('telecoil', 'dai'):
            add('TC/DAI Gain Diff', 0x804272,
                encode_tc_dai_gain_diff_param(calib.telecoil_gain_diff, calib))
        else:
            add('TC/DAI Gain Diff', 0x804272, bytes(48))

        # DFBC (param.py:1200-1207)
        dfbc = modules['DFBC']
        dfbc_mode_map = {
            'Slow FBC': 0x01, 'Slow Weak': 0x03, 'Slow Strong': 0x07,
            'Fast FBC': 0x09, 'Fast Weak': 0x0B, 'Fast Strong DFBC': 0x0F,
        }
        add('DFBC', 0x800052,
            encode_dfbc_param(dfbc_mode_map.get(dfbc['mode'], 0x0F), calib))

        # ISS (param.py:1209-1212)
        iss_mod = modules['ISS']
        add('ISS', 0x8001B2,
            encode_iss_param(1, iss_mod['threshold'], calib, input_type))

        # WNR (param.py:1214-1233)
        wnr_mod = modules['WNR']
        ssp_map = {'Off': 0, 'Minimal': 1, 'Low': 2, 'Medium': 3, 'High': 4}
        ssp_level = ssp_map.get(wnr_mod['suppression_preset'], 1)
        wnr_band_ssp = 0  # chip uses SSP level 0 for band data offsets

        add('WNR Setup', 0x8001C2,
            encode_wnr_1_param(True, False, calib, ssp_level + 1))
        add('WNR Band 0-15', 0x8011C2,
            encode_wnr_band_data_param(calib, wnr_band_ssp, input_type, band_start=0))
        add('WNR Band 16-31', 0x8411C2,
            encode_wnr_band_data_param(calib, wnr_band_ssp, input_type, band_start=16))
        add('WNR Single Mic', 0x8021C2,
            encode_wnr_single_mic_detect_param(calib, wnr_band_ssp, input_type))

        # ENR (param.py:1235-1289)
        enr = modules['ENR']
        enr_num = enr['num_channels']

        add('ENR General Setup', 0x8000C2,
            encode_enr_general_param(1, enr_num, 2, enr_num - 2))

        enr_freq_indices = [_freq_to_index(ch['freq_hz']) for ch in enr['channels']]
        enr_band_counts = []
        for i, fidx in enumerate(enr_freq_indices):
            if i < len(enr_freq_indices) - 1:
                enr_band_counts.append(enr_freq_indices[i + 1] - fidx)
            else:
                enr_band_counts.append(32 - fidx)
        add('ENR Freq Spacing', 0x8010C2,
            encode_enr_freq_spacing_param(enr_band_counts))

        add('ENR SNR Threshold', 0x8020C2,
            encode_enr_snr_threshold_param([ch['snr_th_db'] for ch in enr['channels']]))

        add('ENR Max Attenuation', 0x8030C2,
            encode_enr_max_att_param(
                [ch['max_att_db'] for ch in enr['channels']],
                [ch['snr_th_db'] for ch in enr['channels']]))

        add('ENR Noise Threshold', 0x8040C2,
            encode_enr_noise_th_param(
                [ch['noise_th_db'] for ch in enr['channels']], calib, input_type,
                enr_freq_indices, enr_band_counts))

        add('ENR Upper Noise Thr', 0x8050C2,
            encode_enr_upper_noise_th_param(
                [ch['upper_noise_th_db'] for ch in enr['channels']], calib, input_type,
                enr_freq_indices, enr_band_counts))

        add('ENR Smoothing', 0x8060C2,
            encode_enr_smoothing_param({
                'nhsf': enr['nhsf'], 'nfsf': enr['nfsf'],
                'nnsf': enr['nnsf'], 'snasf': 4,
            }))

        ma_list = [ch['max_att_db'] for ch in enr['channels']]
        add('ENR ETR', 0x8070C2,
            encode_enr_etr_param([ch['exp_trans_ratio'] for ch in enr['channels']], ma_list))
        add('ENR NRR', 0x8080C2,
            encode_enr_nrr_param([ch['noise_red_ratio'] for ch in enr['channels']], ma_list))

        add('ENR SASF', 0x8090C2, encode_enr_sasf_param([8] * enr_num))

        # AGCO (param.py:1291-1296)
        agco_mod = modules['AGCO']
        agco_atk = agco_mod.get('attack_time_01ms', agco_mod.get('attack_time_ms'))
        agco_rel = agco_mod.get('release_time_01ms', agco_mod.get('release_time_ms'))
        add('AGCO', 0x800382, encode_agco_param(1, agco_mod['threshold_db'], agco_atk, agco_rel))

        # MM Plus / DDM2 / Noise Gen2 — always send (disabled=selection=0 if not active)
        # MM Plus (0x800062): selection=0 if not active
        mm_data = bytearray(48)
        bs300_set_word(mm_data, 0, 0)
        add('MM Plus', 0x800062, bytes(mm_data))

        # DDM2 (0x800022): selection=0 if not active
        ddm2_data = bytearray(48)
        bs300_set_word(ddm2_data, 0, 0)
        add('DDM2', 0x800022, bytes(ddm2_data))

        # Noise Gen2 (0x800172): selection=0 (disabled by default)
        ng2_data = bytearray(48)
        bs300_set_word(ng2_data, 0, 0)
        add('Noise Gen2', 0x800172, bytes(ng2_data))

        # ================================================================
        # 6. Compare with chip readback and output frames
        # ================================================================
        print(f"{'='*120}")
        print(f"Program {prog_index} 全量 Param I2C 命令")
        print(f"input={input_type}  ch={num_ch}  2KP  limiter={'on' if wdrc['output_limiting'] else 'off'}")
        print(f"Frame: [Len] [Checksum] [Cmd0 Cmd1 Cmd2] [Data 48B]")
        print(f"{'='*120}")

        matched = 0; tolerated = 0; failed = 0; no_gt = 0

        for cmd_word in sorted(results.keys()):
            name, our_data = results[cmd_word]
            frame, chk = build_frame(cmd_word, our_data)
            frame_hex = ' '.join(f'{b:02X}' for b in frame)

            if cmd_word in chip_data:
                chip = chip_data[cmd_word]
                # Word-level comparison (same as codegen _cmp_words)
                word_errors = []
                for wi in range(16):
                    enc_w = bs300_get_word(our_data, wi)
                    chip_w = bs300_get_word(chip, wi)
                    if enc_w != chip_w:
                        bi0 = wi * 3
                        b0 = abs((our_data[bi0] - chip[bi0]))
                        b1 = abs((our_data[bi0+1] - chip[bi0+1]))
                        b2 = abs((our_data[bi0+2] - chip[bi0+2]))
                        word_errors.append((wi, enc_w, chip_w, max(b0, b1, b2)))

                if not word_errors:
                    matched += 1; tag = 'MATCH'
                else:
                    max_bd = max(e[3] for e in word_errors)
                    if max_bd <= 1:
                        tolerated += 1; tag = f'TOL ±1 ({len(word_errors)} words)'
                    else:
                        failed += 1; tag = f'FAIL ({len(word_errors)} words, max_byte_diff={max_bd})'
            else:
                no_gt += 1; tag = 'NO_GT'

            print(f'  [{tag:25s}] {name:30s} 0x{cmd_word:06X} | {frame_hex}')
            ds = ' '.join(f'{b:02X}' for b in our_data)
            for row in range(3):
                start = row * 16
                end = start * 3 + 47
                padding = ' ' * 25 + '  ' + ' ' * 30 + ' ' + ' ' * 10
                print(f'{padding} |   data[{row}]: {ds[start*3:end]}')

        if failed > 0:
            print(f'\n  --- FAIL details ---')
            for cmd_word in sorted(results.keys()):
                name, our_data = results[cmd_word]
                if cmd_word not in chip_data: continue
                chip = chip_data[cmd_word]
                word_errors = []
                for wi in range(16):
                    enc_w = bs300_get_word(our_data, wi)
                    chip_w = bs300_get_word(chip, wi)
                    if enc_w != chip_w:
                        bi0 = wi * 3
                        b0 = abs(our_data[bi0] - chip[bi0])
                        b1 = abs(our_data[bi0+1] - chip[bi0+1])
                        b2 = abs(our_data[bi0+2] - chip[bi0+2])
                        word_errors.append((wi, enc_w, chip_w, max(b0, b1, b2)))
                if word_errors:
                    max_bd = max(e[3] for e in word_errors)
                    if max_bd > 1:
                        print(f'  {name} (0x{cmd_word:06X}):')
                        for wi, enc, chip_w, bd in word_errors[:4]:
                            print(f'    word[{wi}]: encoded=0x{enc:06X} chip=0x{chip_w:06X} max_byte_diff={bd}')
                        if len(word_errors) > 4:
                            print(f'    ... and {len(word_errors)-4} more words')

        print(f'\n  MATCH={matched}  TOL={tolerated}  FAIL={failed}  NO_GT={no_gt}')
        print()


if __name__ == '__main__':
    main()
