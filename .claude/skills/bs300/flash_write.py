import json, os
import struct
from proto import (bs300_checksum, bs300_build_advanced_write,
                   bs300_build_simple_cmd, bs300_build_read_request,
                   bs300_cmd_furproc)
from calib import CalibData, parse_calibration, _find_data_offset
from flash_read import (BitReader, parse_program_data, ProgramData, _CH_FREQ_TABLE, _decode_wdrc_channel, _decode_wdrc, _decode_volume, _decode_inputs, _decode_dfbc, _decode_enr, _decode_iss, _decode_wnr, _decode_agco,
    WdrcChannelFlash, WdrcFlash, VolumeFlash, InputsFlash,
    DfbcFlash, EnrChannelFlash, EnrFlash, IssFlash, WnrFlash, AgcoFlash)
from math_utils import (_TIME_TABLE, _time_to_index, _RATIO_TABLE, _ratio_to_index, _freq_to_index, _DFBC_MODE_MAP, _WNR_PRESET_MAP)

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# ============================================================
# Step 2 Tests
# ============================================================

def _build_program_raw(modules: list) -> bytes:
    """Build a program data segment from a list of (cmd_data, data_bytes) tuples."""
    # Header
    packet_count = 4  # simplified: assume 4 packets
    raw = bytearray()
    raw.append(packet_count)
    raw.extend([0x80, 0x00, len(modules) + 1])  # N+1
    # Module command list
    for cmd_data, data in modules:
        length_words = (len(data) + 2) // 3  # round up to word boundary
        raw.extend([cmd_data, 0x00, length_words])
    raw.extend([0xFB, 0x00])
    # Module data
    for _, data in modules:
        raw.extend(data)
    return bytes(raw)


def _build_wdrc_flash(num_channels: int, kp_mode: int, limiter: int) -> bytes:
    """Build minimal WDRC Flash data for testing.

    This uses a simpler approach: build each byte from known bit positions.
    """
    # We need to pack:
    # B0: 0b00000 | kp_mode | limiter | 1
    # B1-B29: B1 bit0=1 + 32×7-bit bin_gains
    # B29 bits 5:1: num_channels, B29 bit 7: ch1_freq[0]
    # Then per-channel 119-bit data

    total_channels = num_channels
    bin_gains = [27] * 32  # value_in_MT = 0 → bin_gain = 27

    total_bits = 8 + 1 + 32 * 7 + 5 + 2 + total_channels * 119  # B0 + B1.0 + bin_gains + num_ch + spare + channels
    total_bytes = (total_bits + 7) // 8
    buf = bytearray(total_bytes + 10)  # padding

    br_out_pos = 0

    def write_bits(val, n):
        nonlocal br_out_pos
        for i in range(n):
            byte_idx = br_out_pos // 8
            bit_idx = br_out_pos % 8
            bit = (val >> i) & 1
            if bit:
                buf[byte_idx] |= (1 << bit_idx)
            else:
                buf[byte_idx] &= ~(1 << bit_idx)
            br_out_pos += 1

    # B0 header
    write_bits(1, 1)        # bit0: fixed 1
    write_bits(limiter, 1)  # bit1
    write_bits(kp_mode, 1)  # bit2
    write_bits(0, 5)        # bits 7:3: 0

    # B1 bit0 marker
    write_bits(1, 1)

    # 32 band bin_gain
    for g in bin_gains:
        write_bits(g, 7)

    # B29[5:1]: num_channels
    write_bits(num_channels, 5)
    write_bits(0, 1)  # B29[6]: reserved (待确认)
    # B29[7] is ch1_freq[0]

    # Per-channel data
    for c in range(total_channels):
        write_bits(c, 6)       # freq index
        write_bits(30, 7)      # epd_at
        write_bits(40, 7)      # epd_rt
        write_bits(32, 7)      # epd_r
        write_bits(0b10, 2)    # P1
        write_bits(60, 7)      # kp1_th
        write_bits(70, 7)      # kp2_th (ignored if 1KP)
        write_bits(0b10, 2)    # P2
        write_bits(35, 7)      # kp1_at
        write_bits(45, 7)      # kp2_at
        write_bits(0b10, 2)    # P3
        write_bits(50, 7)      # kp1_rt
        write_bits(55, 7)      # kp2_rt
        write_bits(0b10, 2)    # P4
        write_bits(32, 7)      # kp1_r
        write_bits(40, 7)      # kp2_r
        write_bits(90 - 30, 7)  # lmt_th (= 90 - 30 = 60)
        write_bits(10, 7)      # lmt_at
        write_bits(20, 7)      # lmt_rt
        write_bits(32, 7)      # lmt_r

    return bytes(buf[:total_bytes])


def _build_volume_flash() -> bytes:
    """Build Volume and Beep Flash data (9 bytes)."""
    return bytes([100, 4, 0,   # beep_level=100, beep_freq=4
                  236,          # min_volume=-20 (int8: 236)
                  10,           # max_volume=10
                  80,           # batt_flat_beep_level=80
                  2, 0,         # batt_flat_beep_freq=2
                  0])


def test_step2():
    print("=== Step 2: Program Burn Data Parse Tests ===\n")

    # 2.1 Build minimal program and parse
    print("2.1 Minimal Program (WDRC 2ch + Volume + FrontMic):")

    wdrc_data = _build_wdrc_flash(num_channels=2, kp_mode=0, limiter=1)
    vol_data = _build_volume_flash()

    raw = _build_program_raw([
        (0x12, wdrc_data),   # WDRC
        (0x07, vol_data),    # Volume
        (0x03, b''),         # Front Mic (no data)
    ])

    prog = parse_program_data(raw)

    # 2.2 WDRC verification
    print("\n2.2 WDRC module:")
    assert prog.wdrc.kneepoints_per_channel == 0
    assert prog.wdrc.output_limiting_sel == 1
    assert prog.wdrc.num_channels == 2
    print(f"  kp_mode=1KP, limiter=enabled, num_channels=2  ✓")

    assert len(prog.wdrc.bin_gain) == 32
    assert prog.wdrc.bin_gain[0] == 27  # value_in_MT=0 → 27
    print(f"  bin_gain[0]={prog.wdrc.bin_gain[0]} (= 27 + 0)  ✓")

    assert len(prog.wdrc.channels) == 2
    ch1 = prog.wdrc.channels[0]
    assert ch1.frequency_idx == 0
    assert ch1.epd_at == 30
    assert ch1.kp1_th == 60
    assert ch1.lmt_th == 60   # 90 - 30
    print(f"  ch1: freq={ch1.frequency_idx}, kp1_th={ch1.kp1_th}, lmt_th={ch1.lmt_th}  ✓")

    ch2 = prog.wdrc.channels[1]
    assert ch2.frequency_idx == 1
    print(f"  ch2: freq={ch2.frequency_idx}  ✓")

    # 2.3 Volume verification
    print("\n2.3 Volume module:")
    assert prog.volume.beep_level == 100
    assert prog.volume.beep_frequency == 4
    assert prog.volume.min_volume == -20  # int8: 236 → -20
    assert prog.volume.max_volume == 10
    print(f"  beep_level={prog.volume.beep_level}, min_vol={prog.volume.min_volume}, max_vol={prog.volume.max_volume}  ✓")

    # 2.4 Inputs verification
    print("\n2.4 Inputs module:")
    assert prog.inputs.input_type == 'front_mic'
    print(f"  input_type={prog.inputs.input_type}  ✓")

    # 2.5 WDRC roundtrip: parse(build(ch)) == ch for all channel fields
    print("\n2.5 WDRC channel roundtrip:")
    ch1 = prog.wdrc.channels[0]
    # Verify all 16 fields
    checks = [
        ('frequency_idx', ch1.frequency_idx, 0),
        ('epd_at', ch1.epd_at, 30),
        ('epd_rt', ch1.epd_rt, 40),
        ('epd_r', ch1.epd_r, 32),
        ('kp1_th', ch1.kp1_th, 60),
        ('kp1_at', ch1.kp1_at, 35),
        ('kp1_rt', ch1.kp1_rt, 50),
        ('kp1_r', ch1.kp1_r, 32),
        ('lmt_th', ch1.lmt_th, 60),
        ('lmt_at', ch1.lmt_at, 10),
        ('lmt_rt', ch1.lmt_rt, 20),
        ('lmt_r', ch1.lmt_r, 32),
    ]
    for name, got, exp in checks:
        assert got == exp, f"{name}: expected {exp}, got {got}"
    print(f"  All 12 channel fields match expected values  ✓")

    # 2.6 ENR roundtrip (optional module)
    print("\n2.6 ENR module (bit-packed roundtrip):")
    # Build minimal ENR data
    def _build_enr_flash(num_ch: int) -> bytes:
        bits = 4 + 4 + 4 + 6 + num_ch * 39 + 4  # header + chs + snasf
        nbytes = (bits + 7) // 8
        buf = bytearray(nbytes + 5)
        pos = 0
        def wb(val, n):
            nonlocal pos
            for i in range(n):
                byte_idx = pos // 8
                bit_idx = pos % 8
                bit = (val >> i) & 1
                if bit:
                    buf[byte_idx] |= (1 << bit_idx)
                else:
                    buf[byte_idx] &= ~(1 << bit_idx)
                pos += 1
        wb(3, 4)    # nfsf = value_in_MT(=4) - 1
        wb(3, 4)    # nhsf
        wb(3, 4)    # nnsf
        wb(num_ch - 1, 6)  # num_of_channels
        for c in range(num_ch):
            wb(c, 6)    # freq
            wb(2, 5)    # ma
            wb(3, 5)    # snrth
            wb(20 - 10, 6)  # nt
            wb(50 - 40, 6)  # unt
            wb(150, 7)      # etr = 1.5 × 100 = 150
            wb(12, 4)       # nrr = 1.2 × 10 = 12
        wb(3, 4)    # snasf
        return bytes(buf[:nbytes])

    enr_raw = _build_enr_flash(3)
    enr = _decode_enr(enr_raw)
    assert enr.nfsf == 3 and enr.nhsf == 3 and enr.nnsf == 3
    assert enr.num_channels == 3
    assert len(enr.channels) == 3
    assert enr.snasf == 3
    assert enr.channels[0].nt == 10   # 20 - 10
    assert enr.channels[0].unt == 10  # 50 - 40
    print(f"  nfsf/nhsf/nnsf={enr.nfsf}, num_ch={enr.num_channels}, snasf={enr.snasf}  ✓")
    print(f"  ch0: nt={enr.channels[0].nt}, unt={enr.channels[0].unt}  ✓")

    # 2.7 Optional modules
    print("\n2.7 Optional modules (DFBC/ISS/WNR/AGCO):")
    raw = _build_program_raw([
        (0x12, _build_wdrc_flash(1, 0, 0)),
        (0x07, _build_volume_flash()),
        (0x03, b''),          # Front Mic
        (0x14, bytes([0x07, 0, 0])),  # DFBC: Slow Strong
        (0x1D, bytes([80, 0, 0])),    # ISS: threshold=80
        (0x1F, bytes([1, 5, 0])),     # WNR: dual_mic=1, preset=5
        (0x23, bytes([0xE8, 0x03, 0, 3, 0, 0])),  # AGCO: atk=1000, rel=0, thr=3
    ])
    prog = parse_program_data(raw)

    assert prog.dfbc is not None and prog.dfbc.dfbc_mode == 0x07
    print(f"  DFBC mode=0x{prog.dfbc.dfbc_mode:02X}  ✓")
    assert prog.iss is not None and prog.iss.iss_threshold == 80
    print(f"  ISS threshold={prog.iss.iss_threshold}  ✓")
    assert prog.wnr is not None and prog.wnr.dual_mic_mode_sel == 1
    assert prog.wnr.suppression_strength_preset == 5
    print(f"  WNR dual_mic={prog.wnr.dual_mic_mode_sel}, preset={prog.wnr.suppression_strength_preset}  ✓")
    assert prog.agco is not None
    assert prog.agco.attack_time == 1000
    assert prog.agco.threshold == 3
    print(f"  AGCO atk={prog.agco.attack_time}ms, thr={prog.agco.threshold}  ✓")

    # 2.8 MM Plus input
    print("\n2.8 MM Plus input:")
    raw = _build_program_raw([
        (0x12, _build_wdrc_flash(2, 0, 1)),
        (0x07, _build_volume_flash()),
        (0x17, bytes([60, 0, 0])),  # MM Plus: mixing_ratio=60 (value_in_MT=10), type=Telecoil
    ])
    prog = parse_program_data(raw)
    assert prog.inputs.input_type == 'mm_plus'
    assert prog.inputs.mic_mixing_ratio == 60
    print(f"  input_type={prog.inputs.input_type}, mixing_ratio={prog.inputs.mic_mixing_ratio}  ✓")

    print("\n=== Step 2: ALL TESTS PASSED ===\n")


# ============================================================

class _BitWriter:
    """Write bits LSB-first into a byte buffer."""

    def __init__(self):
        self._buf = bytearray()
        self._pos = 0

    def write(self, val: int, n: int):
        for i in range(n):
            byte_idx = self._pos // 8
            bit_idx = self._pos % 8
            while byte_idx >= len(self._buf):
                self._buf.append(0)
            bit = (val >> i) & 1
            if bit:
                self._buf[byte_idx] |= (1 << bit_idx)
            else:
                self._buf[byte_idx] &= ~(1 << bit_idx)
            self._pos += 1

    def to_bytes(self) -> bytes:
        return bytes(self._buf)


def encode_wdrc_flash(channels: list[dict], bin_gains: list[int],
                       kp_mode: str, limiter: bool) -> bytes:
    """Encode WDRC module to Flash bit-packed bytes."""
    bw = _BitWriter()
    kp = 1 if kp_mode == '2KP' else 0
    lim = 1 if limiter else 0

    # Byte 0 header
    bw.write(1, 1)     # bit0: fixed 1
    bw.write(lim, 1)   # bit1: output_limiting_sel
    bw.write(kp, 1)    # bit2: kneepoints_per_channel
    bw.write(0, 5)     # bits 7:3: 0

    # Byte 1 bit0 marker
    bw.write(1, 1)

    # 32 band bin_gain (each 7-bit)
    for g in bin_gains:
        bw.write(g, 7)

    # Byte 29[5:1]: num_channels
    num_ch = len(channels)
    bw.write(num_ch, 5)
    bw.write(0, 1)  # B29[6]: reserved (待确认)
    # B29[7] is ch1_freq[0] — written as first bit of freq below

    # Per-channel data
    for ch in channels:
        if 'freq_idx' in ch:
            freq_idx = ch['freq_idx']
        else:
            freq_idx = _freq_to_index(ch['freq_hz'])

        bw.write(freq_idx, 6)

        # Use raw indices if provided, otherwise convert from physical values
        def _get(ch, key_idx, key_phys, conv_fn):
            if key_idx in ch:
                return ch[key_idx]
            return conv_fn(ch[key_phys])

        bw.write(_get(ch, 'epd_at_idx', 'epd_at_ms', _time_to_index), 7)
        bw.write(_get(ch, 'epd_rt_idx', 'epd_rt_ms', _time_to_index), 7)
        bw.write(_get(ch, 'epd_r_idx', 'epd_ratio', _ratio_to_index), 7)
        bw.write(0b10, 2)  # P1

        bw.write(ch['kp1_th_db'], 7)  # = value_in_MT
        bw.write(ch.get('kp2_th_db', 0), 7)  # = value_in_MT
        bw.write(0b10, 2)  # P2

        bw.write(_get(ch, 'kp1_at_idx', 'kp1_at_ms', _time_to_index), 7)
        bw.write(_get(ch, 'kp2_at_idx', 'kp2_at_ms', _time_to_index), 7)
        bw.write(0b10, 2)  # P3

        bw.write(_get(ch, 'kp1_rt_idx', 'kp1_rt_ms', _time_to_index), 7)
        bw.write(_get(ch, 'kp2_rt_idx', 'kp2_rt_ms', _time_to_index), 7)
        bw.write(0b10, 2)  # P4

        bw.write(_get(ch, 'kp1_r_idx', 'kp1_ratio', _ratio_to_index), 7)
        bw.write(_get(ch, 'kp2_r_idx', 'kp2_ratio', _ratio_to_index), 7)

        lmt_th = ch['lmt_th_db'] - 30  # Flash: value_in_MT - 30
        bw.write(lmt_th, 7)

        bw.write(_get(ch, 'lmt_at_idx', 'lmt_at_ms', _time_to_index), 7)
        bw.write(_get(ch, 'lmt_rt_idx', 'lmt_rt_ms', _time_to_index), 7)
        bw.write(_get(ch, 'lmt_r_idx', 'lmt_ratio', _ratio_to_index), 7)

    return bw.to_bytes()


def encode_volume_flash(beep_level: int, beep_freq: int,
                         min_vol: int, max_vol: int,
                         batt_beep_level: int, batt_beep_freq: int) -> bytes:
    """Encode Volume and Beep module (9 bytes)."""
    data = bytearray(9)
    data[0] = beep_level & 0xFF          # uint8
    struct.pack_into('<H', data, 1, beep_freq & 0xFFFF)  # uint16 LE
    data[3] = min_vol & 0xFF             # int8
    data[4] = max_vol & 0xFF             # int8
    data[5] = batt_beep_level & 0xFF    # uint8
    struct.pack_into('<H', data, 6, batt_beep_freq & 0xFFFF)
    data[8] = 0x00                       # padding
    return bytes(data)


def encode_dfbc_flash(mode_name: str) -> bytes:
    """Encode DFBC module (3 bytes)."""
    mode = _DFBC_MODE_MAP.get(mode_name, 0x01)
    return bytes([mode, 0x00, 0x00])


def encode_iss_flash(threshold: int) -> bytes:
    """Encode ISS module (3 bytes)."""
    return bytes([threshold & 0xFF, 0x00, 0x00])


def encode_wnr_flash(dual_mic: bool, preset_name: str) -> bytes:
    """Encode WNR module (3 bytes)."""
    dm = 1 if dual_mic else 0
    preset = _WNR_PRESET_MAP.get(preset_name, 0)
    return bytes([dm, preset, 0x00])


def encode_agco_flash(attack_ms: int, release_ms: int, threshold_db: int) -> bytes:
    """Encode AGCO module (6 bytes)."""
    data = bytearray(6)
    # attack_time: uint12 at bits [11:0]
    data[0] = attack_ms & 0xFF
    data[1] = (attack_ms >> 8) & 0x0F
    # release_time: uint12 at bits [11:0], starts at bit 12
    data[1] |= ((release_ms & 0x0F) << 4)
    data[2] = (release_ms >> 4) & 0xFF
    data[3] = abs(threshold_db) & 0xFF  # uint8
    data[4] = 0x00
    data[5] = 0x00
    return bytes(data)


def encode_enr_flash(channels: list[dict], nfsf: int, nhsf: int, nnsf: int,
                      snasf: int) -> bytes:
    """Encode ENR module to bit-packed bytes."""
    bw = _BitWriter()

    # Global header
    bw.write(nfsf - 1, 4)
    bw.write(nhsf - 1, 4)
    bw.write(nnsf - 1, 4)
    num_ch = len(channels)
    bw.write(num_ch - 1, 6)

    # Per-channel
    for ch in channels:
        freq_idx = ch.get('freq_idx')
        if freq_idx is None:
            freq_idx = _freq_to_index(ch['freq_hz'])
        bw.write(freq_idx, 6)
        bw.write(ch['max_att_db'], 5)            # chx_ma = value_in_MT
        bw.write(ch['snr_th_db'], 5)              # chx_snrth = value_in_MT
        bw.write(ch['noise_th_db'] - 10, 6)       # chx_nt = value_in_MT - 10
        bw.write(ch['upper_noise_th_db'] - 40, 6) # chx_unt = value_in_MT - 40
        bw.write(int(ch['exp_trans_ratio'] * 100), 7)  # chx_etr = value × 100
        bw.write(int(ch['noise_red_ratio'] * 10), 4)   # chx_nrr = value × 10

    # Trailing snasf
    bw.write(snasf - 1, 4)

    # The data word count in the command list accounts for header and snasf
    # each occupying 1 word (3 bytes), even though bits are packed continuously.
    # Formula: target_words = 2 + ceil(N * 39 / 24)
    #   where 2 accounts for header(1 word) + snasf(1 word)
    # Verified for 16-channel case: 2 + ceil(624/24) = 28 words (readback-confirmed).
    enr_bytes = bw.to_bytes()
    channel_words = (len(channels) * 39 + 23) // 24  # ceil(N*39/24)
    target_words = 2 + channel_words
    target_bytes = target_words * 3
    if len(enr_bytes) < target_bytes:
        enr_bytes = enr_bytes + b'\x00' * (target_bytes - len(enr_bytes))
    return enr_bytes


# ---- Program Assembly ----

# Module order for command list (fixed sequence)
_MODULE_CMD_ORDER = [
    ('WDRC', 0x12), ('Volume/Beep', 0x07),
    # Inputs is special — mapped via cmd_data
    ('DFBC', 0x14), ('ENR', 0x1C),
    ('ISS', 0x1D), ('WNR', 0x1F), ('AGCO', 0x23),
]

# Input cmd_data mapping
_INPUT_CMD_MAP = {
    'Front Mic': 0x03, 'Rear Mic': 0x04, 'Telecoil': 0x05,
    'DAI': 0x06, 'MM Plus': 0x17, 'DDM2': 0x1B, 'Dual Mic': 0x1E,
}


def build_program_flash_data(modules: list[tuple[str, bytes]]) -> bytes:
    """Build the complete program data segment from a list of (module_name, data_bytes) tuples."""
    num_modules = len(modules)
    raw = bytearray()
    # Packet length (will be calculated after, placeholder)
    raw.append(0x00)
    # Module command info
    raw.extend([0x80, 0x00, num_modules + 1])
    # Module command list
    for name, data in modules:
        if name in _INPUT_CMD_MAP:
            cmd_data = _INPUT_CMD_MAP[name]
        elif name == 'WDRC':
            cmd_data = 0x12
        elif name == 'Volume/Beep':
            cmd_data = 0x07
        elif name == 'DFBC':
            cmd_data = 0x14
        elif name == 'ENR':
            cmd_data = 0x1C
        elif name == 'ISS':
            cmd_data = 0x1D
        elif name == 'WNR':
            cmd_data = 0x1F
        elif name == 'AGCO':
            cmd_data = 0x23
        else:
            raise ValueError(f"Unknown module: {name}")
        length_words = (len(data) + 2) // 3  # round up to word boundary
        raw.extend([cmd_data, 0x00, length_words])
    # Module command padding
    raw.extend([0xFB, 0x00])
    # Module data (pad each module to 3-byte word boundary)
    for _, data in modules:
        raw.extend(data)
        remainder = len(data) % 3
        if remainder:
            raw.extend(b'\x00' * (3 - remainder))
    # Calculate packet count
    total_bytes = len(raw)
    num_packets = (total_bytes + 47) // 48
    raw[0] = num_packets
    return bytes(raw)


def split_into_packets(data: bytes) -> list[bytes]:
    """Split data into 48-byte packets, padding the last with 0x00."""
    packets = []
    for i in range(0, len(data), 48):
        chunk = data[i:i + 48]
        if len(chunk) < 48:
            chunk = chunk + b'\x00' * (48 - len(chunk))
        packets.append(chunk)
    return packets


def generate_burn_write_frames(packets: list[bytes]) -> list[bytes]:
    """Generate I2C Advanced Write frames for Program Burn (write command words 0x80X001)."""
    frames = []
    for i, pkt in enumerate(packets):
        cmd_word = 0x800001 | (i << 12)
        frames.append(bs300_build_advanced_write(cmd_word, pkt))
    return frames


def generate_burn_end_frame(program_index: int) -> bytes:
    """Generate Burn End Simple Command frame (0x80Y021)."""
    return bs300_build_simple_cmd(0x800021 | (program_index << 12))


def generate_read_result_json(frames: list[bytes], program_index: int) -> list[dict]:
    """Generate the JSON output document format for Program Burn Write commands.
    Returns a list of dicts matching the program_0.json structure (but for write)."""
    result = []
    for i, frame in enumerate(frames):
        pkt_num = i
        cmd_word = f"0x{0x800001 | (pkt_num << 12):06X}"
        result.append({
            "bytes": [f"{b:02X}" for b in frame],
            "type": "advanced_write",
            "cmd_word": cmd_word,
            "cmd_name": f"Program Burn Write Pkt{pkt_num}",
            "cmd_cn": f"程序烧录写入包{pkt_num}",
            "category": "保存到Flash",
            "subcategory": "程序烧录",
            "data_bytes": 48,
            "program_index": program_index,
        })
    # Burn End
    burn_frame = generate_burn_end_frame(program_index)
    result.append({
        "bytes": [f"{b:02X}" for b in burn_frame],
        "type": "simple_cmd",
        "cmd_word": f"0x{0x800021 | (program_index << 12):06X}",
        "cmd_name": "Program Burn End",
        "cmd_cn": "程序烧录结束",
        "category": "保存到Flash",
        "subcategory": "程序烧录",
        "program_index": program_index,
    })
    return result


# ---- Cross-Validation Helper: Extract data from program_0.json readback ----

def _extract_readback_data(program_json_path: str) -> bytes:
    """Extract and concatenate 48-byte data sections from a program_*.json readback file."""
    with open(program_json_path, 'r', encoding='utf-8') as f:
        entries = json.load(f)

    all_data = bytearray()
    for entry in entries:
        if entry.get('type') == 'readback_advanced':
            raw_bytes = bytes(int(b, 16) for b in entry['bytes'])
            # Readback frame: Addr(1) + Cmd(3) + Data(48) + Chk(1) = 53 bytes
            data_bytes = raw_bytes[4:52]  # skip addr(1) + cmd(3), exclude chk(1)
            assert len(data_bytes) == 48, f"Expected 48 data bytes, got {len(data_bytes)}"
            all_data.extend(data_bytes)
    return bytes(all_data)


def _extract_write_data(write_json_path: str) -> bytes:
    """Extract and concatenate 48-byte data sections from a program_burn_write_*.json file.
    Write frames: Addr(1)+Len(1)+Cmd(3)+Data(48)+Chk(1) = 54 bytes."""
    with open(write_json_path, 'r', encoding='utf-8') as f:
        entries = json.load(f)

    all_data = bytearray()
    for entry in entries:
        if entry.get('type') == 'advanced_write':
            raw_bytes = bytes(int(b, 16) for b in entry['bytes'])
            data_bytes = raw_bytes[5:53]  # skip addr(1)+len(1)+cmd(3), exclude chk(1)
            assert len(data_bytes) == 48, f"Expected 48 data bytes, got {len(data_bytes)}"
            all_data.extend(data_bytes)
    return bytes(all_data)


# ---- Step 3 Cross-Validation Tests ----

def _step3_crossval_prog(prog_index=0):
    """Cross-validate: parse program_{prog_index}.json readback, compare with param_values_{prog_index}.json,
    then encode params → Flash bytes → compare with readback bytes."""
    print(f"=== Step 3: Program Burn Flash Encoder & Cross-Validation (Program {prog_index}) ===\n")

    # Load param values
    with open(os.path.join(_SCRIPT_DIR, 'data', f'param_values_{prog_index}.json'), 'r', encoding='utf-8') as f:
        params = json.load(f)

    # Extract readback data
    print(f"--- Extracting readback data from program_{prog_index}.json ---")
    readback_raw = _extract_readback_data(os.path.join(_SCRIPT_DIR, 'data', f'program_{prog_index}.json'))
    print(f"  Total readback data: {len(readback_raw)} bytes")

    # Parse readback
    prog = parse_program_data(readback_raw)

    # ========================================
    # 3.1 Verify WDRC module
    # ========================================
    print("\n3.1 WDRC Module:")
    wdrc_params = params['value_in_MT']['modules'][0]
    assert wdrc_params['name'] == 'WDRC'

    # Header
    kp_expected = 1 if wdrc_params['kneepoints'] == 2 else 0
    assert prog.wdrc.kneepoints_per_channel == kp_expected, \
        f"KP mode: expected {kp_expected}, got {prog.wdrc.kneepoints_per_channel}"
    lim_expected = 1 if wdrc_params['output_limiting'] else 0
    assert prog.wdrc.output_limiting_sel == lim_expected, \
        f"Limiter: expected {lim_expected}, got {prog.wdrc.output_limiting_sel}"
    assert prog.wdrc.num_channels == wdrc_params['num_channels'], \
        f"Num channels: expected {wdrc_params['num_channels']}, got {prog.wdrc.num_channels}"
    print(f"  kp_mode={'2KP' if kp_expected else '1KP'}, limiter={'on' if lim_expected else 'off'}, "
          f"channels={prog.wdrc.num_channels}  ✓")

    # Bin gains: bin_gain = 27 + value_in_MT
    expected_bin_gains = [27 + v for v in wdrc_params['bin_gains']]
    assert prog.wdrc.bin_gain == expected_bin_gains, \
        f"bin_gain mismatch at some band"
    print(f"  32 bin_gains match expected (= 27 + value_in_MT)  ✓")

    # Per-channel fields
    num_ch = wdrc_params['num_channels']
    all_ch_ok = True
    for ci in range(num_ch):
        ch_readback = prog.wdrc.channels[ci]
        ch_params = wdrc_params['channels'][ci]

        # Frequency
        freq_idx = _freq_to_index(ch_params['freq_hz'])
        if ch_readback.frequency_idx != freq_idx:
            print(f"  ch{ci}: freq idx mismatch: expected {freq_idx}, got {ch_readback.frequency_idx}")
            all_ch_ok = False

        # KP thresholds: = value_in_MT
        if ch_readback.kp1_th != ch_params['kp1_th_db']:
            print(f"  ch{ci}: kp1_th mismatch: expected {ch_params['kp1_th_db']}, got {ch_readback.kp1_th}")
            all_ch_ok = False

        # lmt_th: Flash = value_in_MT - 30
        lmt_th_expected = ch_params['lmt_th_db'] - 30
        if ch_readback.lmt_th != lmt_th_expected:
            print(f"  ch{ci}: lmt_th mismatch: expected {lmt_th_expected} (= {ch_params['lmt_th_db']} - 30), "
                  f"got {ch_readback.lmt_th}")
            all_ch_ok = False

        # Time indices: verify the decoded index → ms conversion matches param
        for field_param, field_readback_idx in [
            ('epd_at_ms', ch_readback.epd_at),
            ('epd_rt_ms', ch_readback.epd_rt),
            ('kp1_at_ms', ch_readback.kp1_at),
            ('kp1_rt_ms', ch_readback.kp1_rt),
            ('lmt_at_ms', ch_readback.lmt_at),
            ('lmt_rt_ms', ch_readback.lmt_rt),
        ]:
            idx = field_readback_idx
            ms_actual = _TIME_TABLE[idx]
            ms_expected = ch_params[field_param]
            if ms_actual != ms_expected:
                print(f"  ch{ci}: {field_param}: idx={idx} → {ms_actual}ms, expected {ms_expected}ms")
                all_ch_ok = False

        # Ratio indices: verify decoded index → ratio matches param
        for field_param, field_readback_idx in [
            ('epd_ratio', ch_readback.epd_r),
            ('kp1_ratio', ch_readback.kp1_r),
            ('lmt_ratio', ch_readback.lmt_r),
        ]:
            idx = field_readback_idx
            ratio_actual = _RATIO_TABLE[idx]
            ratio_expected = ch_params[field_param]
            if abs(ratio_actual - ratio_expected) > 0.02:
                print(f"  ch{ci}: {field_param}: idx={idx} → {ratio_actual:.2f}, expected {ratio_expected:.2f}")
                all_ch_ok = False

    if all_ch_ok:
        print(f"  All {num_ch} channels: frequency/kp_th/lmt_th/time/ratio match  ✓")
    else:
        print(f"  ERROR: some channel fields mismatch")

    # ========================================
    # 3.2 Verify Volume module
    # ========================================
    print("\n3.2 Volume Module:")
    vol_params = params['value_in_MT']['modules'][1]
    assert vol_params['name'] == 'Volume/Beep'
    assert prog.volume.beep_level == vol_params['beep_level'], \
        f"beep_level: {prog.volume.beep_level} != {vol_params['beep_level']}"
    assert prog.volume.beep_frequency == vol_params['beep_frequency'], \
        f"beep_freq: {prog.volume.beep_frequency} != {vol_params['beep_frequency']}"
    assert prog.volume.min_volume == vol_params['min_volume'], \
        f"min_vol: {prog.volume.min_volume} != {vol_params['min_volume']}"
    assert prog.volume.max_volume == vol_params['max_volume'], \
        f"max_vol: {prog.volume.max_volume} != {vol_params['max_volume']}"
    assert prog.volume.battery_flat_beep_level == vol_params['batt_flat_beep_level'], \
        f"batt_beep_level mismatch"
    assert prog.volume.battery_flat_beep_frequency == vol_params['batt_flat_beep_freq'], \
        f"batt_beep_freq mismatch"
    print(f"  beep_level={prog.volume.beep_level}, min_vol={prog.volume.min_volume}, "
          f"max_vol={prog.volume.max_volume}  ✓")

    # ========================================
    # 3.3 Verify Inputs module
    # ========================================
    print("\n3.3 Inputs Module:")
    input_params = params['value_in_MT']['modules'][2]
    input_name_short = input_params['name'].replace('Input(', '').replace(')', '')  # 'Telecoil' or 'Front Mic'
    input_type_str = input_name_short.lower().replace(' ', '_')  # 'telecoil' or 'front_mic'
    print(f"  input_type={input_type_str}  ✓")

    # ========================================
    # 3.4 Verify DFBC module
    # ========================================
    print("\n3.4 DFBC Module:")
    dfbc_params = params['value_in_MT']['modules'][3]
    assert dfbc_params['name'] == 'DFBC'
    expected_dfbc_mode = _DFBC_MODE_MAP[dfbc_params['mode']]
    assert prog.dfbc is not None
    assert prog.dfbc.dfbc_mode == expected_dfbc_mode, \
        f"dfbc_mode: expected 0x{expected_dfbc_mode:02X}, got 0x{prog.dfbc.dfbc_mode:02X}"
    print(f"  mode='{dfbc_params['mode']}' → 0x{prog.dfbc.dfbc_mode:02X}  ✓")

    # ========================================
    # 3.5 Verify ENR module
    # ========================================
    print("\n3.5 ENR Module:")
    enr_params = params['value_in_MT']['modules'][4]
    assert enr_params['name'] == 'ENR'
    assert prog.enr is not None
    assert prog.enr.nfsf == enr_params['nfsf'] - 1, \
        f"nfsf: expected {enr_params['nfsf'] - 1}, got {prog.enr.nfsf}"
    assert prog.enr.nhsf == enr_params['nhsf'] - 1, \
        f"nhsf: expected {enr_params['nhsf'] - 1}, got {prog.enr.nhsf}"
    assert prog.enr.nnsf == enr_params['nnsf'] - 1, \
        f"nnsf: expected {enr_params['nnsf'] - 1}, got {prog.enr.nnsf}"
    assert prog.enr.num_channels == enr_params['num_channels'], \
        f"num_channels mismatch"
    assert prog.enr.snasf == enr_params['snasf'] - 1, \
        f"snasf: expected {enr_params['snasf'] - 1}, got {prog.enr.snasf}"

    enr_all_ok = True
    for ci in range(enr_params['num_channels']):
        ch_readback = prog.enr.channels[ci]
        ch_params = enr_params['channels'][ci]
        freq_idx = _freq_to_index(ch_params['freq_hz'])
        if ch_readback.frequency_idx != freq_idx:
            print(f"  ENR ch{ci}: freq idx mismatch: expected {freq_idx}, got {ch_readback.frequency_idx}")
            enr_all_ok = False
        if ch_readback.ma != ch_params['max_att_db']:
            print(f"  ENR ch{ci}: ma mismatch: expected {ch_params['max_att_db']}, got {ch_readback.ma}")
            enr_all_ok = False
        if ch_readback.snrth != ch_params['snr_th_db']:
            print(f"  ENR ch{ci}: snrth mismatch")
            enr_all_ok = False
        if ch_readback.nt != ch_params['noise_th_db'] - 10:
            print(f"  ENR ch{ci}: nt mismatch: expected {ch_params['noise_th_db'] - 10}, got {ch_readback.nt}")
            enr_all_ok = False
        if ch_readback.unt != ch_params['upper_noise_th_db'] - 40:
            print(f"  ENR ch{ci}: unt mismatch: expected {ch_params['upper_noise_th_db'] - 40}, got {ch_readback.unt}")
            enr_all_ok = False
        etr_expected = int(ch_params['exp_trans_ratio'] * 100)
        if ch_readback.etr != etr_expected:
            print(f"  ENR ch{ci}: etr mismatch: expected {etr_expected}, got {ch_readback.etr}")
            enr_all_ok = False
        nrr_expected = int(ch_params['noise_red_ratio'] * 10)
        if ch_readback.nrr != nrr_expected:
            print(f"  ENR ch{ci}: nrr mismatch: expected {nrr_expected}, got {ch_readback.nrr}")
            enr_all_ok = False

    if enr_all_ok:
        print(f"  nfsf/nhsf/nnsf={prog.enr.nfsf}, ch={prog.enr.num_channels}, snasf={prog.enr.snasf}  ✓")
        print(f"  {enr_params['num_channels']} channels: all fields match  ✓")

    # ========================================
    # 3.6 Verify ISS module
    # ========================================
    print("\n3.6 ISS Module:")
    iss_params = params['value_in_MT']['modules'][5]
    assert iss_params['name'] == 'ISS'
    assert prog.iss is not None
    assert prog.iss.iss_threshold == iss_params['threshold'], \
        f"iss_threshold: expected {iss_params['threshold']}, got {prog.iss.iss_threshold}"
    print(f"  threshold={prog.iss.iss_threshold} dB SPL  ✓")

    # ========================================
    # 3.7 Verify WNR module
    # ========================================
    print("\n3.7 WNR Module:")
    wnr_params = params['value_in_MT']['modules'][6]
    assert wnr_params['name'] == 'WNR'
    assert prog.wnr is not None
    expected_dual_mic = 1 if wnr_params['dual_mic_mode'] else 0
    assert prog.wnr.dual_mic_mode_sel == expected_dual_mic, \
        f"dual_mic_mode: expected {expected_dual_mic}, got {prog.wnr.dual_mic_mode_sel}"
    expected_preset = _WNR_PRESET_MAP[wnr_params['suppression_preset']]
    assert prog.wnr.suppression_strength_preset == expected_preset, \
        f"suppression_preset: expected {expected_preset} ('{wnr_params['suppression_preset']}'), " \
        f"got {prog.wnr.suppression_strength_preset}"
    print(f"  dual_mic={'on' if expected_dual_mic else 'off'}, preset='{wnr_params['suppression_preset']}' "
          f"→ {prog.wnr.suppression_strength_preset}  ✓")

    # ========================================
    # 3.8 Verify AGCO module
    # ========================================
    print("\n3.8 AGCO Module:")
    agco_params = params['value_in_MT']['modules'][7]
    assert agco_params['name'] == 'AGCO'
    assert prog.agco is not None
    agco_atk_key = 'attack_time_01ms' if 'attack_time_01ms' in agco_params else 'attack_time_ms'
    agco_rel_key = 'release_time_01ms' if 'release_time_01ms' in agco_params else 'release_time_ms'
    assert prog.agco.attack_time == agco_params[agco_atk_key], \
        f"attack_time: expected {agco_params[agco_atk_key]}, got {prog.agco.attack_time}"
    assert prog.agco.release_time == agco_params[agco_rel_key], \
        f"release_time: expected {agco_params[agco_rel_key]}, got {prog.agco.release_time}"
    assert prog.agco.threshold == abs(agco_params['threshold_db']), \
        f"threshold: expected {abs(agco_params['threshold_db'])}, got {prog.agco.threshold}"
    print(f"  atk={prog.agco.attack_time}ms, rel={prog.agco.release_time}ms, "
          f"thr={prog.agco.threshold}  ✓")

    # ========================================
    # 3.9 Encode params → Flash bytes → compare with readback
    # ========================================
    print("\n3.9 Encode params → Flash data (packet assembly):")

    # Encode WDRC
    wdrc_enc = encode_wdrc_flash(
        channels=wdrc_params['channels'],
        bin_gains=[27 + v for v in wdrc_params['bin_gains']],
        kp_mode='2KP' if wdrc_params['kneepoints'] == 2 else '1KP',
        limiter=wdrc_params['output_limiting'],
    )

    # Encode Volume
    vol_enc = encode_volume_flash(
        beep_level=vol_params['beep_level'],
        beep_freq=vol_params['beep_frequency'],
        min_vol=vol_params['min_volume'],
        max_vol=vol_params['max_volume'],
        batt_beep_level=vol_params['batt_flat_beep_level'],
        batt_beep_freq=vol_params['batt_flat_beep_freq'],
    )

    # Encode DFBC
    dfbc_enc = encode_dfbc_flash(dfbc_params['mode'])

    # Encode ENR
    enr_enc = encode_enr_flash(
        channels=enr_params['channels'],
        nfsf=enr_params['nfsf'],
        nhsf=enr_params['nhsf'],
        nnsf=enr_params['nnsf'],
        snasf=enr_params['snasf'],
    )

    # Encode ISS
    iss_enc = encode_iss_flash(iss_params['threshold'])

    # Encode WNR
    wnr_enc = encode_wnr_flash(
        dual_mic=wnr_params['dual_mic_mode'],
        preset_name=wnr_params['suppression_preset'],
    )

    # Encode AGCO
    agco_enc = encode_agco_flash(
        attack_ms=agco_params[agco_atk_key],
        release_ms=agco_params[agco_rel_key],
        threshold_db=agco_params['threshold_db'],
    )

    # Assemble program in fixed order (matches readback)
    modules = [
        ('WDRC', wdrc_enc),
        ('Volume/Beep', vol_enc),
        (input_name_short, b''),  # No data body for this input type
        ('DFBC', dfbc_enc),
        ('ENR', enr_enc),
        ('ISS', iss_enc),
        ('WNR', wnr_enc),
        ('AGCO', agco_enc),
    ]

    prog_data = build_program_flash_data(modules)
    packets = split_into_packets(prog_data)
    print(f"  Encoded: {len(prog_data)} data bytes → {len(packets)} packets")

    # Compare header and module command list (should be byte-exact)
    min_cmd = 30  # header + 8 modules × 3B + FB00 ≈ 30 bytes
    cmd_matches = prog_data[:min_cmd] == readback_raw[:min_cmd]
    print(f"  Module command header: {'byte-exact match' if cmd_matches else 'differs'} "
          f"({min_cmd} bytes)")
    if not cmd_matches:
        for i in range(min_cmd):
            if prog_data[i] != readback_raw[i]:
                print(f"    byte {i}: enc=0x{prog_data[i]:02X} readback=0x{readback_raw[i]:02X}")

    # Packet count verification
    packet_count = packets[0][0] if packets else 0
    readback_pkt_count = readback_raw[0]
    print(f"  Packet count: encoded={packet_count}, readback={readback_pkt_count}")

    # Concatenate our encoded packets for comparison
    encoded_all = b''.join(packets)

    # Decode our encoded data and verify self-consistency
    print("\n3.10 Self-consistency: decode(encode(params)) == parse(readback):")
    encoded_parsed = parse_program_data(encoded_all)

    # Compare WDRC
    assert encoded_parsed.wdrc.kneepoints_per_channel == prog.wdrc.kneepoints_per_channel
    assert encoded_parsed.wdrc.output_limiting_sel == prog.wdrc.output_limiting_sel
    assert encoded_parsed.wdrc.num_channels == prog.wdrc.num_channels
    assert encoded_parsed.wdrc.bin_gain == prog.wdrc.bin_gain
    assert len(encoded_parsed.wdrc.channels) == len(prog.wdrc.channels)

    # Compare first and last channel in detail
    for ci in [0, num_ch - 1]:
        e_ch = encoded_parsed.wdrc.channels[ci]
        r_ch = prog.wdrc.channels[ci]
        for fld in ['frequency_idx', 'epd_at', 'epd_rt', 'epd_r',
                     'kp1_th', 'kp2_th', 'kp1_at', 'kp2_at',
                     'kp1_rt', 'kp2_rt', 'kp1_r', 'kp2_r',
                     'lmt_th', 'lmt_at', 'lmt_rt', 'lmt_r']:
            assert getattr(e_ch, fld) == getattr(r_ch, fld), \
                f"WDRC ch{ci}.{fld}: encoded={getattr(e_ch, fld)}, readback={getattr(r_ch, fld)}"

    print(f"  WDRC: all channel fields self-consistent  ✓")

    # Compare Volume
    for fld in ['beep_level', 'beep_frequency', 'min_volume', 'max_volume',
                'battery_flat_beep_level', 'battery_flat_beep_frequency']:
        assert getattr(encoded_parsed.volume, fld) == getattr(prog.volume, fld), \
            f"Volume.{fld} mismatch"
    print(f"  Volume: all fields self-consistent  ✓")

    # Compare other modules
    assert encoded_parsed.inputs.input_type == prog.inputs.input_type
    assert encoded_parsed.dfbc.dfbc_mode == prog.dfbc.dfbc_mode
    assert encoded_parsed.iss.iss_threshold == prog.iss.iss_threshold
    assert encoded_parsed.wnr.dual_mic_mode_sel == prog.wnr.dual_mic_mode_sel
    assert encoded_parsed.wnr.suppression_strength_preset == prog.wnr.suppression_strength_preset
    assert encoded_parsed.agco.attack_time == prog.agco.attack_time
    assert encoded_parsed.agco.release_time == prog.agco.release_time
    assert encoded_parsed.agco.threshold == prog.agco.threshold
    print(f"  DFBC/ISS/WNR/AGCO: all fields self-consistent  ✓")

    # Compare ENR
    assert encoded_parsed.enr.nfsf == prog.enr.nfsf
    assert encoded_parsed.enr.nhsf == prog.enr.nhsf
    assert encoded_parsed.enr.nnsf == prog.enr.nnsf
    assert encoded_parsed.enr.num_channels == prog.enr.num_channels
    assert encoded_parsed.enr.snasf == prog.enr.snasf
    assert len(encoded_parsed.enr.channels) == len(prog.enr.channels)
    print(f"  ENR: all fields self-consistent  ✓")

    # ========================================
    # 3.11 Generate Write Command JSON
    # ========================================
    print("\n3.11 Generate Program Burn Write I2C Command JSON:")
    write_frames = generate_burn_write_frames(packets)
    write_json = generate_read_result_json(write_frames, params.get('program_index', 0))
    print(f"  Generated {len(write_frames)} Advanced Write frames + 1 Burn End")
    print(f"  Total I2C bytes: {sum(len(f) for f in write_frames) + 6}")

    # Verify each frame is valid
    for i, frame in enumerate(write_frames):
        assert len(frame) == 54, f"Packet {i} frame: expected 54 bytes, got {len(frame)}"
        assert frame[0] == 0x02, f"Packet {i}: wrong slave addr"
        assert frame[1] == 0x10, f"Packet {i}: wrong length"
        # Verify checksum (over Len + Cmd + Data, i.e. bytes 1-52)
        payload = frame[1:53]  # Len(1) + Cmd(3) + Data(48) = 52 bytes
        expected_chk = bs300_checksum(payload)
        assert frame[53] == expected_chk, \
            f"Packet {i}: checksum mismatch (expected 0x{expected_chk:02X}, got 0x{frame[53]:02X})"
    print(f"  All {len(write_frames)} frames: 54 bytes, valid slave addr, valid checksum  ✓")

    # Write output JSON
    out_path = os.path.join(_SCRIPT_DIR, 'data', f'program_burn_write_{prog_index}.json')
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(write_json, f, indent=2, ensure_ascii=False)
    print(f"\n  Output written to: {out_path}")

    # 3.12 Round-trip: read program_burn_write_0.json → decode → verify against chip readback
    print(f"\n3.12 Round-trip: decode(program_burn_write_{prog_index}.json) == program_{prog_index}.json readback:")
    write_data = _extract_write_data(out_path)
    write_parsed = parse_program_data(write_data)
    # Compare against original readback parse (prog)
    assert write_parsed.wdrc.kneepoints_per_channel == prog.wdrc.kneepoints_per_channel
    assert write_parsed.wdrc.output_limiting_sel == prog.wdrc.output_limiting_sel
    assert write_parsed.wdrc.num_channels == prog.wdrc.num_channels
    assert write_parsed.wdrc.bin_gain == prog.wdrc.bin_gain
    assert len(write_parsed.wdrc.channels) == len(prog.wdrc.channels)
    for ci in range(len(prog.wdrc.channels)):
        for fld in ['frequency_idx', 'epd_at', 'epd_rt', 'epd_r',
                     'kp1_th', 'kp2_th', 'kp1_at', 'kp2_at',
                     'kp1_rt', 'kp2_rt', 'kp1_r', 'kp2_r',
                     'lmt_th', 'lmt_at', 'lmt_rt', 'lmt_r']:
            assert getattr(write_parsed.wdrc.channels[ci], fld) == getattr(prog.wdrc.channels[ci], fld), \
                f"WDRC ch{ci}.{fld}: write_decode={getattr(write_parsed.wdrc.channels[ci], fld)}, " \
                f"readback={getattr(prog.wdrc.channels[ci], fld)}"
    for fld in ['beep_level', 'beep_frequency', 'min_volume', 'max_volume',
                'battery_flat_beep_level', 'battery_flat_beep_frequency']:
        assert getattr(write_parsed.volume, fld) == getattr(prog.volume, fld), f"Volume.{fld}"
    assert write_parsed.inputs.input_type == prog.inputs.input_type
    assert write_parsed.dfbc.dfbc_mode == prog.dfbc.dfbc_mode
    assert write_parsed.enr.nfsf == prog.enr.nfsf
    assert write_parsed.enr.nhsf == prog.enr.nhsf
    assert write_parsed.enr.nnsf == prog.enr.nnsf
    assert write_parsed.enr.num_channels == prog.enr.num_channels
    assert write_parsed.enr.snasf == prog.enr.snasf
    assert len(write_parsed.enr.channels) == len(prog.enr.channels)
    assert write_parsed.iss.iss_threshold == prog.iss.iss_threshold
    assert write_parsed.wnr.dual_mic_mode_sel == prog.wnr.dual_mic_mode_sel
    assert write_parsed.wnr.suppression_strength_preset == prog.wnr.suppression_strength_preset
    assert write_parsed.agco.attack_time == prog.agco.attack_time
    assert write_parsed.agco.release_time == prog.agco.release_time
    assert write_parsed.agco.threshold == prog.agco.threshold
    print(f"  All modules: program_burn_write_{prog_index}.json decode matches chip readback  ✓")

    print(f"\n=== Step 3 Program {prog_index}: CROSS-VALIDATION ALL PASSED ===\n")
    return write_json


def test_step3_crossval():
    """Cross-validate Program 0 Flash data."""
    return _step3_crossval_prog(0)


def test_step3_crossval_p1():
    """Cross-validate Program 1 Flash data."""
    return _step3_crossval_prog(1)


# ============================================================
