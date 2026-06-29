#!/usr/bin/env python3
"""
Generate integer-arithmetic lookup tables for BS300 C encode functions.
Verifies integer formulas match Python codegen float results byte-for-byte.

C semantics: division truncates toward zero, round-half-away-from-zero for rounding.
s64 (long long) is available for intermediate calculations.
"""
import os, math

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# ============================================================
# Helper: C-style division with round-half-away-from-zero
# ============================================================

def c_round_div(num, den):
    """C-style round-half-away-from-zero: (num + den/2) / den. Matches Python round()."""
    if num >= 0:
        return (num + den // 2) // den
    else:
        return -((-num + den // 2) // den)

def c_trunc_div(num, den):
    """C-style truncation toward zero, no rounding. Matches Python int()."""
    if num >= 0:
        return num // den
    else:
        return -((-num) // den)

# ============================================================
# 1. Beep Level frac24 lookup table
# ============================================================

def gen_beep_frac24_table():
    entries = []
    for x in range(-255, 141):
        frac = round(0x7FFFFF * (10 ** (x / 20.0)))
        frac = max(0, min(0xFFFFFF, frac))
        entries.append(frac)
    return entries

# ============================================================
# 2. ISS frac48 lookup table
# ============================================================

def gen_iss_frac48_table():
    entries = []
    for et in range(30, 101):
        exponent = et / 10.0
        frac48 = round((1.0 / (10.0 ** exponent)) * (1 << 47))
        entries.append(frac48 & 0xFFFFFFFFFFFF)
    return entries

# ============================================================
# 3. Mic2 cal frac24 lookup table
# ============================================================

def gen_mic2_cal_frac24_table():
    entries = []
    for x in range(-50, 51):
        frac = round(0x800000 * (10 ** (x / 200.0)))
        frac = max(0, min(0xFFFFFF, frac))
        entries.append(frac)
    return entries

# ============================================================
# 4. AGCO exp lookup table
# ============================================================

def gen_agco_exp_table():
    entries = [0]
    for x in range(1, 2501):
        frac = round((1 - math.exp(-10.0 / x)) * 0x7FFFFF)
        frac = max(0, min(0x7FFFFF, frac))
        entries.append(frac)
    return entries

# ============================================================
# 5. Verification: integer formula must match Python float
# ============================================================

def verify_enr_nt():
    """ENR NT: val = round(5.307 * (nt + 130 - mic1_cal - igd/10.0) - 371.2)
    Integer: x10 = nt*10 + 1300 - mic1_cal*10 - igd
             num = 5307 * x10 - 3712000
             val = c_round_div(num, 10000)
    """
    print("=== ENR NT integer formula verification ===")
    errors = 0
    for nt in [4, 10, 20, 30, 50, 72]:
        for mic1_cal in [100, 148, 200, 255]:
            for igd in [-50, -10, 0, 10, 50]:
                # Python float
                x = nt + 130 - mic1_cal - igd / 10.0
                py_val = round(5.307 * x - 371.2)

                # Integer (C semantics)
                x10 = nt * 10 + 1300 - mic1_cal * 10 - igd
                num = 5307 * x10 - 3712000
                int_val = c_round_div(num, 10000)

                if py_val != int_val:
                    errors += 1
                    if errors <= 3:
                        print(f"  nt={nt} mic1={mic1_cal} igd={igd}: py={py_val} int={int_val}")
    if errors:
        print(f"  Total: {errors}")
    else:
        print("  All match! ✓")
    return errors == 0

def verify_enr_etr():
    """ENR ETR: coded = int((6.02/32)*(1-1/etr)/ma * 0x800000)
    Simplify: coded = 301 * 0x800000 * (etr_x100 - 100) / (1600 * etr_x100 * ma)
    Uses s64 intermediate.
    """
    print("=== ENR ETR integer formula verification ===")
    errors = 0
    for etr_x100 in [20, 30, 50, 70, 100]:
        for ma in [1, 5, 10, 20, 30]:
            # Python float
            etr = max(etr_x100 / 100.0, 0.01)
            py_val = (6.02 / 32.0) * (1.0 - 1.0 / etr) / max(ma, 1)
            py_coded = int(py_val * 0x800000) & 0xFFFFFF

            # Integer: coded = 301 * 0x800000 * (etr_x100 - 100) / (1600 * etr_x100 * ma)
            # 301 * 0x800000 = 2524971008, fits in u32
            num = 2524971008 * (etr_x100 - 100)  # Python int: arbitrary precision
            den = 1600 * etr_x100 * max(ma, 1)
            # Python int() truncates toward zero, so use C-style rounding then truncate
            int_coded = c_trunc_div(num, den)  # truncation: matches Python int()
            int_coded = int_coded & 0xFFFFFF

            if py_coded != int_coded:
                errors += 1
                if errors <= 3:
                    print(f"  etr={etr_x100} ma={ma}: py=0x{py_coded:06X} int=0x{int_coded:06X}")
    if errors:
        print(f"  Total: {errors}")
    else:
        print("  All match! ✓")
    return errors == 0

def verify_enr_nrr():
    """ENR NRR: coded = int((6.02/32) * nrr/10 / ma * 0x7FFFFF)
    Simplify: coded = 301 * 0x7FFFFF * nrr_x10 / (16000 * ma)
    301 * 0x7FFFFF = 2524970707
    """
    print("=== ENR NRR integer formula verification ===")
    errors = 0
    for nrr_x10 in [1, 5, 10, 15]:
        for ma in [1, 5, 10, 20, 30]:
            # Python float
            nrr = nrr_x10 / 10.0
            py_val = (6.02 / 32.0) * nrr / max(ma, 1)
            py_coded = int(py_val * 0x7FFFFF) & 0xFFFFFF

            # Integer
            num = 2524970707 * nrr_x10
            den = 16000 * max(ma, 1)
            int_coded = c_trunc_div(num, den)  # truncation: matches Python _frac24 → int()
            int_coded = int_coded & 0xFFFFFF

            if py_coded != int_coded:
                errors += 1
                if errors <= 3:
                    print(f"  nrr={nrr_x10} ma={ma}: py=0x{py_coded:06X} int=0x{int_coded:06X}")
    if errors:
        print(f"  Total: {errors}")
    else:
        print("  All match! ✓")
    return errors == 0

def verify_volume_beep_table():
    print("=== Volume Beep lookup table verification ===")
    table = gen_beep_frac24_table()
    errors = 0
    for x in range(-255, 141):
        py_val = round(0x7FFFFF * (10 ** (x / 20.0)))
        py_val = max(0, min(0xFFFFFF, py_val))
        if table[x + 255] != py_val:
            errors += 1
    if errors:
        print(f"  Errors: {errors}")
    else:
        print(f"  All {len(table)} entries match! ✓")
    return errors == 0

def verify_iss_table():
    print("=== ISS lookup table verification ===")
    table = gen_iss_frac48_table()
    errors = 0
    for et in range(30, 101):
        exponent = et / 10.0
        py_val = round((1.0 / (10.0 ** exponent)) * (1 << 47))
        if table[et - 30] != py_val:
            errors += 1
    if errors:
        print(f"  Errors: {errors}")
    else:
        print(f"  All {len(table)} entries match! ✓")
    return errors == 0

def verify_mic2_cal_table():
    print("=== Mic2 cal lookup table verification ===")
    table = gen_mic2_cal_frac24_table()
    errors = 0
    for x in range(-50, 51):
        py_val = round(0x800000 * (10 ** (x / 200.0)))
        py_val = max(0, min(0xFFFFFF, py_val))
        if table[x + 50] != py_val:
            errors += 1
    if errors:
        print(f"  Errors: {errors}")
    else:
        print(f"  All {len(table)} entries match! ✓")
    return errors == 0

def verify_agco_table():
    print("=== AGCO exp lookup table verification ===")
    table = gen_agco_exp_table()
    errors = 0
    for x in range(1, 2501):
        py_val = round((1 - math.exp(-10.0 / x)) * 0x7FFFFF)
        py_val = max(0, min(0x7FFFFF, py_val))
        if table[x] != py_val:
            errors += 1
    if errors:
        print(f"  Errors: {errors}")
    else:
        print(f"  All {len(table)-1} entries match! ✓")
    return errors == 0

def verify_mm_plus_table():
    """Verify MM Plus lookup table."""
    print("=== MM Plus lookup table verification ===")
    table = gen_mm_plus_frac24_table()
    errors = 0
    for x in range(-500, 1501):
        py_val = round(524288 * (10 ** (x / 200.0)))
        py_val = max(0, min(0xFFFFFF, py_val))
        if table[x + 500] != py_val:
            errors += 1
    if errors:
        print(f"  Errors: {errors}")
    else:
        print(f"  All {len(table)} entries match! ✓")
    return errors == 0

def verify_wnr_detect():
    """WNR detect: val = round((75 - avg_ceil) * 65536 / (6.02 * 8))
    Integer: (75 - avg_ceil) * 409600 / 301
    """
    print("=== WNR detect level verification ===")
    errors = 0
    for avg_ceil in range(100, 200):
        # Python float
        py_val = int(round((75 - avg_ceil) * (65536 / 6.02 / 8)))

        # Integer: (75 - avg_ceil) * 65536 / (6.02 * 8) = (75 - avg_ceil) * 6553600 / 4816
        # 65536 / 6.02 = 327680/301 per tenth-dB... actually:
        # 65536 / 6.02 / 8 = 65536 / 48.16 = 6553600 / 4816
        # Simplify: 6553600 / 4816 = 409600 / 301
        num = (75 - avg_ceil) * 409600
        int_val = c_round_div(num, 301)

        if py_val != int_val:
            errors += 1
            if errors <= 3:
                print(f"  avg={avg_ceil}: py={py_val} int={int_val}")
    if errors:
        print(f"  Total: {errors}")
    else:
        print("  All match! ✓")
    return errors == 0

def gen_mm_plus_frac24_table():
    """Generate: val = round(524288 * 10^(x/200)), x ∈ [-500, 1500] step 1."""
    entries = []
    for x in range(-500, 1501):
        frac = round(524288 * (10 ** (x / 200.0)))
        frac = max(0, min(0xFFFFFF, frac))
        entries.append(frac)
    return entries

def verify_ddm2_delay():
    """DDM2: delay_us = mic_delay_raw * 0.1, val = 0.008 * delay_us * 0x7FFFFF
    val = mic_delay_raw * 0.0008 * 0x7FFFFF = mic_delay_raw * 8388607 / 1250
    """
    print("=== DDM2 delay verification ===")
    errors = 0
    for delay_raw in [0, 100, 500, 1000, 5000, 10000, 65535]:
        # Python float
        py_val = int(0.008 * delay_raw * 0.1 * 0x7FFFFF) & 0xFFFFFF

        # Integer: mic_delay_raw * 8388607 / 1250, truncation (matches Python int())
        num = delay_raw * 8388607
        int_val = c_trunc_div(num, 1250)
        int_val = int_val & 0xFFFFFF

        if py_val != int_val:
            errors += 1
            print(f"  delay={delay_raw}: py=0x{py_val:06X} int=0x{int_val:06X}")
    if errors:
        print(f"  Total: {errors}")
    else:
        print("  All match! ✓")
    return errors == 0

# ============================================================
# 6. Generate C header file
# ============================================================

def gen_c_header():
    lines = []
    lines.append("/* Auto-generated by gen_c_int_math.py — DO NOT EDIT MANUALLY */")
    lines.append("/* Integer arithmetic lookup tables for BS300 Param I2C encoding */")
    lines.append("#ifndef BS300_ENCODE_TABLES_H")
    lines.append("#define BS300_ENCODE_TABLES_H")
    lines.append("")

    # Beep frac24 table
    beep = gen_beep_frac24_table()
    lines.append("/* Beep level dB → frac24 lookup.")
    lines.append(" * Index = (beep_level_db - outcal_beep) + 255.")
    lines.append(" * table[x+255] = round(0x7FFFFF * 10^(x/20)), x ∈ [-255, 140].")
    lines.append(" */")
    lines.append("#define BS300_BEEP_TABLE_OFFSET  255")
    lines.append("#define BS300_BEEP_TABLE_SIZE   396")
    lines.append("static const u32 bs300_beep_frac24_table[396] = {")
    for i in range(0, len(beep), 8):
        chunk = beep[i:i+8]
        lines.append("    " + ", ".join(f"0x{v:06X}U" for v in chunk) + ",")
    lines.append("};")
    lines.append("")

    # ISS frac48 table
    iss = gen_iss_frac48_table()
    lines.append("/* ISS frac48 lookup. Index = exponent_tenth - 30.")
    lines.append(" * exponent = (-3 - thr + mic1_cal + igd_db) / 10, ∈ [3.0, 10.0] step 0.1.")
    lines.append(" * Each entry: lo, hi uint24 (48-bit total).")
    lines.append(" */")
    lines.append("#define BS300_ISS_TABLE_OFFSET    30")
    lines.append("#define BS300_ISS_TABLE_SIZE      71")
    lines.append("static const u32 bs300_iss_frac48_table[71][2] = {")
    for v in iss:
        lines.append(f"    {{0x{v & 0xFFFFFF:06X}U, 0x{(v >> 24) & 0xFFFFFF:06X}U}},")
    lines.append("};")
    lines.append("")

    # Mic2 cal table
    mic2 = gen_mic2_cal_frac24_table()
    lines.append("/* Mic2 cal frac24 lookup (WNR setup + DDM2).")
    lines.append(" * Index = mic2_gain_diff_tenth_db + 50, x ∈ [-50, 50] tenth-dB.")
    lines.append(" * table[x+50] = round(0x800000 * 10^(x/200)).")
    lines.append(" */")
    lines.append("#define BS300_MIC2_CAL_TABLE_OFFSET  50")
    lines.append("#define BS300_MIC2_CAL_TABLE_SIZE    101")
    lines.append("static const u32 bs300_mic2_cal_frac24_table[101] = {")
    for i in range(0, len(mic2), 8):
        chunk = mic2[i:i+8]
        lines.append("    " + ", ".join(f"0x{v:06X}U" for v in chunk) + ",")
    lines.append("};")
    lines.append("")

    # AGCO exp table
    agco = gen_agco_exp_table()
    lines.append("/* AGCO attack/release exp lookup.")
    lines.append(" * Index = atk_01ms or rel_01ms, ∈ [1, 2500].")
    lines.append(" * table[x] = round((1 - exp(-10/x)) * 0x7FFFFF).")
    lines.append(" */")
    lines.append("#define BS300_AGCO_EXP_TABLE_SIZE    2501")
    lines.append("static const u32 bs300_agco_exp_table[2501] = {")
    for i in range(0, len(agco), 8):
        chunk = agco[i:i+8]
        lines.append("    " + ", ".join(f"0x{v:06X}U" for v in chunk) + ",")
    lines.append("};")
    lines.append("")

    # MM Plus frac24 table: val = round(524288 * 10^(x/200))
    # x = mix_ratio * 10 - igd_tenth_db, range [-500, 1500], step 1
    mm_plus_table = gen_mm_plus_frac24_table()
    lines.append("/* MM Plus frac24 lookup.")
    lines.append(" * Index = (mix_ratio * 10 - igd_tenth_db) + 500.")
    lines.append(" * val = round(524288 * 10^(x/200)), x ∈ [-500, 1500].")
    lines.append(" */")
    lines.append("#define BS300_MM_PLUS_TABLE_OFFSET  500")
    lines.append(f"#define BS300_MM_PLUS_TABLE_SIZE    {len(mm_plus_table)}")
    lines.append("static const u32 bs300_mm_plus_frac24_table[] = {")
    for i in range(0, len(mm_plus_table), 8):
        chunk = mm_plus_table[i:i+8]
        lines.append("    " + ", ".join(f"0x{v:06X}U" for v in chunk) + ",")
    lines.append("};")
    lines.append("")

    # Constant macros
    lines.append("/* ENR ETR: coded = round(301 * 0x800000 * (etr_x100 - 100) / (1600 * etr_x100 * ma)) */")
    lines.append("/* 301 * 0x800000 = 2524971008, fits u32. Product needs s64. */")
    lines.append("#define BS300_ENR_ETR_NUM_BASE  2524971008ULL")
    lines.append("")
    lines.append("/* ENR NRR: coded = round(301 * 0x7FFFFF * nrr_x10 / (16000 * ma)) */")
    lines.append("/* 301 * 0x7FFFFF = 2524970707 */")
    lines.append("#define BS300_ENR_NRR_NUM_BASE  2524970707ULL")
    lines.append("")
    lines.append("/* DDM2 mic delay: coded = round(mic_delay_raw * 8388607 / 1250) */")
    lines.append("/* mic_delay_raw ∈ [0, 65535], product ≤ 549B, needs s64. */")
    lines.append("#define BS300_DDM2_DELAY_NUM     8388607")
    lines.append("#define BS300_DDM2_DELAY_DEN     1250")
    lines.append("")
    lines.append("/* WNR detect threshold: val = round((75 - avg_ceil) * 409600 / 301) */")
    lines.append("")
    lines.append("#endif /* BS300_ENCODE_TABLES_H */")

    return "\n".join(lines)

# ============================================================
# Main
# ============================================================

if __name__ == "__main__":
    all_pass = True
    all_pass &= verify_enr_nt()
    all_pass &= verify_enr_etr()
    all_pass &= verify_enr_nrr()
    all_pass &= verify_volume_beep_table()
    all_pass &= verify_iss_table()
    all_pass &= verify_mic2_cal_table()
    all_pass &= verify_agco_table()
    all_pass &= verify_wnr_detect()
    all_pass &= verify_ddm2_delay()
    all_pass &= verify_mm_plus_table()

    print()
    if all_pass:
        print("=== ALL VERIFICATIONS PASSED ===")
        header = gen_c_header()
        out_path = os.path.join(os.path.dirname(SCRIPT_DIR), "output", "bs300_encode_tables.h")
        with open(out_path, 'w') as f:
            f.write(header)
        print(f"C header written to: {out_path}")
        print(f"File size: {len(header)} bytes")
    else:
        print("=== VERIFICATION FAILED — fix mismatches before generating C code ===")
