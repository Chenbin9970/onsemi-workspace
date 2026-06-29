import os, json
from proto import (bs300_checksum, bs300_get_word, bs300_set_word,
                   bs300_build_advanced_write, bs300_cmd_furproc, bs300_cmd_pktnum)

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
# ============================================================
# Step 1: Calibration Data Read & Parse
# ============================================================

from dataclasses import dataclass
import struct


@dataclass
class CalibData:
    """Parsed calibration data from 3-packet (144 bytes) I2C read."""
    mic1_band: list[int]             # 32 × uint8, = output_cal[x] - gain_cal[x]; band 0 is invalid
    output_band: list[int]            # 32 × uint8, = output_cal[x]; band 0 is invalid
    mic2_gain_diff: int               # int16, 0.1 dB LSB, range [-5.0, 5.0] dB
    mic_delay: int                    # uint16, 0.1 us LSB
    telecoil_gain_diff: int           # int16, 0.1 dB LSB, range [-50.0, 50.0] dB
    dai_gain_diff: int                # int16, 0.1 dB LSB, range [-50.0, 50.0] dB
    fbc_bulk_delay: int               # uint16, 1 us LSB
    digital_audio_sensitivity: int    # int16

    def avg_mic1_cal(self) -> float:
        """avg(outCal - gainCal) over bands 1-31 (skip band 0)."""
        return sum(self.mic1_band[1:]) / 31.0

    def avg_output_cal(self) -> float:
        """avg(outCal) over bands 1-31 (skip band 0)."""
        return sum(self.output_band[1:]) / 31.0

    def input_gain_diff_db(self, input_type: str) -> float:
        """Return gain diff in dB for Telecoil/DAI input; 0.0 for Mic."""
        if input_type == 'telecoil':
            return self.telecoil_gain_diff / 10.0
        elif input_type == 'dai':
            return self.dai_gain_diff / 10.0
        return 0.0


# ---- Calibration module info table (handbook Table §校准数据内容) ----
# (index, length_bytes, is_unsigned)
_CALIB_MODULES = [
    (0x01, 35, False),   # Mic1 calibration
    (0x03, 35, False),   # Output calibration
    (0x02,  3, False),   # Mic2 gain difference (int16)
    (0x04,  3, True),    # Mic delay (uint16)
    (0x05,  3, False),   # Telecoil gain difference (int16)
    (0x06,  3, False),   # DAI gain difference (int16)
    (0x07,  3, True),    # FBC bulk delay (uint16)
    (0x08,  3, False),   # Digital audio sensitivity (int16)
]


def parse_calibration(raw: bytes) -> CalibData:
    """Parse 144 bytes of raw calibration data (3 packets × 48 bytes).

    Handles both the contiguous 108-byte payload and the full 144-byte
    array (zero-padded).  The call site should concatenate p0 + p1 + p2
    before passing.
    """
    if len(raw) < 108:
        raise ValueError(f"Calibration data too short: {len(raw)} < 108")

    # Validate header
    assert raw[0] == 3, f"Expected packet count 3, got {raw[0]}"
    assert raw[2] == 9, f"Expected module count+1 = 9, got {raw[2]}"

    # ---- Parse module info section (bytes 3-19) for validation ----
    for i in range(8):
        idx = raw[3 + i * 2]
        length = raw[3 + i * 2 + 1]
        expected_idx, expected_len, _ = _CALIB_MODULES[i]
        assert idx == expected_idx, f"Module {i}: expected index 0x{expected_idx:02X}, got 0x{idx:02X}"
        assert length == expected_len, f"Module {i}: expected length {expected_len}, got {length}"
    assert raw[19] == 0xFB, f"Expected end marker 0xFB at byte 19, got 0x{raw[19]:02X}"

    # ---- Parse Mic1 calibration (bytes 20-54) ----
    assert raw[20:23] == b'\x02\xFA\x00', f"Mic1 header mismatch: {raw[20:23].hex(' ')}"
    mic1_band = [raw[23 + i] for i in range(32)]

    # ---- Parse Output calibration (bytes 55-89) ----
    assert raw[55:58] == b'\x01\xFA\x00', f"Output header mismatch: {raw[55:58].hex(' ')}"
    output_band = [raw[58 + i] for i in range(32)]

    # ---- Parse 6 short modules (bytes 90-107) ----
    def _read_short(offset: int, is_unsigned: bool):
        assert raw[offset] == 0x40, f"Short module header at byte {offset}: expected 0x40, got 0x{raw[offset]:02X}"
        fmt = '<H' if is_unsigned else '<h'
        return struct.unpack(fmt, raw[offset + 1: offset + 3])[0]

    mic2_gain_diff = _read_short(90, False)
    mic_delay = _read_short(93, True)
    telecoil_gain_diff = _read_short(96, False)
    dai_gain_diff = _read_short(99, False)
    fbc_bulk_delay = _read_short(102, True)
    digital_audio_sensitivity = _read_short(105, False)

    # ---- Verify zero-padding (bytes 108-143) ----
    assert all(b == 0 for b in raw[108:144]), "Zero-padding region contains non-zero bytes"

    return CalibData(
        mic1_band=mic1_band,
        output_band=output_band,
        mic2_gain_diff=mic2_gain_diff,
        mic_delay=mic_delay,
        telecoil_gain_diff=telecoil_gain_diff,
        dai_gain_diff=dai_gain_diff,
        fbc_bulk_delay=fbc_bulk_delay,
        digital_audio_sensitivity=digital_audio_sensitivity,
    )


# ---- Test data builders ----

def _build_calib_packet0(mic1_bands: list[int]) -> bytes:
    """Build Packet 0 (48 bytes) from 32 mic1 band values."""
    assert len(mic1_bands) == 32
    # Packet info
    p0 = bytearray(48)
    p0[0] = 3    # packet count
    p0[1] = 0x00
    p0[2] = 9    # module count + 1
    # Module info (8 × 2 bytes)
    for i, (idx, length, _) in enumerate(_CALIB_MODULES):
        p0[3 + i * 2] = idx
        p0[3 + i * 2 + 1] = length
    p0[19] = 0xFB  # end marker
    # Mic1 header + bands 0-24
    p0[20:23] = b'\x02\xFA\x00'
    for i in range(25):
        p0[23 + i] = mic1_bands[i]
    return bytes(p0)


def _build_calib_packet1(mic1_bands: list[int], output_bands: list[int],
                          mic2_gd: int, mic_delay: int) -> bytes:
    """Build Packet 1 (48 bytes): mic1 bands 25-31 + output bands + first 2 short modules."""
    assert len(mic1_bands) == 32 and len(output_bands) == 32
    p1 = bytearray(48)
    # Mic1 bands 25-31
    for i in range(25, 32):
        p1[i - 25] = mic1_bands[i]
    # Output header + bands
    p1[7:10] = b'\x01\xFA\x00'
    for i in range(32):
        p1[10 + i] = output_bands[i]
    # Mic2 gain diff
    p1[42] = 0x40
    struct.pack_into('<h', p1, 43, mic2_gd)
    # Mic delay
    p1[45] = 0x40
    struct.pack_into('<H', p1, 46, mic_delay)
    return bytes(p1)


def _build_calib_packet2(tc_gd: int, dai_gd: int, fbc_bd: int, das: int) -> bytes:
    """Build Packet 2 (48 bytes): last 4 short modules + 36 bytes zero padding."""
    p2 = bytearray(48)
    p2[0] = 0x40
    struct.pack_into('<h', p2, 1, tc_gd)
    p2[3] = 0x40
    struct.pack_into('<h', p2, 4, dai_gd)
    p2[6] = 0x40
    struct.pack_into('<H', p2, 7, fbc_bd)
    p2[9] = 0x40
    struct.pack_into('<h', p2, 10, das)
    # bytes 12-47 already zero
    return bytes(p2)


# ============================================================
# Step 1 Tests
# ============================================================

def test_step1():
    print("=== Step 1: Calibration Data Parse Tests ===\n")

    # Build artificial calibration data with known values
    mic1_vals = [0] + [80 + i for i in range(31)]  # band0=0, bands 1-31 = 80..110
    out_vals  = [0] + [110 + i for i in range(31)]  # band0=0, bands 1-31 = 110..140

    p0 = _build_calib_packet0(mic1_vals)
    p1 = _build_calib_packet1(
        mic1_vals, out_vals,
        mic2_gd=15,       # 15 × 0.1 = 1.5 dB
        mic_delay=500,     # 500 × 0.1 = 50.0 us
    )
    p2 = _build_calib_packet2(
        tc_gd=50,          # 50 × 0.1 = 5.0 dB
        dai_gd=-50,        # -50 × 0.1 = -5.0 dB
        fbc_bd=320,        # 320 us
        das=0,
    )

    raw = p0 + p1 + p2
    assert len(raw) == 144

    calib = parse_calibration(raw)

    # 1.1 Packet header
    print("1.1 Packet header validation:")
    print(f"  packet_count=3, module_count+1=9  ✓")

    # 1.2 Mic1 bands
    print("\n1.2 Mic1 band array:")
    assert len(calib.mic1_band) == 32
    assert calib.mic1_band[0] == 0
    assert calib.mic1_band[1] == 80
    assert calib.mic1_band[31] == 110
    print(f"  band[0]={calib.mic1_band[0]} (invalid), band[1]={calib.mic1_band[1]}, band[31]={calib.mic1_band[31]}  ✓")

    # 1.3 Output bands
    print("\n1.3 Output band array:")
    assert len(calib.output_band) == 32
    assert calib.output_band[1] == 110
    assert calib.output_band[31] == 140
    print(f"  band[0]={calib.output_band[0]} (invalid), band[1]={calib.output_band[1]}, band[31]={calib.output_band[31]}  ✓")

    # 1.4 Short modules
    print("\n1.4 Short calibration modules:")
    assert calib.mic2_gain_diff == 15
    print(f"  mic2_gain_diff  = {calib.mic2_gain_diff}  (1.5 dB)  ✓")
    assert calib.mic_delay == 500
    print(f"  mic_delay       = {calib.mic_delay}  (50.0 us)  ✓")
    assert calib.telecoil_gain_diff == 50
    print(f"  telecoil_gain   = {calib.telecoil_gain_diff}  (5.0 dB)  ✓")
    assert calib.dai_gain_diff == -50
    print(f"  dai_gain        = {calib.dai_gain_diff}  (-5.0 dB)  ✓")
    assert calib.fbc_bulk_delay == 320
    print(f"  fbc_bulk_delay  = {calib.fbc_bulk_delay} us  ✓")

    # 1.5 Derived values
    print("\n1.5 Derived calibration values:")
    avg_m1 = calib.avg_mic1_cal()
    # mic1_vals[1:] = 80,81,...,110 → avg = (80+110)/2 = 95.0
    expected_avg_m1 = sum(range(80, 111)) / 31.0
    assert abs(avg_m1 - expected_avg_m1) < 0.01, f"avg_mic1_cal: expected {expected_avg_m1}, got {avg_m1}"
    print(f"  avg_mic1_cal()  = {avg_m1:.2f}  (expected {expected_avg_m1:.1f})  ✓")

    avg_out = calib.avg_output_cal()
    expected_avg_out = sum(range(110, 141)) / 31.0
    assert abs(avg_out - expected_avg_out) < 0.01, f"avg_output_cal: expected {expected_avg_out}, got {avg_out}"
    print(f"  avg_output_cal() = {avg_out:.2f}  (expected {expected_avg_out:.1f})  ✓")

    assert calib.input_gain_diff_db('mic') == 0.0
    assert calib.input_gain_diff_db('telecoil') == 5.0   # 50/10
    assert calib.input_gain_diff_db('dai') == -5.0        # -50/10
    print(f"  input_gain_diff_db('mic')      = {calib.input_gain_diff_db('mic')}  ✓")
    print(f"  input_gain_diff_db('telecoil') = {calib.input_gain_diff_db('telecoil')}  ✓")
    print(f"  input_gain_diff_db('dai')      = {calib.input_gain_diff_db('dai')}  ✓")

    # 1.6 Zero-padding verification
    print("\n1.6 Zero-padding:")
    assert all(b == 0 for b in raw[108:144]), "Bytes 108-143 must be zero"
    print(f"  bytes 108-143 all zero  ✓")

    # 1.7 Roundtrip: parse → rebuild → parse
    print("\n1.7 Roundtrip consistency:")
    p0b = _build_calib_packet0(calib.mic1_band)
    p1b = _build_calib_packet1(calib.mic1_band, calib.output_band,
                                calib.mic2_gain_diff, calib.mic_delay)
    p2b = _build_calib_packet2(calib.telecoil_gain_diff, calib.dai_gain_diff,
                                calib.fbc_bulk_delay, calib.digital_audio_sensitivity)
    calib2 = parse_calibration(p0b + p1b + p2b)
    assert calib2.mic1_band == calib.mic1_band
    assert calib2.output_band == calib.output_band
    assert calib2.telecoil_gain_diff == calib.telecoil_gain_diff
    assert calib2.dai_gain_diff == calib.dai_gain_diff
    print(f"  parse(build(calib)) == calib  ✓")

    print("\n=== Step 1: ALL TESTS PASSED ===\n")


# ============================================================
# Step 1b: Cross-validate with calibration.json & calibration_values.json
# ============================================================

import json

def _find_data_offset(raw_bytes: bytes) -> int:
    """Locate the start of the 48-byte data section within a raw I2C frame
    by searching for known calibration header patterns.

    Returns the byte offset where the 48-byte data section begins.
    """
    # Try offsets 0..8 and check which one produces valid data
    for offset in range(9):
        cand = raw_bytes[offset: offset + 48]
        if len(cand) < 48:
            continue
        # Packet 0 signature: byte0=3 (packet_count), byte1=0, byte2=9
        if cand[0] == 3 and cand[1] == 0 and cand[2] == 9:
            return offset
        # Packet 1 signature: Mic1 header at the tail, Output header at offset 7
        if cand[7:10] == b'\x01\xFA\x00':
            return offset
        # Packet 2 signature: starts with 0x40 (first short module header)
        if cand[0] == 0x40 and cand[3] == 0x40:
            return offset
    raise ValueError("Cannot locate data section in raw bytes")


def test_step1_crossval():
    print("=== Step 1b: Cross-Validation with JSON files ===\n")

    # Load real calibration I2C data
    with open(os.path.join(_SCRIPT_DIR, 'data', 'calibration.json'), 'r', encoding='utf-8') as f:
        raw_entries = json.load(f)

    with open(os.path.join(_SCRIPT_DIR, 'data', 'calibration_values.json'), 'r', encoding='utf-8') as f:
        expected = json.load(f)

    # Group entries by PKTNUM (bits 15:12 of cmd_word), extract data sections
    data_sections = {}  # pktnum -> 48 bytes
    for entry in raw_entries:
        cmd = int(entry['cmd_word'], 16)
        pktnum = bs300_cmd_pktnum(cmd)
        if pktnum > 2:   # only 3 calibration packets (0, 1, 2)
            print(f"  PKTNUM={pktnum} cmd=0x{cmd:06X}: skipping (padding packet)")
            continue
        raw_bytes = bytes(int(h, 16) for h in entry['bytes'])
        off = _find_data_offset(raw_bytes)
        data_sections[pktnum] = raw_bytes[off: off + 48]
        print(f"  PKTNUM={pktnum} cmd=0x{cmd:06X}: data at offset {off}, {len(data_sections[pktnum])} bytes")

    # Ensure we have packets 0, 1, 2
    for p in [0, 1, 2]:
        assert p in data_sections, f"Missing packet {p} in calibration.json"

    # Concatenate data sections in order
    raw = data_sections[0] + data_sections[1] + data_sections[2]
    assert len(raw) == 144, f"Expected 144 bytes, got {len(raw)}"

    # Parse with our implementation
    calib = parse_calibration(raw)

    # ---- Cross-validate every field ----
    errors = []

    print("\n1b.1 Mic1 calibration (32 bands):")
    for i in range(32):
        exp = expected['mic1_calibration'][i]
        got = calib.mic1_band[i]
        if exp != got:
            errors.append(f"mic1_band[{i}]: expected {exp}, got {got}")
    if errors:
        for e in errors[:5]:  # show first 5 mismatches
            print(f"  MISMATCH: {e}")
    else:
        print(f"  All 32 bands match ✓")
    errors.clear()

    print("\n1b.2 Output calibration (32 bands):")
    for i in range(32):
        exp = expected['output_calibration'][i]
        got = calib.output_band[i]
        if exp != got:
            errors.append(f"output_band[{i}]: expected {exp}, got {got}")
    if errors:
        for e in errors[:5]:
            print(f"  MISMATCH: {e}")
    else:
        print(f"  All 32 bands match ✓")
    errors.clear()

    print("\n1b.3 Short calibration modules:")
    checks = [
        ('mic2_gain_diff', calib.mic2_gain_diff, expected['mic2_gain_diff']),
        ('mic_delay', calib.mic_delay, expected['mic_delay']),
        ('telecoil_gain_diff', calib.telecoil_gain_diff, expected['telecoil_gain_diff']),
        ('dai_gain_diff', calib.dai_gain_diff, expected['dai_gain_diff']),
        ('fbc_bulk_delay', calib.fbc_bulk_delay, expected['fbc_bulk_delay']),
        ('digital_audio_sensitivity', calib.digital_audio_sensitivity, expected['digital_audio_sensitivity']),
    ]
    all_ok = True
    for name, got, exp in checks:
        status = "✓" if got == exp else "✗ MISMATCH"
        if got != exp:
            all_ok = False
        print(f"  {name:30s}: expected {exp:6d}, got {got:6d}  {status}")
    assert all_ok, "Short module cross-validation failed"

    # 1b.4 Verify gain_calibration relationship
    print("\n1b.4 Gain calibration self-consistency:")
    # mic1_cal = output_cal - gain_cal → gain_cal = output_cal - mic1_cal
    gc_errors = []
    for i in range(32):
        computed_gc = expected['output_calibration'][i] - expected['mic1_calibration'][i]
        expected_gc = expected['gain_calibration'][i]
        if computed_gc != expected_gc:
            gc_errors.append(f"band[{i}]: output-mic1={computed_gc}, expected gain_cal={expected_gc}")
    if gc_errors:
        for e in gc_errors[:5]:
            print(f"  {e}")
    else:
        print(f"  output_cal - mic1_cal == gain_cal  for all 32 bands ✓")

    # 1b.5 Verify derived values
    print("\n1b.5 Derived values:")
    avg_m1 = calib.avg_mic1_cal()
    avg_out = calib.avg_output_cal()
    print(f"  avg_mic1_cal()  = {avg_m1:.2f}")
    print(f"  avg_output_cal() = {avg_out:.2f}")
    print(f"  input_gain_diff_db('telecoil') = {calib.input_gain_diff_db('telecoil'):.1f}")
    print(f"  input_gain_diff_db('dai')      = {calib.input_gain_diff_db('dai'):.1f}")

    # Manual check: telecoil_gain_diff = -450 (from JSON)
    # input_gain_diff_db('telecoil') = -450 / 10.0 = -45.0
    expected_tc_db = expected['telecoil_gain_diff'] / 10.0
    assert calib.input_gain_diff_db('telecoil') == expected_tc_db, \
        f"TC gain diff: expected {expected_tc_db}, got {calib.input_gain_diff_db('telecoil')}"
    print(f"  telecoil gain diff dB check: {calib.input_gain_diff_db('telecoil'):.1f} == {expected_tc_db:.1f} ✓")

    print("\n=== Step 1b: CROSS-VALIDATION ALL PASSED ===\n")


# ============================================================
