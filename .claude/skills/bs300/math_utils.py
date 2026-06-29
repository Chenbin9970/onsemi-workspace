import math

# Step 3: Program Burn Flash Encoder + Cross-Validation
# ============================================================

import json
import struct


# ---- Lookup Tables ----

def _build_time_table() -> list[int]:
    """Build Table 2-2: Attack/Release Time lookup (122 entries, index → ms)."""
    sparse = {
        0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 7, 8: 8, 9: 9,
        10: 10, 11: 11, 12: 12, 13: 13, 14: 14, 15: 15, 16: 16, 17: 17, 18: 18, 19: 19,
        20: 20, 21: 22, 22: 24, 23: 26, 24: 28, 25: 30, 26: 32, 27: 34, 28: 36, 29: 38,
        30: 40, 31: 42, 32: 44, 33: 46, 34: 48, 35: 50, 36: 55, 37: 60, 38: 65, 39: 70,
        40: 75, 41: 80, 42: 85, 43: 90, 44: 95, 45: 100, 46: 110, 47: 120, 48: 130, 49: 140,
        50: 150, 51: 160, 52: 170, 53: 180, 54: 190, 55: 200, 56: 220, 57: 240, 58: 260, 59: 280,
        60: 300, 61: 320, 62: 340, 63: 360, 64: 380, 65: 400, 66: 420, 67: 440, 68: 460, 69: 480,
        70: 500, 71: 550, 72: 600, 73: 650, 74: 700, 75: 750, 76: 800, 77: 850, 78: 900, 79: 950,
        80: 1000, 81: 1100, 82: 1200, 83: 1300, 84: 1400, 85: 1500, 86: 1600, 87: 1700, 88: 1800, 89: 1900,
        90: 2000, 91: 2200, 92: 2500, 93: 2600, 94: 2800, 95: 3000, 96: 3200, 97: 3400, 98: 3600, 99: 3800,
        100: 4000, 101: 4200, 102: 4400, 103: 4600, 104: 4800, 105: 5000, 106: 5500, 107: 6000, 108: 6500, 109: 7000,
        110: 7500, 111: 8000, 112: 8500, 113: 9000, 114: 9500, 115: 10000, 116: 11000, 117: 12000, 118: 13000, 119: 14000,
        120: 15000, 121: 16000,
    }
    return [sparse[i] for i in range(122)]


_TIME_TABLE = _build_time_table()


# Table 2-3: WDRC Ratio lookup (128 entries, index → ratio)
# Exact values from BS300 Communication Protocol Handbook
_RATIO_TABLE = [
    0.000, 0.220, 0.250, 0.280, 0.300, 0.320, 0.350, 0.380,  # 0-7
    0.400, 0.430, 0.450, 0.470, 0.500, 0.520, 0.550, 0.570,  # 8-15
    0.600, 0.630, 0.650, 0.680, 0.700, 0.730, 0.750, 0.770,  # 16-23
    0.800, 0.820, 0.850, 0.880, 0.900, 0.930, 0.950, 0.980,  # 24-31
    1.000, 1.010, 1.020, 1.031, 1.042, 1.053, 1.064, 1.075,  # 32-39
    1.087, 1.099, 1.111, 1.124, 1.136, 1.149, 1.163, 1.176,  # 40-47
    1.190, 1.205, 1.220, 1.235, 1.250, 1.266, 1.282, 1.299,  # 48-55
    1.316, 1.333, 1.351, 1.370, 1.389, 1.408, 1.429, 1.449,  # 56-63
    1.471, 1.493, 1.515, 1.538, 1.563, 1.587, 1.613, 1.639,  # 64-71
    1.667, 1.695, 1.724, 1.754, 1.786, 1.818, 1.852, 1.887,  # 72-79
    1.923, 1.961, 2.000, 2.041, 2.083, 2.128, 2.174, 2.222,  # 80-87
    2.273, 2.326, 2.381, 2.439, 2.500, 2.564, 2.632, 2.703,  # 88-95
    2.778, 2.857, 2.941, 3.030, 3.125, 3.226, 3.333, 3.448,  # 96-103
    3.571, 3.704, 3.846, 4.000, 4.167, 4.348, 4.545, 4.762,  # 104-111
    5.000, 5.260, 5.560, 5.880, 6.250, 6.670, 7.140, 7.690,  # 112-119
    8.330, 9.090, 10.000, 11.110, 12.500, 14.290, 16.670, 20.000,  # 120-127
]

# Frequency table (index → Hz)
_FREQ_TABLE = [
    0, 125, 375, 625, 875, 1125, 1375, 1625,
    1875, 2125, 2375, 2625, 2875, 3125, 3375, 3625,
    3875, 4125, 4375, 4625, 4875, 5125, 5375, 5625,
    5875, 6125, 6375, 6625, 6875, 7125, 7375, 7625,
]

# DFBC mode map
_DFBC_MODE_MAP = {
    'Slow FBC': 0x01, 'Slow Weak DFBC': 0x03, 'Slow Strong DFBC': 0x07,
    'Fast FBC': 0x09, 'Fast Weak DFBC': 0x0B, 'Fast Strong DFBC': 0x0F,
}

# WNR suppression preset map
_WNR_PRESET_MAP = {
    'Minimal': 1, 'Light': 3, 'Moderate': 6, 'Strong': 9, 'Maximum': 12,
}


# ---- Lookup Helpers ----

def _time_to_index(ms: int) -> int:
    """Convert time in ms to the closest Table 2-2 index."""
    best_idx = 0
    best_diff = abs(_TIME_TABLE[0] - ms)
    for i, t in enumerate(_TIME_TABLE):
        d = abs(t - ms)
        if d < best_diff:
            best_diff = d
            best_idx = i
    return best_idx


def _ratio_to_index(ratio: float) -> int:
    """Convert ratio to the closest Table 2-3 index."""
    best_idx = 1
    best_diff = abs(_RATIO_TABLE[1] - ratio)
    for i in range(1, 128):
        d = abs(_RATIO_TABLE[i] - ratio)
        if d < best_diff:
            best_diff = d
            best_idx = i
    return best_idx


def _freq_to_index(hz: int) -> int:
    """Convert frequency in Hz to channel table index."""
    for i, f in enumerate(_FREQ_TABLE):
        if f == hz:
            return i
    raise ValueError(f"Frequency {hz} Hz not found in table")


# ---- Step 3: Parameter Encoder Functions ----


# Step 4: Fixed-Point Math Tools
# ============================================================

def frac24_to_s32(val24: int) -> int:
    """Sign-extend a 24-bit value to 32-bit signed integer."""
    assert 0 <= val24 <= 0xFFFFFF, f"val24 out of range: 0x{val24:X}"
    if val24 & 0x800000:
        return val24 - 0x1000000
    return val24


def clamp_u32(v: int, lo: int, hi: int) -> int:
    """Clamp unsigned 32-bit value to [lo, hi]."""
    if v < lo:
        return lo
    if v > hi:
        return hi
    return v


def clamp_s32(v: int, lo: int, hi: int) -> int:
    """Clamp signed 32-bit value to [lo, hi]."""
    if v < lo:
        return lo
    if v > hi:
        return hi
    return v


# ============================================================
# Step 4 Tests
# ============================================================

def test_step4():
    print("=== Step 4: Fixed-Point Math Tests ===\n")

    # 4.1 frac24_to_s32 — sign extension
    print("4.1 frac24_to_s32 (sign-extend 24-bit → 32-bit signed):")
    assert frac24_to_s32(0x000000) == 0
    assert frac24_to_s32(0x000001) == 1
    assert frac24_to_s32(0x7FFFFF) == 0x7FFFFF       # max positive
    assert frac24_to_s32(0x800000) == -0x800000       # min negative
    assert frac24_to_s32(0xFFFFFF) == -1              # -1
    assert frac24_to_s32(0x800001) == -0x7FFFFF       # most negative + 1
    print(f"  frac24_to_s32(0x000000) = {frac24_to_s32(0x000000)}  ✓")
    print(f"  frac24_to_s32(0x7FFFFF) = {frac24_to_s32(0x7FFFFF)}  ✓")
    print(f"  frac24_to_s32(0x800000) = {frac24_to_s32(0x800000)}  ✓")
    print(f"  frac24_to_s32(0xFFFFFF) = {frac24_to_s32(0xFFFFFF)}  ✓")

    # 4.2 clamp
    print("\n4.2 clamp_u32 / clamp_s32:")
    assert clamp_u32(5, 0, 10) == 5
    assert clamp_u32(0, 0, 10) == 0
    assert clamp_u32(10, 0, 10) == 10
    assert clamp_u32(15, 0, 10) == 10   # above hi
    assert clamp_u32(4294967295, 0, 0xFFF) == 0xFFF  # overflow check
    print(f"  clamp_u32(5, 0, 10) = {clamp_u32(5, 0, 10)}  ✓")
    print(f"  clamp_u32(15, 0, 10) = {clamp_u32(15, 0, 10)}  ✓")

    assert clamp_s32(5, -10, 10) == 5
    assert clamp_s32(-15, -10, 10) == -10  # below lo
    assert clamp_s32(-5, -10, 10) == -5
    assert clamp_s32(15, -10, 10) == 10
    print(f"  clamp_s32(5, -10, 10) = {clamp_s32(5, -10, 10)}  ✓")
    print(f"  clamp_s32(-15, -10, 10) = {clamp_s32(-15, -10, 10)}  ✓")

    # 4.3 Lookup tables (already in code, verify consistency)
    print("\n4.3 Lookup Table Verification:")

    # Table 2-2: Attack/Release Time (122 entries)
    assert len(_TIME_TABLE) == 122
    assert _TIME_TABLE[0] == 0
    assert _TIME_TABLE[30] == 40      # manual: idx30 → 40ms
    assert _TIME_TABLE[60] == 300     # manual: idx60 → 300ms
    assert _TIME_TABLE[80] == 1000    # idx80 → 1s
    assert _TIME_TABLE[121] == 16000  # max: 16s
    print(f"  Time table: {len(_TIME_TABLE)} entries, [0]=0ms, [30]=40ms, [121]=16000ms  ✓")

    # Table 2-3: WDRC Ratio (128 entries)
    assert len(_RATIO_TABLE) == 128
    assert abs(_RATIO_TABLE[0] - 0.000) < 0.001
    assert abs(_RATIO_TABLE[1] - 0.220) < 0.001
    assert abs(_RATIO_TABLE[32] - 1.000) < 0.001  # unity at idx32
    assert abs(_RATIO_TABLE[64] - 1.471) < 0.001
    assert abs(_RATIO_TABLE[96] - 2.778) < 0.001
    assert abs(_RATIO_TABLE[127] - 20.000) < 0.001  # max
    # Ratio table is monotonically increasing (except idx0)
    for i in range(2, 128):
        assert _RATIO_TABLE[i] >= _RATIO_TABLE[i - 1], \
            f"Ratio table not monotonic at idx {i}: {_RATIO_TABLE[i-1]} > {_RATIO_TABLE[i]}"
    print(f"  Ratio table: {len(_RATIO_TABLE)} entries, monotonic increasing, [1]=0.22, [127]=20.0  ✓")

    # 4.4 Frequency table
    print("\n4.4 Frequency Table:")
    assert len(_FREQ_TABLE) == 32
    assert _FREQ_TABLE[0] == 0
    assert _FREQ_TABLE[1] == 125   # step = 250Hz from idx 1
    assert _FREQ_TABLE[31] == 7625
    for i in range(2, 32):
        assert _FREQ_TABLE[i] - _FREQ_TABLE[i - 1] == 250, \
            f"Freq step not 250 at idx {i}"
    print(f"  Freq table: 32 entries, step=250Hz, [0]=0, [1]=125, [31]=7625  ✓")

    # 4.5 Lookup helpers: time → index → time roundtrip
    print("\n4.5 Lookup roundtrip:")
    for ms in [0, 10, 40, 100, 300, 500, 1000, 2000, 5000, 10000, 16000]:
        idx = _time_to_index(ms)
        recovered = _TIME_TABLE[idx]
        # The recovered value should be the closest table entry to ms
        assert recovered == min(_TIME_TABLE, key=lambda t: abs(t - ms))
        print(f"  time_to_index({ms}ms) → idx={idx} → {recovered}ms  ✓")
    # 4.5b ratio lookup roundtrip
    for ratio in [0.22, 0.50, 1.00, 1.50, 2.00, 3.00, 5.00, 10.00, 20.00]:
        idx = _ratio_to_index(ratio)
        recovered = _RATIO_TABLE[idx]
        assert recovered == min(_RATIO_TABLE[1:], key=lambda r: abs(r - ratio))
        print(f"  ratio_to_index({ratio:.2f}) → idx={idx} → {recovered:.3f}  ✓")

    print("\n=== Step 4: ALL TESTS PASSED ===\n")


# ============================================================
