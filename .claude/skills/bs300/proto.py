import os
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


# Step 0: Protocol Foundation Layer
# ============================================================

def bs300_checksum(payload: bytes) -> int:
    """Calculate checksum over Length + Command + Data sections.
    sum = Σ(Length + Command + Data bytes)
    checksum = 0xFF - (sum & 0xFF)
    """
    s = sum(payload)
    return 0xFF - (s & 0xFF)


def bs300_build_simple_cmd(cmd_word: int) -> bytes:
    """Build a Simple Command frame (6 bytes total incl slave addr).
    Format: [Addr(0x02)] Len(0x00) Cmd_L Cmd_M Cmd_H Chk
    """
    cmd_bytes = bytes([cmd_word & 0xFF, (cmd_word >> 8) & 0xFF, (cmd_word >> 16) & 0xFF])
    payload = bytes([0x00]) + cmd_bytes  # Length + Command
    chk = bs300_checksum(payload)
    return bytes([0x02]) + payload + bytes([chk])


def bs300_build_read_request(length_data: int) -> bytes:
    """Build a Read Request frame (3 bytes total incl slave addr).
    length_data: 0x00 for status query, 0x10 for data read.
    Format: [Addr(0x02)] Len Chk
    Len byte: bit7=1 (R/W-REQ), bits[6:0]=length_data
    """
    length_byte = 0x80 | length_data
    chk = bs300_checksum(bytes([length_byte]))
    return bytes([0x02, length_byte, chk])


def bs300_build_advanced_write(cmd_word: int, data: bytes) -> bytes:
    """Build an Advanced Write frame (54 bytes total incl slave addr).
    Format: [Addr(0x02)] Len(0x10) Cmd_L Cmd_M Cmd_H Data(48B) Chk
    """
    assert len(data) == 48, f"Data section must be 48 bytes, got {len(data)}"
    cmd_bytes = bytes([cmd_word & 0xFF, (cmd_word >> 8) & 0xFF, (cmd_word >> 16) & 0xFF])
    payload = bytes([0x10]) + cmd_bytes + data
    chk = bs300_checksum(payload)
    return bytes([0x02]) + payload + bytes([chk])


def bs300_parse_response(data: bytes):
    """Parse an I2C read response.
    Returns (cmd_word, data_section).
    - 4-byte response (status query): data_section is empty bytes
    - 52-byte response (data read): 48-byte data_section
    Raises ValueError on unexpected length.
    """
    cmd_word = data[0] | (data[1] << 8) | (data[2] << 16)
    if len(data) == 4:
        return cmd_word, b''
    elif len(data) == 52:
        return cmd_word, data[3:51]
    else:
        raise ValueError(f"Unexpected response length: {len(data)}")


# ---- 24-bit Word Read/Write (little-endian, within 48-byte buffer) ----

def bs300_get_word(data: bytearray, n: int) -> int:
    """Read the n-th 24-bit word from a 48-byte buffer (little-endian).
    n ranges 0..15, each word occupies 3 bytes at offset n*3.
    """
    off = n * 3
    return data[off] | (data[off + 1] << 8) | (data[off + 2] << 16)


def bs300_set_word(data: bytearray, n: int, value: int):
    """Write the n-th 24-bit word into a 48-byte buffer (little-endian).
    Only the lower 24 bits of `value` are used.
    """
    off = n * 3
    data[off] = value & 0xFF
    data[off + 1] = (value >> 8) & 0xFF
    data[off + 2] = (value >> 16) & 0xFF


# ---- Command Word Field Extraction ----

def bs300_cmd_furproc(cmd_word: int) -> int:
    """Extract FURPROC (bit 23): 0 = ready, 1 = needs further processing."""
    return (cmd_word >> 23) & 1


def bs300_cmd_pktnum(cmd_word: int) -> int:
    """Extract PKTNUM (bits 15:12): packet sequence number 0-15."""
    return (cmd_word >> 12) & 0xF


def bs300_cmd_rdwrtbn(cmd_word: int) -> int:
    """Extract RDWRTBN (bit 4): 0 = EEPROM burn, 1 = RAM read/write."""
    return (cmd_word >> 4) & 1


# ============================================================
# Step 0 Tests — verify against handbook reference data
# ============================================================

def test_step0():
    print("=== Step 0: Protocol Foundation Tests ===\n")

    # 0.1 Checksum (handbook lines 144-163)
    print("0.1 Checksum:")
    buf = bytes([0x10, 0x12, 0x10, 0x80, 0xD8, 0xF5, 0x03, 0xD8, 0xF5, 0x03] + [0] * 42)
    chk = bs300_checksum(buf)
    assert chk == 0xAD, f"Expected 0xAD, got 0x{chk:02X}"
    # 1106 = 0x452, last byte = 0x52, 0xFF - 0x52 = 0xAD
    print(f"  checksum([0x10,0x12,...,42x0x00]) = 0x{chk:02X}  ✓")

    # 0.2 Frame Building — Simple Commands
    print("\n0.2 Frame Building:")

    mute_frame = bs300_build_simple_cmd(0x800000)
    assert mute_frame == bytes([0x02, 0x00, 0x00, 0x00, 0x80, 0x7F]), \
        f"Mute mismatch: got {mute_frame.hex(' ')}"
    print(f"  Mute 0x800000      → {mute_frame.hex(' ')}  ✓")

    prepcal_frame = bs300_build_simple_cmd(0x800051)
    assert prepcal_frame == bytes([0x02, 0x00, 0x51, 0x00, 0x80, 0x2E]), \
        f"PrepCal mismatch: got {prepcal_frame.hex(' ')}"
    print(f"  PrepCal 0x800051   → {prepcal_frame.hex(' ')}  ✓")

    rr0 = bs300_build_read_request(0x00)
    assert rr0 == bytes([0x02, 0x80, 0x7F]), \
        f"ReadRequest(0) mismatch: got {rr0.hex(' ')}"
    print(f"  ReadRequest(0x00)  → {rr0.hex(' ')}  ✓")

    rr16 = bs300_build_read_request(0x10)
    assert rr16 == bytes([0x02, 0x90, 0x6F]), \
        f"ReadRequest(16) mismatch: got {rr16.hex(' ')}"
    print(f"  ReadRequest(0x10)  → {rr16.hex(' ')}  ✓")

    # 0.3 24-bit Word Read/Write
    print("\n0.3 24-bit Word Read/Write (little-endian):")
    data = bytearray(48)
    bs300_set_word(data, 0, 0x123456)
    assert data[0] == 0x56 and data[1] == 0x34 and data[2] == 0x12, \
        f"set_word: byte-level check failed: [{data[0]:02X} {data[1]:02X} {data[2]:02X}]"
    word = bs300_get_word(data, 0)
    assert word == 0x123456, f"get_word: expected 0x123456, got 0x{word:06X}"
    print(f"  set(0, 0x123456)  → [{data[0]:02X} {data[1]:02X} {data[2]:02X}]")
    print(f"  get(0)            → 0x{word:06X}  ✓")

    # 0.4 Command Word Field Extraction
    print("\n0.4 Command Word Field Extraction:")
    assert bs300_cmd_furproc(0x800000) == 1
    assert bs300_cmd_furproc(0x000000) == 0
    print(f"  FURPROC(0x800000) = {bs300_cmd_furproc(0x800000)}  (bit23=1)  ✓")
    print(f"  FURPROC(0x000000) = {bs300_cmd_furproc(0x000000)}  (bit23=0)  ✓")

    assert bs300_cmd_pktnum(0x801001) == 1
    assert bs300_cmd_pktnum(0x800001) == 0
    print(f"  PKTNUM(0x801001)  = {bs300_cmd_pktnum(0x801001)}  (bits15:12=1)  ✓")
    print(f"  PKTNUM(0x800001)  = {bs300_cmd_pktnum(0x800001)}  (bits15:12=0)  ✓")

    # 0x51 → binary 0101_0001, bit4 = 1
    assert bs300_cmd_rdwrtbn(0x800051) == 1
    # 0x01 → binary 0000_0001, bit4 = 0
    assert bs300_cmd_rdwrtbn(0x800001) == 0
    print(f"  RDWRTBN(0x800051) = {bs300_cmd_rdwrtbn(0x800051)}  (bit4=1, RAM)  ✓")
    print(f"  RDWRTBN(0x800001) = {bs300_cmd_rdwrtbn(0x800001)}  (bit4=0, EEPROM)  ✓")

    # 0.5 Advanced Write frame
    print("\n0.5 Advanced Write Frame:")
    test_data = bytearray(48)
    aw_frame = bs300_build_advanced_write(0x800001, bytes(test_data))
    assert len(aw_frame) == 54, f"Advanced Write: expected 54 bytes, got {len(aw_frame)}"
    assert aw_frame[0] == 0x02 and aw_frame[1] == 0x10
    # checksum: payload = [0x10, 0x01, 0x00, 0x80, 48×0x00], sum=0x91 → chk=0x6E
    assert aw_frame[-1] == 0x6E, f"AdvWrite checksum: expected 0x6E, got 0x{aw_frame[-1]:02X}"
    print(f"  AdvWrite(0x800001, 48×0x00): {len(aw_frame)} bytes, chk=0x{aw_frame[-1]:02X}  ✓")

    # 0.6 Response Parsing
    print("\n0.6 Response Parsing:")
    # 4-byte status response
    status = bytes([0x00, 0x00, 0x00, 0x7F])
    cmd, dat = bs300_parse_response(status)
    assert cmd == 0x000000 and dat == b''
    print(f"  Parse 4B status:   cmd=0x{cmd:06X}, data=''  ✓")

    # 52-byte data response
    data_resp = bytes([0x00, 0x80, 0x00]) + bytes(48) + bytes([0x7F])
    cmd, dat = bs300_parse_response(data_resp)
    assert cmd == 0x008000 and len(dat) == 48
    print(f"  Parse 52B data:    cmd=0x{cmd:06X}, data_len={len(dat)}  ✓")

    print("\n=== Step 0: ALL TESTS PASSED ===\n")

