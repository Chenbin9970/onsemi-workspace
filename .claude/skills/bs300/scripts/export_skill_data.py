"""
Export skill ground truth data as C headers for bs300_convert.c cross-validation.

Reads: calibration.json, program_0.json, program_1.json,
       param_commands_0.json, param_commands_1.json,
       param_values_0.json, param_values_1.json

Outputs to output/:
  bs300_skill_data.h   — Flash raw data + calib raw data + expected Param outputs
"""
import json, os, sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SKILL_DIR = os.path.dirname(SCRIPT_DIR)
sys.path.insert(0, SKILL_DIR)

from proto import bs300_cmd_pktnum
from calib import parse_calibration, _find_data_offset
from flash_read import parse_program_data
from param import _step5_crossval_prog


def extract_program_raw(json_path):
    """Extract concatenated Flash data from program_*.json."""
    with open(json_path, 'r') as f:
        entries = json.load(f)
    data_sections = {}
    for entry in entries:
        raw_bytes = bytes(int(h, 16) for h in entry['bytes'])
        cmd = int(entry['cmd_word'], 16)
        pktnum = (cmd >> 12) & 0xF
        if len(raw_bytes) == 53:  # Read Response: addr(1) + cmd(3) + data(48) + chk(1)
            data_sections[pktnum] = raw_bytes[4:52]
    return b''.join(data_sections[p] for p in sorted(data_sections))


def extract_calib_raw(json_path):
    """Extract 144-byte calibration raw data from calibration.json."""
    with open(json_path, 'r') as f:
        entries = json.load(f)
    data_sections = {}
    for entry in entries:
        cmd = int(entry['cmd_word'], 16)
        pktnum = bs300_cmd_pktnum(cmd)
        if pktnum > 2:
            continue
        raw_bytes = bytes(int(h, 16) for h in entry['bytes'])
        off = _find_data_offset(raw_bytes)
        data_sections[pktnum] = raw_bytes[off: off + 48]
    return data_sections[0] + data_sections[1] + data_sections[2]


def extract_param_commands(json_path):
    """Extract {cmd_word_int: 48-byte data} from param_commands_*.json."""
    with open(json_path, 'r') as f:
        entries = json.load(f)
    result = {}
    for entry in entries:
        cmd = int(entry['cmd_word'], 16)
        raw_bytes = bytes(int(h, 16) for h in entry['bytes'])
        # Advanced Write: [Addr(1)] [Len(1)] [Cmd(3)] [Data(48)] [Chk(1)] = 54 bytes
        if len(raw_bytes) == 54:
            result[cmd] = raw_bytes[5:53]
    return result


def bytes_to_c_array(data, name, indent=0):
    """Format bytes as C array initializer."""
    prefix = ' ' * indent
    lines = []
    for i in range(0, len(data), 12):
        chunk = data[i:i + 12]
        lines.append(prefix + ', '.join(f'0x{b:02X}' for b in chunk) + ',')
    return '\n'.join(lines)


def write_header():
    output_dir = os.path.join(SKILL_DIR, 'output')

    print("Loading skill data...")
    prog0_raw = extract_program_raw(os.path.join(SKILL_DIR, 'data', 'program_0.json'))
    prog1_raw = extract_program_raw(os.path.join(SKILL_DIR, 'data', 'program_1.json'))
    calib_raw = extract_calib_raw(os.path.join(SKILL_DIR, 'data', 'calibration.json'))
    chip_p0 = extract_param_commands(os.path.join(SKILL_DIR, 'data', 'param_commands_0.json'))
    chip_p1 = extract_param_commands(os.path.join(SKILL_DIR, 'data', 'param_commands_1.json'))

    # Run Python reference to get expected Param outputs
    print("Running Python reference cross-validation...")
    calib = parse_calibration(calib_raw)

    # Get expected outputs from Python codegen
    from param import (
        encode_wdrc_general_param, encode_wdrc_freq_spacing_param,
        encode_wdrc_kp_threshold_param, encode_wdrc_attack_time_param,
        encode_wdrc_release_time_param, encode_wdrc_ratio_param,
        encode_wdrc_bin_gain_param, encode_wdrc_lmt_threshold_param,
        encode_wdrc_lmt_atk_time_param, encode_wdrc_lmt_rel_time_param,
        encode_wdrc_lmt_ratio_param,
        encode_volume_beep_param, encode_dfbc_param, encode_iss_param,
        encode_wnr_1_param, encode_wnr_band_data_param, encode_wnr_single_mic_detect_param,
        encode_enr_general_param, encode_enr_freq_spacing_param,
        encode_enr_snr_threshold_param, encode_enr_max_att_param,
        encode_enr_noise_th_param, encode_enr_upper_noise_th_param,
        encode_enr_smoothing_param, encode_enr_etr_param, encode_enr_nrr_param,
        encode_agco_param, encode_mm_plus_param, encode_ddm2_param,
        encode_tc_dai_gain_diff_param, _beep_freq_to_data,
    )
    from math_utils import _time_to_index, _ratio_to_index, _freq_to_index

    all_expected = {}  # prog_index → {cmd_word_int → (name, 48B data)}

    for prog_idx in [0, 1]:
        with open(os.path.join(SKILL_DIR, 'data', f'param_values_{prog_idx}.json'), 'r') as f:
            params = json.load(f)

        modules = {m['name']: m for m in params['value_in_MT']['modules']}
        input_module = next((m for m in modules.keys() if m.startswith('Input(')), None)
        input_type = 'telecoil'
        if input_module:
            input_type = input_module.replace('Input(', '').replace(')', '').lower().replace(' ', '_')

        wdrc = modules['WDRC']
        num_ch = wdrc['num_channels']
        nsbc = 0
        nmbc = num_ch - nsbc
        kp_mode = '2KP'

        kp1_th = [ch['kp1_th_db'] for ch in wdrc['channels']]
        kp2_th = [ch['kp2_th_db'] for ch in wdrc['channels']]
        epd_at_idx = [_time_to_index(ch['epd_at_ms']) for ch in wdrc['channels']]
        kp1_at_idx = [_time_to_index(ch['kp1_at_ms']) for ch in wdrc['channels']]
        kp2_at_idx = [_time_to_index(ch['kp2_at_ms']) for ch in wdrc['channels']]
        epd_rt_idx = [_time_to_index(ch['epd_rt_ms']) for ch in wdrc['channels']]
        kp1_rt_idx = [_time_to_index(ch['kp1_rt_ms']) for ch in wdrc['channels']]
        kp2_rt_idx = [_time_to_index(ch['kp2_rt_ms']) for ch in wdrc['channels']]
        epd_r_idx = [_ratio_to_index(ch['epd_ratio']) for ch in wdrc['channels']]
        kp1_r_idx = [_ratio_to_index(ch['kp1_ratio']) for ch in wdrc['channels']]
        kp2_r_idx = [_ratio_to_index(ch['kp2_ratio']) for ch in wdrc['channels']]
        bin_gains = wdrc['bin_gains']
        lmt_th = [ch['lmt_th_db'] for ch in wdrc['channels']]
        lmt_at_idx = [_time_to_index(ch['lmt_at_ms']) for ch in wdrc['channels']]
        lmt_rt_idx = [_time_to_index(ch['lmt_rt_ms']) for ch in wdrc['channels']]
        lmt_r_idx = [_ratio_to_index(ch['lmt_ratio']) for ch in wdrc['channels']]
        wdrc_freq_idx = [_freq_to_index(ch['freq_hz']) for ch in wdrc['channels']]

        vol = modules['Volume/Beep']
        input_sel = {'front_mic': 0, 'rear_mic': 0, 'telecoil': 1, 'dai': 1}.get(input_type, 0)
        beep_freq_data = _beep_freq_to_data(vol['beep_frequency'])
        batt_freq_data = _beep_freq_to_data(vol['batt_flat_beep_freq'])

        dfbc = modules['DFBC']
        dfbc_map = {'Slow FBC': 0x01, 'Slow Weak': 0x03, 'Slow Strong': 0x07,
                    'Fast FBC': 0x09, 'Fast Weak': 0x0B, 'Fast Strong DFBC': 0x0F}

        iss_mod = modules['ISS']
        wnr_mod = modules['WNR']
        ssp_map = {'Off': 0, 'Minimal': 1, 'Low': 2, 'Medium': 3, 'High': 4}
        ssp = ssp_map.get(wnr_mod['suppression_preset'], 1)

        enr = modules['ENR']
        enr_num = enr['num_channels']
        enr_fidx = [_freq_to_index(ch['freq_hz']) for ch in enr['channels']]
        enr_bc = []
        for i, fidx in enumerate(enr_fidx):
            if i < len(enr_fidx) - 1:
                enr_bc.append(enr_fidx[i + 1] - fidx)
            else:
                enr_bc.append(32 - fidx)

        agco = modules['AGCO']

        expected = {}

        # WDRC
        kp_cal_offset = [0] * 16
        kp_cal_offset[3] = 1; kp_cal_offset[6] = 1
        lmt_cal_offset = [0] * 16
        lmt_cal_offset[3] = 1; lmt_cal_offset[9] = 1

        expected[0x8000B2] = ("WDRC_General",
            encode_wdrc_general_param(num_ch, nsbc, kp_mode, wdrc['output_limiting']))
        mbc_counts = [2] * nmbc
        expected[0x8010B2] = ("WDRC_FreqSpacing",
            encode_wdrc_freq_spacing_param(mbc_counts))
        expected[0x8020B2] = ("WDRC_KPThreshold",
            encode_wdrc_kp_threshold_param(kp1_th, calib, kp_mode, input_type,
                                           kp2_th, wdrc_freq_idx, kp_cal_offset))
        expected[0x8030B2] = ("WDRC_AttackTime",
            encode_wdrc_attack_time_param(epd_at_idx, kp1_at_idx, kp_mode, kp2_at_idx))
        expected[0x8040B2] = ("WDRC_ReleaseTime",
            encode_wdrc_release_time_param(epd_rt_idx, kp1_rt_idx, kp_mode, kp2_rt_idx))
        expected[0x8050B2] = ("WDRC_Ratio",
            encode_wdrc_ratio_param(epd_r_idx, kp1_r_idx, kp_mode, kp2_r_idx))
        expected[0x8060B2] = ("WDRC_BinGain",
            encode_wdrc_bin_gain_param(bin_gains, calib, input_type))
        expected[0x8070B2] = ("WDRC_LmtThreshold",
            encode_wdrc_lmt_threshold_param(lmt_th, calib, wdrc_freq_idx, lmt_cal_offset))
        expected[0x8080B2] = ("WDRC_LmtAttack",
            encode_wdrc_lmt_atk_time_param(lmt_at_idx))
        expected[0x8090B2] = ("WDRC_LmtRelease",
            encode_wdrc_lmt_rel_time_param(lmt_rt_idx))
        expected[0x80A0B2] = ("WDRC_LmtRatio",
            encode_wdrc_lmt_ratio_param(lmt_r_idx))

        # Volume/Beep
        expected[0x800081] = ("VolumeBeep",
            encode_volume_beep_param(vol['beep_level'], beep_freq_data,
                                     vol['min_volume'], vol['max_volume'], input_sel,
                                     vol['batt_flat_beep_level'], batt_freq_data, calib))

        # TC Gain Diff
        if input_type in ('telecoil', 'dai'):
            expected[0x804272] = ("TCGainDiff",
                encode_tc_dai_gain_diff_param(calib.telecoil_gain_diff, calib))
        else:
            expected[0x804272] = ("TCGainDiff", bytes(48))

        # DFBC
        expected[0x800052] = ("DFBC",
            encode_dfbc_param(dfbc_map.get(dfbc['mode'], 0x0F), calib))

        # ISS
        expected[0x8001B2] = ("ISS",
            encode_iss_param(1, iss_mod['threshold'], calib, input_type))

        # WNR
        expected[0x8001C2] = ("WNR_Setup",
            encode_wnr_1_param(True, False, calib, ssp + 1))
        expected[0x8011C2] = ("WNR_Bands0_15",
            encode_wnr_band_data_param(calib, 0, input_type, band_start=0))
        expected[0x8411C2] = ("WNR_Bands16_31",
            encode_wnr_band_data_param(calib, 0, input_type, band_start=16))
        expected[0x8021C2] = ("WNR_SingleMic",
            encode_wnr_single_mic_detect_param(calib, 0, input_type))

        # ENR
        expected[0x8000C2] = ("ENR_General",
            encode_enr_general_param(1, enr_num, 2, enr_num - 2))
        expected[0x8010C2] = ("ENR_FreqSpacing",
            encode_enr_freq_spacing_param(enr_bc))
        expected[0x8020C2] = ("ENR_SNRThreshold",
            encode_enr_snr_threshold_param([ch['snr_th_db'] for ch in enr['channels']]))
        expected[0x8030C2] = ("ENR_MaxAtt",
            encode_enr_max_att_param([ch['max_att_db'] for ch in enr['channels']],
                                     [ch['snr_th_db'] for ch in enr['channels']]))
        expected[0x8040C2] = ("ENR_NoiseThr",
            encode_enr_noise_th_param([ch['noise_th_db'] for ch in enr['channels']],
                                       calib, input_type, enr_fidx, enr_bc))
        expected[0x8050C2] = ("ENR_UpperNoiseThr",
            encode_enr_upper_noise_th_param([ch['upper_noise_th_db'] for ch in enr['channels']],
                                             calib, input_type, enr_fidx, enr_bc))
        expected[0x8060C2] = ("ENR_Smoothing",
            encode_enr_smoothing_param({'nhsf': enr['nhsf'], 'nfsf': enr['nfsf'],
                                        'nnsf': enr['nnsf'], 'snasf': 4}))
        expected[0x8070C2] = ("ENR_ETR",
            encode_enr_etr_param([ch['exp_trans_ratio'] for ch in enr['channels']],
                                 [ch['max_att_db'] for ch in enr['channels']]))
        expected[0x8080C2] = ("ENR_NRR",
            encode_enr_nrr_param([ch['noise_red_ratio'] for ch in enr['channels']],
                                 [ch['max_att_db'] for ch in enr['channels']]))

        # AGCO
        agco_atk = agco.get('attack_time_01ms', agco.get('attack_time_ms'))
        agco_rel = agco.get('release_time_01ms', agco.get('release_time_ms'))
        expected[0x800382] = ("AGCO",
            encode_agco_param(1, agco['threshold_db'], agco_atk, agco_rel))

        # MM Plus (disabled)
        from proto import bs300_set_word
        mm_data = bytearray(48)
        bs300_set_word(mm_data, 0, 0)
        expected[0x800062] = ("MMPlus", bytes(mm_data))

        # DDM2 (disabled)
        ddm2_data = bytearray(48)
        bs300_set_word(ddm2_data, 0, 0)
        expected[0x800022] = ("DDM2", bytes(ddm2_data))

        all_expected[prog_idx] = expected

    # ---- Write C header ----
    header_path = os.path.join(output_dir, 'bs300_skill_data.h')

    with open(header_path, 'w', encoding='utf-8') as f:
        f.write("""/* Auto-generated by export_skill_data.py — BS300 skill ground truth data */
/* DO NOT EDIT. Used by bs300_convert.c for cross-validation.              */
#ifndef BS300_SKILL_DATA_H
#define BS300_SKILL_DATA_H

#include <stdint.h>

""")

        # Calibration raw (144 bytes)
        f.write(f"/* Calibration raw data, {len(calib_raw)} bytes */\n")
        f.write(f"static const uint8_t bs300_skill_calib_raw[{len(calib_raw)}] = {{\n")
        f.write(bytes_to_c_array(calib_raw, 'calib', 4))
        f.write("\n};\n\n")

        # Program 0 raw
        f.write(f"/* Program 0 Flash raw data, {len(prog0_raw)} bytes */\n")
        f.write(f"static const uint8_t bs300_skill_prog0_raw[{len(prog0_raw)}] = {{\n")
        f.write(bytes_to_c_array(prog0_raw, 'prog0', 4))
        f.write("\n};\n\n")

        # Program 1 raw
        f.write(f"/* Program 1 Flash raw data, {len(prog1_raw)} bytes */\n")
        f.write(f"static const uint8_t bs300_skill_prog1_raw[{len(prog1_raw)}] = {{\n")
        f.write(bytes_to_c_array(prog1_raw, 'prog1', 4))
        f.write("\n};\n\n")

        # Expected Param outputs for Program 0 and Program 1
        for prog_idx in [0, 1]:
            expected = all_expected[prog_idx]
            f.write(f"/* ===== Program {prog_idx} Expected Param Outputs (from Python codegen) ===== */\n")
            f.write(f"#define BS300_SKILL_P{prog_idx}_CMD_COUNT {len(expected)}\n\n")

            # Sorted by cmd_word
            cmd_list = sorted(expected.keys())

            f.write(f"static const uint32_t bs300_skill_p{prog_idx}_cmd_words[] = {{\n")
            for cmd in cmd_list:
                f.write(f"    0x{cmd:06X}U,  /* {expected[cmd][0]} */\n")
            f.write("};\n\n")

            f.write(f"static const uint8_t bs300_skill_p{prog_idx}_expected[{len(expected)}][48] = {{\n")
            for cmd in cmd_list:
                name, data = expected[cmd]
                f.write(f"    {{ /* 0x{cmd:06X} {name} */\n")
                f.write(bytes_to_c_array(data, name, 8))
                f.write("    },\n")
            f.write("};\n\n")

        f.write("#endif /* BS300_SKILL_DATA_H */\n")

    print(f"\nWrote: {header_path}  ({os.path.getsize(header_path)} bytes)")

    # ---- Cross-validation summary ----
    print("\n" + "=" * 60)
    print("Cross-Validation: Python reference vs Chip Readback")
    print("=" * 60)

    for prog_idx in [0, 1]:
        chip = chip_p0 if prog_idx == 0 else chip_p1
        expected = all_expected[prog_idx]
        byte_exact = 0
        tolerated = 0
        mismatches = 0

        for cmd, (name, enc) in sorted(expected.items()):
            if cmd not in chip:
                continue
            chip_data = chip[cmd]
            # Compare word by word
            errs = 0
            max_byte_diff = 0
            for wi in range(16):
                ew = enc[wi * 3] | (enc[wi * 3 + 1] << 8) | (enc[wi * 3 + 2] << 16)
                cw = chip_data[wi * 3] | (chip_data[wi * 3 + 1] << 8) | (chip_data[wi * 3 + 2] << 16)
                if ew != cw:
                    errs += 1
                    for bi in range(3):
                        d = abs((ew >> (bi * 8) & 0xFF) - (cw >> (bi * 8) & 0xFF))
                        if d > max_byte_diff: max_byte_diff = d
            if errs == 0:
                byte_exact += 1
            elif max_byte_diff <= 1:
                tolerated += 1
            else:
                mismatches += 1
                print(f"  P{prog_idx} MISMATCH: 0x{cmd:06X} {name}: {errs}/16 words, max_byte_diff={max_byte_diff}")

        print(f"  P{prog_idx}: {byte_exact} exact, {tolerated} tolerated, {mismatches} mismatch")

    print("\nDone. Compile bs300_convert.c with bs300_skill_data.h to validate.")


if __name__ == '__main__':
    write_header()
