#!/usr/bin/env python3
"""
Cross-validate program_*.json (Flash readback) vs param_values_*.json (MT config)
for both Program 0 and Program 1.
Decodes Flash data and compares every field against the MT config values.
"""
import json
import os
import sys
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PARENT_DIR = os.path.dirname(_SCRIPT_DIR)
sys.path.insert(0, _PARENT_DIR)
from bs300_codegen import parse_program_data, _CH_FREQ_TABLE, _TIME_TABLE, _RATIO_TABLE


def _flash_time_to_ms(idx: int) -> int:
    """Convert Flash time table index to actual ms."""
    return _TIME_TABLE[idx]


def _flash_ratio_to_x100(idx: int) -> int:
    """Convert Flash ratio table index to ratio × 100.
    Uses int(v+0.5) instead of round() to consistently round .5 up (not banker's rounding)."""
    return int(_RATIO_TABLE[idx] * 100 + 0.5)


def _flash_convert(fld_name: str, flash_val: int) -> int:
    """Convert a Flash field value from table index to actual value for comparison."""
    # Time fields: _at, _rt suffix → time table index → ms
    if fld_name.endswith('_at') or fld_name.endswith('_rt'):
        return _flash_time_to_ms(flash_val)
    # Ratio fields: _r suffix (but not _thr threshold)
    if fld_name.endswith('_r') and not fld_name.endswith('_thr'):
        return _flash_ratio_to_x100(flash_val)
    # lmt_th: Flash stores value_in_MT - 30
    if fld_name == 'lmt_th':
        return flash_val + 30
    return flash_val


def validate_program(prog_index):
    """Cross-validate one program's Flash readback against MT config."""
    print(f"\n{'#' * 70}")
    print(f"# Program {prog_index}")
    print(f"{'#' * 70}")

    # ---- Load data ----
    with open(os.path.join(_PARENT_DIR, 'data', f'program_{prog_index}.json'), encoding='utf-8') as f:
        prog = json.load(f)
    with open(os.path.join(_PARENT_DIR, 'data', f'param_values_{prog_index}.json'), encoding='utf-8') as f:
        mt_config = json.load(f)

    # ---- Extract Flash data ----
    full_packets = []
    for item in prog:
        b = [int(x, 16) for x in item['bytes']]
        if len(b) == 53:
            full_packets.append(bytes(b[4:52]))
    flash_data = b''.join(full_packets)
    parsed = parse_program_data(flash_data)

    # ---- Get MT modules ----
    mt_modules = {m['name']: m for m in mt_config['value_in_MT']['modules']}

    errors = []
    warnings = []
    oks = 0

    def err(mod, field, flash_val, mt_val, detail=''):
        errors.append(f"  [{mod}] {field}: Flash={flash_val}, MT={mt_val}  {detail}")

    def ok():
        nonlocal oks
        oks += 1

    def warn(mod, field, msg):
        warnings.append(f"  [{mod}] {field}: {msg}")

    # ============================================================
    # WDRC
    # ============================================================
    print("=" * 70)
    print("WDRC")
    print("=" * 70)
    wdrc_mt = mt_modules['WDRC']
    wdrc_flash = parsed.wdrc

    # bin_gains
    print("\n--- bin_gains ---")
    flash_bin_gains = wdrc_flash.bin_gain
    mt_bin_gains = wdrc_mt['bin_gains']
    bg_match = 0
    for i in range(32):
        expected_flash = mt_bin_gains[i] + 27
        if flash_bin_gains[i] != expected_flash:
            err('WDRC', f'bin_gain[{i}]', flash_bin_gains[i], expected_flash,
                f'(MT={mt_bin_gains[i]})')
        else:
            bg_match += 1
            ok()
    print(f"  bin_gains: {bg_match}/32 match")

    # num_channels
    print("\n--- num_channels ---")
    flash_nch = wdrc_flash.num_channels
    mt_nch = wdrc_mt['num_channels']
    if flash_nch == mt_nch:
        print(f"  num_channels: {flash_nch} ✓")
        ok()
    else:
        err('WDRC', 'num_channels', flash_nch, mt_nch)

    # kneepoints
    flash_kp = wdrc_flash.kneepoints_per_channel
    mt_kp = wdrc_mt['kneepoints']
    # Flash: 0=1KP, 1=2KP; MT stores 1 or 2
    if flash_kp + 1 == mt_kp:
        print(f"  kneepoints: Flash={flash_kp} (={flash_kp+1}KP) MT={mt_kp} ✓")
        ok()
    else:
        err('WDRC', 'kneepoints', flash_kp, mt_kp)

    # output_limiting
    flash_ol = wdrc_flash.output_limiting_sel
    mt_ol = 1 if wdrc_mt.get('output_limiting') else 0
    if flash_ol == mt_ol:
        print(f"  output_limiting: {flash_ol} ✓")
        ok()
    else:
        err('WDRC', 'output_limiting', flash_ol, mt_ol)

    # Per-channel comparison
    print("\n--- Per-channel ---")
    ch_freq_table = _CH_FREQ_TABLE
    for ci, (fch, mch) in enumerate(zip(wdrc_flash.channels, wdrc_mt['channels'])):
        # Frequency
        expected_fidx = ch_freq_table.index(mch['freq_hz']) if mch['freq_hz'] in ch_freq_table else -1
        if fch.frequency_idx != expected_fidx:
            err('WDRC', f'ch{ci}.freq_idx', fch.frequency_idx, expected_fidx,
                f'(freq_hz={mch["freq_hz"]})')
        else:
            ok()

        # EPD
        for fld_flash, fld_mt, conv in [
            ('epd_at', 'epd_at_ms', lambda x: x),
            ('epd_rt', 'epd_rt_ms', lambda x: x),
            ('epd_r', 'epd_ratio', lambda x: int(x * 100 + 0.5)),
        ]:
            fv = _flash_convert(fld_flash, getattr(fch, fld_flash))
            mv = conv(mch[fld_mt])
            if fv != mv:
                err('WDRC', f'ch{ci}.{fld_flash}', fv, mv, f'(MT {fld_mt}={mch[fld_mt]})')
            else:
                ok()

        # Kneepoint 1 & 2 + Limiter
        for prefix in ['kp1', 'kp2']:
            for fld_flash, fld_mt, conv in [
                (f'{prefix}_th', f'{prefix}_th_db', lambda x: x),
                (f'{prefix}_at', f'{prefix}_at_ms', lambda x: x),
                (f'{prefix}_rt', f'{prefix}_rt_ms', lambda x: x),
                (f'{prefix}_r', f'{prefix}_ratio', lambda x: int(x * 100 + 0.5)),
            ]:
                fv = _flash_convert(fld_flash, getattr(fch, fld_flash))
                mv = conv(mch[fld_mt])
                if fv != mv:
                    err('WDRC', f'ch{ci}.{fld_flash}', fv, mv, f'(MT {fld_mt}={mch[fld_mt]})')
                else:
                    ok()

        # Limiter
        for fld_flash, fld_mt, conv in [
            ('lmt_th', 'lmt_th_db', lambda x: x),
            ('lmt_at', 'lmt_at_ms', lambda x: x),
            ('lmt_rt', 'lmt_rt_ms', lambda x: x),
            ('lmt_r', 'lmt_ratio', lambda x: int(x * 100 + 0.5)),
        ]:
            fv = _flash_convert(fld_flash, getattr(fch, fld_flash))
            mv = conv(mch[fld_mt])
            if fv != mv:
                err('WDRC', f'ch{ci}.{fld_flash}', fv, mv, f'(MT {fld_mt}={mch[fld_mt]})')
            else:
                ok()

    print(f"  Per-channel fields: see error list below")

    # ============================================================
    # Volume
    # ============================================================
    print("\n" + "=" * 70)
    print("Volume")
    print("=" * 70)
    vol_mt = mt_modules.get('Volume/Beep', mt_modules.get('Volume', {}))
    vol_flash = parsed.volume
    checks = [
        ('beep_level', 'beep_level_db'),
        ('beep_frequency', 'beep_frequency'),
        ('min_volume', 'min_volume'),
        ('max_volume', 'max_volume'),
    ]
    for ff, fm in checks:
        fv = getattr(vol_flash, ff, None)
        mv = vol_mt.get(fm)
        if fv is not None and mv is not None:
            if fv == mv:
                print(f"  {ff}: Flash={fv} MT={mv} ✓")
                ok()
            else:
                err('Volume', ff, fv, mv)
        else:
            warn('Volume', ff, f'Flash={fv}, MT={mv}')

    # ============================================================
    # Inputs
    # ============================================================
    print("\n" + "=" * 70)
    print("Inputs")
    print("=" * 70)
    inputs_flash = parsed.inputs
    # Find the Input module
    input_key = next((k for k in mt_modules if k.startswith('Input(')), None)
    inputs_mt = mt_modules.get(input_key, {}) if input_key else {}

    if inputs_flash and inputs_mt:
        flash_type = inputs_flash.input_type
        mt_type = (inputs_mt.get('input_type', '') or
                   input_key.replace('Input(', '').replace(')', '').lower().replace(' ', '_') if input_key else '')
        # Normalize both to lowercase-with-underscores
        flash_norm = flash_type.lower().replace(' ', '_')
        mt_norm = mt_type.lower().replace(' ', '_')
        if flash_norm == mt_norm:
            print(f"  input_type: {flash_type} ✓")
            ok()
        else:
            err('Inputs', 'input_type', flash_type, mt_type)
    else:
        print("  (no inputs data)")

    # ============================================================
    # DFBC
    # ============================================================
    print("\n" + "=" * 70)
    print("DFBC")
    print("=" * 70)
    dfbc_flash = parsed.dfbc
    dfbc_mt = mt_modules.get('DFBC', {})
    if dfbc_flash:
        fmode = dfbc_flash.dfbc_mode
        mmode = dfbc_mt.get('mode', '')
        # Normalize: if MT has a string name, convert to expected int value
        _DFBC_MAP = {'Slow FBC': 0x01, 'Slow Weak DFBC': 0x03, 'Slow Strong DFBC': 0x07,
                     'Fast FBC': 0x09, 'Fast Weak DFBC': 0x0B, 'Fast Strong DFBC': 0x0F}
        if isinstance(mmode, str) and mmode in _DFBC_MAP:
            mmode = _DFBC_MAP[mmode]
        if fmode == mmode:
            print(f"  mode: {fmode} ✓")
            ok()
        else:
            err('DFBC', 'mode', fmode, mmode)
    else:
        print("  (no DFBC data)")

    # ============================================================
    # ENR
    # ============================================================
    print("\n" + "=" * 70)
    print("ENR")
    print("=" * 70)
    enr_flash = parsed.enr
    enr_mt = mt_modules.get('ENR', {})

    print("\n--- Smoothing Factors ---")
    for ff, fm in [('nfsf', 'nfsf'), ('nhsf', 'nhsf'), ('nnsf', 'nnsf'), ('snasf', 'snasf')]:
        fv = getattr(enr_flash, ff)
        mv = enr_mt.get(fm)
        if mv is not None:
            expected_flash = mv - 1
            if fv == expected_flash:
                print(f"  {ff}: Flash={fv} (MT={mv}) ✓")
                ok()
            else:
                err('ENR', ff, fv, expected_flash, f'(MT={mv})')
        else:
            warn('ENR', ff, f'Flash={fv}, MT missing')

    flash_nch = enr_flash.num_channels
    mt_nch = enr_mt.get('num_channels')
    if mt_nch:
        if flash_nch == mt_nch:
            print(f"  num_channels: {flash_nch} ✓")
            ok()
        else:
            err('ENR', 'num_channels', flash_nch, mt_nch)

    print("\n--- ENR Per-channel ---")
    if enr_flash.channels and 'channels' in enr_mt:
        for ci, (fch, mch) in enumerate(zip(enr_flash.channels, enr_mt['channels'])):
            expected_fidx = _CH_FREQ_TABLE.index(mch['freq_hz']) if mch['freq_hz'] in _CH_FREQ_TABLE else -1
            if fch.frequency_idx != expected_fidx:
                err('ENR', f'ch{ci}.freq_idx', fch.frequency_idx, expected_fidx,
                    f'(freq_hz={mch["freq_hz"]})')
            else:
                ok()

            if fch.ma != mch.get('max_att_db', -1):
                err('ENR', f'ch{ci}.ma', fch.ma, mch.get('max_att_db', -1))
            else:
                ok()

            if fch.snrth != mch.get('snr_th_db', -1):
                err('ENR', f'ch{ci}.snrth', fch.snrth, mch.get('snr_th_db', -1))
            else:
                ok()

            expected_nt = mch.get('noise_th_db', 10) - 10
            if fch.nt != expected_nt:
                err('ENR', f'ch{ci}.nt', fch.nt, expected_nt,
                    f'(MT noise_th_db={mch.get("noise_th_db")})')
            else:
                ok()

            expected_unt = mch.get('upper_noise_th_db', 40) - 40
            if fch.unt != expected_unt:
                err('ENR', f'ch{ci}.unt', fch.unt, expected_unt,
                    f'(MT upper_noise_th_db={mch.get("upper_noise_th_db")})')
            else:
                ok()

            expected_etr = round(mch.get('exp_trans_ratio', 0) * 100)
            if fch.etr != expected_etr:
                err('ENR', f'ch{ci}.etr', fch.etr, expected_etr,
                    f'(MT exp_trans_ratio={mch.get("exp_trans_ratio")})')
            else:
                ok()

            nr_val = mch.get('noise_red_ratio')
            if nr_val is not None:
                expected_nrr = round(nr_val * 10)
                if fch.nrr != expected_nrr:
                    err('ENR', f'ch{ci}.nrr', fch.nrr, expected_nrr,
                        f'(MT noise_red_ratio={nr_val})')
                else:
                    ok()
            else:
                warn('ENR', f'ch{ci}.nrr', f'Flash={fch.nrr}, MT noise_red_ratio missing')

    # ============================================================
    # ISS
    # ============================================================
    print("\n" + "=" * 70)
    print("ISS")
    print("=" * 70)
    iss_flash = parsed.iss
    iss_mt = mt_modules.get('ISS', {})
    if iss_flash:
        fv = iss_flash.iss_threshold
        mv = iss_mt.get('threshold_db')
        if mv is not None:
            if fv == mv:
                print(f"  threshold: Flash={fv} MT={mv} ✓")
                ok()
            else:
                err('ISS', 'threshold', fv, mv)
    else:
        print("  (no ISS data)")

    # ============================================================
    # WNR
    # ============================================================
    print("\n" + "=" * 70)
    print("WNR")
    print("=" * 70)
    wnr_flash = parsed.wnr
    wnr_mt = mt_modules.get('WNR', {})
    _WNR_PRESET_MAP = {'Minimal': 1, 'Light': 3, 'Moderate': 6, 'Strong': 9, 'Maximum': 12}
    if wnr_flash:
        for ff, fm in [
            ('dual_mic_mode_sel', 'dual_mic_mode'),
            ('suppression_strength_preset', 'suppression_preset'),
        ]:
            fv = getattr(wnr_flash, ff, None)
            mv = wnr_mt.get(fm)
            if fv is not None and mv is not None:
                # Normalize: bool→int, str→int via preset map
                if isinstance(mv, bool):
                    mv = 1 if mv else 0
                elif isinstance(mv, str) and mv in _WNR_PRESET_MAP:
                    mv = _WNR_PRESET_MAP[mv]
                if fv == mv:
                    print(f"  {ff}: Flash={fv} MT={mv} ✓")
                    ok()
                else:
                    err('WNR', ff, fv, mv)
            else:
                warn('WNR', ff, f'Flash={fv}, MT={mv}')
    else:
        print("  (no WNR data)")

    # ============================================================
    # AGCO
    # ============================================================
    print("\n" + "=" * 70)
    print("AGCO")
    print("=" * 70)
    agco_flash = parsed.agco
    agco_mt = mt_modules.get('AGCO', {})
    if agco_flash:
        fv_atk = agco_flash.attack_time
        mv_atk = agco_mt.get('attack_time_01ms', agco_mt.get('attack_time_ms'))
        if mv_atk is not None:
            if fv_atk == mv_atk:
                print(f"  attack_time: Flash={fv_atk} (={fv_atk*0.1}ms) MT={mv_atk} ✓")
                ok()
            else:
                err('AGCO', 'attack_time', fv_atk, mv_atk)

        fv_rel = agco_flash.release_time
        mv_rel = agco_mt.get('release_time_01ms', agco_mt.get('release_time_ms'))
        if mv_rel is not None:
            if fv_rel == mv_rel:
                print(f"  release_time: Flash={fv_rel} (={fv_rel*0.1}ms) MT={mv_rel} ✓")
                ok()
            else:
                err('AGCO', 'release_time', fv_rel, mv_rel)

        fv_thr = agco_flash.threshold
        mv_thr = abs(agco_mt.get('threshold_db', 0))
        if fv_thr == mv_thr:
            print(f"  threshold: Flash={fv_thr} MT={mv_thr} ✓")
            ok()
        else:
            err('AGCO', 'threshold', fv_thr, mv_thr)
    else:
        print("  (no AGCO data)")

    # ============================================================
    # Summary
    # ============================================================
    print("\n" + "=" * 70)
    print(f"SUMMARY — Program {prog_index}")
    print("=" * 70)
    print(f"Total checks passed: {oks}")
    print(f"Errors: {len(errors)}")
    print(f"Warnings: {len(warnings)}")

    if errors:
        print(f"\n--- ERRORS ({len(errors)}) ---")
        for e in errors:
            print(e)

    if warnings:
        print(f"\n--- WARNINGS ({len(warnings)}) ---")
        for w in warnings:
            print(w)

    return oks, errors, warnings


if __name__ == '__main__':
    all_oks = 0
    all_errors = []
    all_warnings = []

    for pi in [0, 1]:
        oks, errors, warnings = validate_program(pi)
        all_oks += oks
        all_errors.extend([f"[P{pi}] {e}" for e in errors])
        all_warnings.extend([f"[P{pi}] {w}" for w in warnings])

    print(f"\n{'#' * 70}")
    print(f"# OVERALL SUMMARY")
    print(f"{'#' * 70}")
    print(f"Total checks passed: {all_oks}")
    print(f"Total errors: {len(all_errors)}")
    print(f"Total warnings: {len(all_warnings)}")
    if all_errors:
        print(f"\n--- ALL ERRORS ({len(all_errors)}) ---")
        for e in all_errors:
            print(e)
    if all_warnings:
        print(f"\n--- ALL WARNINGS ({len(all_warnings)}) ---")
        for w in all_warnings:
            print(w)
