from dataclasses import dataclass
# Step 2: Program Burn Data Read (Flash Format Parse)
# ============================================================

class BitReader:
    """Read bits LSB-first from a byte buffer.
    Bit stream: within each byte, bit 0 first → bit 7 last;
    across bytes, earlier byte first.
    Global bit position P maps to byte P//8, bit P%8.
    """

    def __init__(self, data: bytes, byte_offset: int = 0, bit_offset: int = 0):
        self._data = data
        self._pos = byte_offset * 8 + bit_offset

    @property
    def pos(self) -> int:
        return self._pos

    def skip(self, n: int):
        self._pos += n

    def read(self, n: int) -> int:
        """Read n bits and return as integer (bit 0 = first read bit)."""
        result = 0
        for i in range(n):
            byte_idx = self._pos // 8
            bit_idx = self._pos % 8
            if byte_idx >= len(self._data):
                raise IndexError(f"BitReader overflow: pos={self._pos}, len={len(self._data)}")
            bit = (self._data[byte_idx] >> bit_idx) & 1
            result |= (bit << i)
            self._pos += 1
        return result


@dataclass
class WdrcChannelFlash:
    """One WDRC channel decoded from Flash bit-packed format (119 bit)."""
    frequency_idx: int    # 6-bit, channel frequency table index
    epd_at: int           # 7-bit, Table 2-2 index
    epd_rt: int           # 7-bit
    epd_r: int            # 7-bit, Table 2-3 index
    kp1_th: int           # 7-bit, = value_in_MT
    kp2_th: int           # 7-bit, = value_in_MT (≥ kp1_th), 0 if 1KP
    kp1_at: int           # 7-bit
    kp2_at: int           # 7-bit
    kp1_rt: int           # 7-bit
    kp2_rt: int           # 7-bit
    kp1_r: int            # 7-bit
    kp2_r: int            # 7-bit
    lmt_th: int           # 7-bit, = value_in_MT - 30
    lmt_at: int           # 7-bit
    lmt_rt: int           # 7-bit
    lmt_r: int            # 7-bit


@dataclass
class WdrcFlash:
    """WDRC module decoded from Flash format."""
    kneepoints_per_channel: int  # 0=1KP, 1=2KP
    output_limiting_sel: int     # 0=Disable, 1=Enable
    num_channels: int            # 1-16
    bin_gain: list[int]          # 32 × 7-bit int7, = 27 + value_in_MT
    channels: list[WdrcChannelFlash]


@dataclass
class VolumeFlash:
    """Volume and Beep module decoded from Flash format."""
    beep_level: int
    beep_frequency: int
    min_volume: int
    max_volume: int
    battery_flat_beep_level: int
    battery_flat_beep_frequency: int


@dataclass
class InputsFlash:
    """Inputs module decoded from Flash format."""
    input_type: str   # 'front_mic', 'rear_mic', 'telecoil', 'dai', 'mm_plus', 'ddm2', 'dual_mic'
    # MM Plus fields
    mic_mixing_ratio: int = 0    # = 50 + value_in_MT
    mm_type: int = 0             # 0=Telecoil, 1=DAI
    # DDM2 fields
    omni_threshold: int = 0
    open_ear_mode_sel: int = 0
    mode: int = 0                # 0=FDM, 1=ADM
    fixed_polar_pattern: int = 0
    apply_omni_threshold_to_fdm: int = 0
    cutoff_frequency: int = 0


@dataclass
class DfbcFlash:
    """DFBC module decoded from Flash format."""
    dfbc_mode: int  # 0x01, 0x03, 0x07, 0x09, 0x0B, 0x0F


@dataclass
class EnrChannelFlash:
    """One ENR channel decoded from Flash format (39 bit)."""
    frequency_idx: int  # 6-bit
    ma: int             # 5-bit, = value_in_MT
    snrth: int          # 5-bit, = value_in_MT
    nt: int             # 6-bit, = value_in_MT - 10
    unt: int            # 6-bit, = value_in_MT - 40
    etr: int            # 7-bit, = value_in_MT × 100
    nrr: int            # 4-bit, = value_in_MT × 10


@dataclass
class EnrFlash:
    """ENR module decoded from Flash format."""
    nfsf: int           # 4-bit, = value_in_MT - 1
    nhsf: int           # 4-bit
    nnsf: int           # 4-bit
    num_channels: int
    snasf: int          # 4-bit, = value_in_MT - 1
    channels: list[EnrChannelFlash]


@dataclass
class IssFlash:
    iss_threshold: int  # uint8, = value_in_MT


@dataclass
class WnrFlash:
    dual_mic_mode_sel: int
    suppression_strength_preset: int  # = value_in_MT


@dataclass
class AgcoFlash:
    attack_time: int    # uint12 ms, = value_in_MT
    release_time: int   # uint12 ms, = value_in_MT
    threshold: int      # uint8, = abs(value_in_MT)


@dataclass
class ProgramData:
    """Full Program x data decoded from Flash format."""
    wdrc: WdrcFlash
    volume: VolumeFlash
    inputs: InputsFlash
    dfbc: DfbcFlash | None = None
    enr: EnrFlash | None = None
    iss: IssFlash | None = None
    wnr: WnrFlash | None = None
    agco: AgcoFlash | None = None


# ---- Module command table ----
_MODULE_TABLE = {
    0x12: 'wdrc',
    0x07: 'volume',
    0x03: 'front_mic',
    0x04: 'rear_mic',
    0x05: 'telecoil',
    0x06: 'dai',
    0x17: 'mm_plus',
    0x1B: 'ddm2',
    0x1E: 'dual_mic',
    0x14: 'dfbc',
    0x1C: 'enr',
    0x21: 'noise_gen2',
    0x1D: 'iss',
    0x1F: 'wnr',
    0x26: 'acclimatization',
    0x23: 'agco',
}

# Channel frequency table (index → Hz)
_CH_FREQ_TABLE = [
    0, 125, 375, 625, 875, 1125, 1375, 1625,
    1875, 2125, 2375, 2625, 2875, 3125, 3375, 3625,
    3875, 4125, 4375, 4625, 4875, 5125, 5375, 5625,
    5875, 6125, 6375, 6625, 6875, 7125, 7375, 7625,
]


def _decode_wdrc_channel(br: BitReader) -> WdrcChannelFlash:
    """Decode one 119-bit WDRC channel from bit stream."""
    freq = br.read(6)
    epd_at = br.read(7)
    epd_rt = br.read(7)
    epd_r = br.read(7)
    assert br.read(2) == 0b10, f"WDRC P1 marker mismatch at bit {br.pos}"
    kp1_th = br.read(7)
    kp2_th = br.read(7)
    assert br.read(2) == 0b10, f"WDRC P2 marker mismatch at bit {br.pos}"
    kp1_at = br.read(7)
    kp2_at = br.read(7)
    assert br.read(2) == 0b10, f"WDRC P3 marker mismatch at bit {br.pos}"
    kp1_rt = br.read(7)
    kp2_rt = br.read(7)
    assert br.read(2) == 0b10, f"WDRC P4 marker mismatch at bit {br.pos}"
    kp1_r = br.read(7)
    kp2_r = br.read(7)
    lmt_th = br.read(7)
    lmt_at = br.read(7)
    lmt_rt = br.read(7)
    lmt_r = br.read(7)
    return WdrcChannelFlash(
        frequency_idx=freq, epd_at=epd_at, epd_rt=epd_rt, epd_r=epd_r,
        kp1_th=kp1_th, kp2_th=kp2_th,
        kp1_at=kp1_at, kp2_at=kp2_at,
        kp1_rt=kp1_rt, kp2_rt=kp2_rt,
        kp1_r=kp1_r, kp2_r=kp2_r,
        lmt_th=lmt_th, lmt_at=lmt_at, lmt_rt=lmt_rt, lmt_r=lmt_r,
    )


def _decode_wdrc(raw: bytes) -> WdrcFlash:
    """Decode WDRC module from raw data bytes (bit-packed)."""
    br = BitReader(raw)
    # Byte 0 header
    _ = br.read(1)     # bit0: fixed 0b1
    limiter = br.read(1)
    kp_mode = br.read(1)
    _ = br.read(5)     # bits 7:3: 0

    # Skip byte1.bit0 = 0b1 marker
    br.skip(1)  # B1 bit0

    # 32 band bin_gain, each 7-bit
    bin_gains = [br.read(7) for _ in range(32)]
    # bin_gain = 27 + value_in_MT, so value_in_MT = bin_gain - 27

    # Byte 29: num_channels is at B29[5:1]
    # The bit reader is now at B29 bit6 (after reading 32×7 bits)
    # Let's read num_channels from B29[5:1]
    # Actually, we need to read from the current position.
    # After bin_gains, br.pos is at bit 8*1+1 + 32*7 = 9 + 224 = 233
    # B29 = byte 29. B29 bits 0-7 = positions 232-239.
    # B29 bit 0 → position 232 (this is band_32[6])
    # B29 bits 5:1 → positions 233-237
    # So at position 233, we need to read 5 bits for num_channels.
    # But wait, br just read band_32 (7 bits from positions 226-232), so br.pos = 233. ✓

    num_ch = br.read(5)       # B29[5:1]
    br.skip(1)                # B29[6]: reserved (待确认)
    # B29[7] is ch1_freq[0] — read by _decode_wdrc_channel as first bit of 6-bit freq

    # Remaining bits for per-channel
    channels = [_decode_wdrc_channel(br) for _ in range(num_ch)]

    return WdrcFlash(
        kneepoints_per_channel=kp_mode,
        output_limiting_sel=limiter,
        num_channels=num_ch,
        bin_gain=bin_gains,
        channels=channels,
    )


def _decode_volume(raw: bytes) -> VolumeFlash:
    """Decode Volume and Beep module (9 bytes)."""
    return VolumeFlash(
        beep_level=raw[0],
        beep_frequency=raw[1] | (raw[2] << 8),
        min_volume=raw[3] - 256 if raw[3] > 127 else raw[3],
        max_volume=raw[4] - 256 if raw[4] > 127 else raw[4],
        battery_flat_beep_level=raw[5],
        battery_flat_beep_frequency=raw[6] | (raw[7] << 8),
    )


def _decode_inputs(cmd_data: int, raw: bytes) -> InputsFlash:
    """Decode Inputs module."""
    type_map = {0x03: 'front_mic', 0x04: 'rear_mic', 0x05: 'telecoil',
                0x06: 'dai', 0x17: 'mm_plus', 0x1B: 'ddm2', 0x1E: 'dual_mic'}
    inp = InputsFlash(input_type=type_map.get(cmd_data, 'unknown'))
    if cmd_data == 0x17 and len(raw) >= 3:  # MM Plus
        inp.mic_mixing_ratio = raw[0]       # = 50 + value_in_MT
        inp.mm_type = raw[1]
    elif cmd_data == 0x1B and len(raw) >= 6:  # DDM2
        inp.omni_threshold = raw[1]
        inp.open_ear_mode_sel = (raw[2] >> 6) & 1
        inp.mode = (raw[2] >> 5) & 1
        inp.apply_omni_threshold_to_fdm = (raw[2] >> 3) & 1
        inp.fixed_polar_pattern = raw[2] & 0x7
        inp.cutoff_frequency = raw[3] | (raw[4] << 8) | (raw[5] << 16)
    return inp


def _decode_dfbc(raw: bytes) -> DfbcFlash:
    return DfbcFlash(dfbc_mode=raw[0])


def _decode_enr(raw: bytes) -> EnrFlash:
    """Decode ENR module (bit-packed)."""
    br = BitReader(raw)
    nfsf = br.read(4)
    nhsf = br.read(4)
    nnsf = br.read(4)
    num_ch_low = br.read(4)
    num_ch_high = br.read(2)
    num_ch = num_ch_low | (num_ch_high << 4)
    # num_ch = ENR_channel_count - 1

    channels = []
    for _ in range(num_ch + 1):
        ch_freq = br.read(6)
        ch_ma = br.read(5)
        ch_snrth = br.read(5)
        ch_nt = br.read(6)
        ch_unt = br.read(6)
        ch_etr = br.read(7)
        ch_nrr = br.read(4)
        channels.append(EnrChannelFlash(
            frequency_idx=ch_freq, ma=ch_ma, snrth=ch_snrth,
            nt=ch_nt, unt=ch_unt, etr=ch_etr, nrr=ch_nrr,
        ))

    snasf = br.read(4)

    return EnrFlash(
        nfsf=nfsf, nhsf=nhsf, nnsf=nnsf,
        num_channels=num_ch + 1,
        snasf=snasf,
        channels=channels,
    )


def _decode_iss(raw: bytes) -> IssFlash:
    return IssFlash(iss_threshold=raw[0])


def _decode_wnr(raw: bytes) -> WnrFlash:
    return WnrFlash(
        dual_mic_mode_sel=raw[0],
        suppression_strength_preset=raw[1],
    )


def _decode_agco(raw: bytes) -> AgcoFlash:
    attack = raw[0] | ((raw[1] & 0x0F) << 8)      # uint12
    release = ((raw[1] & 0xF0) >> 4) | (raw[2] << 4)  # uint12
    return AgcoFlash(
        attack_time=attack,
        release_time=release,
        threshold=raw[3],
    )


def parse_program_data(raw: bytes) -> ProgramData:
    """Parse concatenated Program Burn packets into ProgramData.

    `raw` is the concatenation of all packet data sections (each 48 bytes),
    forming the complete program data segment.

    Returns a ProgramData with all decoded modules.
    """
    assert len(raw) >= 8, "Program data too short"

    # Parse header
    packet_count = raw[0]
    assert raw[1:4] == b'\x80\x00' + bytes([raw[3]]), "Module command info mismatch"
    num_cmds = raw[3] - 1  # N+1 → N

    # Parse module command list
    cmd_entries = []  # (cmd_data, length_in_words)
    pos = 4
    for _ in range(num_cmds):
        cmd_data = raw[pos]
        length_words = raw[pos + 2]
        cmd_entries.append((cmd_data, length_words))
        pos += 3
    assert raw[pos:pos + 2] == b'\xFB\x00', f"Expected 0xFB 0x00 at byte {pos}, got {raw[pos:pos+2].hex(' ')}"
    pos += 2  # skip FB 00

    # Parse module data in order
    result = {}
    for cmd_data, length_words in cmd_entries:
        module_type = _MODULE_TABLE.get(cmd_data, 'unknown')
        length_bytes = length_words * 3
        data = raw[pos:pos + length_bytes]
        pos += length_bytes

        if module_type == 'wdrc':
            result['wdrc'] = _decode_wdrc(data)
        elif module_type == 'volume':
            result['volume'] = _decode_volume(data)
        elif module_type in ('front_mic', 'rear_mic', 'telecoil', 'dai', 'mm_plus', 'ddm2', 'dual_mic'):
            result['inputs'] = _decode_inputs(cmd_data, data)
        elif module_type == 'dfbc':
            result['dfbc'] = _decode_dfbc(data)
        elif module_type == 'enr':
            result['enr'] = _decode_enr(data)
        elif module_type == 'iss':
            result['iss'] = _decode_iss(data)
        elif module_type == 'wnr':
            result['wnr'] = _decode_wnr(data)
        elif module_type == 'agco':
            result['agco'] = _decode_agco(data)
        # noise_gen2 and acclimatization are parsed on demand later

    return ProgramData(
        wdrc=result['wdrc'],
        volume=result['volume'],
        inputs=result.get('inputs', InputsFlash(input_type='front_mic')),
        dfbc=result.get('dfbc'),
        enr=result.get('enr'),
        iss=result.get('iss'),
        wnr=result.get('wnr'),
        agco=result.get('agco'),
    )


# ============================================================
