/**
 * BS300 Program Burn Data Read — I2C Command Flow
 *
 * Reads program burn data (Flash format) from BS300 chip via I2C.
 * Protocol: 11 packets (0–10), each 48 bytes → 528 bytes total payload.
 *
 * Usage:
 *   1. Implement bs300_i2c_write() and bs300_i2c_read() for your platform.
 *   2. Call bs300_program_read(program_index, &prog) to read program data.
 *   3. Call bs300_program_parse() to decode the Flash bit-packed format.
 *
 * Reference: BS300 Protocol Handbook v3
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ============================================================
 * Platform I2C Abstraction — implement these for your hardware
 * ============================================================ */

#ifndef BS300_I2C_ADDR
#define BS300_I2C_ADDR  0x02
#endif

/**
 * Write bytes to BS300 via I2C.
 * Return true on success.
 */
extern bool bs300_i2c_write(uint8_t addr, const uint8_t *data, uint8_t len);

/**
 * Read bytes from BS300 via I2C.
 * Return true on success.
 */
extern bool bs300_i2c_read(uint8_t addr, uint8_t *data, uint8_t len);

/**
 * Delay milliseconds — platform-specific.
 */
extern void bs300_delay_ms(uint32_t ms);

/* ============================================================
 * Protocol Constants
 * ============================================================ */

#define BS300_PKT_SIZE          48   /* data section per packet */
#define BS300_PKT_COUNT         11   /* packets 0–10 for read */
#define BS300_TOTAL_DATA        (BS300_PKT_SIZE * BS300_PKT_COUNT)  /* 528 */

/* Command words: Read Start for program 0–3 */
#define BS300_CMD_READ_PROG(p)  (0x800000 | ((p) << 12) | 0x31)

/* Command words: Read packet X (0–10) */
#define BS300_CMD_READ_PKT(x)   (0x800000 | ((x) << 12) | 0x11)

/* Frame lengths */
#define BS300_SIMPLE_CMD_LEN     6
#define BS300_READ_REQ_LEN       3
#define BS300_ADV_WRITE_LEN      54

/* I2C read response lengths */
#define BS300_STATUS_RESP_LEN    4
#define BS300_DATA_RESP_LEN      52

/* ============================================================
 * Checksum
 * ============================================================ */

/**
 * BS300 checksum over payload (Length + Command + Data bytes).
 * checksum = 0xFF - (sum & 0xFF)
 * NOTE: Slave address byte is NOT included.
 */
static uint8_t bs300_checksum(const uint8_t *payload, uint8_t len)
{
    uint16_t sum = 0;
    for (uint8_t i = 0; i < len; i++) {
        sum += payload[i];
    }
    return (uint8_t)(0xFF - (sum & 0xFF));
}

/* ============================================================
 * Frame Building
 * ============================================================ */

/**
 * Build a Simple Command frame (6 bytes including slave addr).
 * Format: [Addr(0x02)] Len(0x00) Cmd_L Cmd_M Cmd_H Chk
 *
 * @param cmd_word  24-bit command word (little-endian)
 * @param frame_out Output buffer, must be at least 6 bytes
 */
static void bs300_build_simple_cmd(uint32_t cmd_word, uint8_t *frame_out)
{
    uint8_t payload[4];
    payload[0] = 0x00;                               /* Length */
    payload[1] = (uint8_t)(cmd_word & 0xFF);         /* Cmd L */
    payload[2] = (uint8_t)((cmd_word >> 8) & 0xFF);  /* Cmd M */
    payload[3] = (uint8_t)((cmd_word >> 16) & 0xFF); /* Cmd H */

    frame_out[0] = BS300_I2C_ADDR;
    frame_out[1] = payload[0];
    frame_out[2] = payload[1];
    frame_out[3] = payload[2];
    frame_out[4] = payload[3];
    frame_out[5] = bs300_checksum(payload, 4);
}

/**
 * Build a Read Request frame (3 bytes including slave addr).
 * Format: [Addr(0x02)] Len(R/W-REQ=1|len) Chk
 *
 * @param length_data  0x00 for status query, 0x10 for data read
 * @param frame_out    Output buffer, must be at least 3 bytes
 */
static void bs300_build_read_request(uint8_t length_data, uint8_t *frame_out)
{
    uint8_t len_byte = 0x80 | length_data;  /* bit7=1 → R/W-REQ */
    frame_out[0] = BS300_I2C_ADDR;
    frame_out[1] = len_byte;
    frame_out[2] = bs300_checksum(&len_byte, 1);
}

/* ============================================================
 * Command Word Field Extraction
 * ============================================================ */

/** FURPROC (bit 23): 0 = ready, 1 = needs further processing */
static bool bs300_cmd_furproc(uint32_t cmd_word)
{
    return (cmd_word >> 23) & 1;
}

/** PKTNUM (bits 15:12): packet sequence number 0–15 */
static uint8_t bs300_cmd_pktnum(uint32_t cmd_word)
{
    return (cmd_word >> 12) & 0xF;
}

/* ============================================================
 * Response Parsing
 * ============================================================ */

/**
 * Parse a 4-byte or 52-byte I2C read response.
 *
 * @param resp         Raw response bytes
 * @param len          Response length (4 or 52)
 * @param cmd_word_out Parsed command word (bits [2:0] = checksum discarded)
 * @param data_out     48-byte data section (only valid when len == 52)
 * @return true on success
 */
static bool bs300_parse_response(const uint8_t *resp, uint8_t len,
                                  uint32_t *cmd_word_out, uint8_t *data_out)
{
    if (len == BS300_STATUS_RESP_LEN) {
        /* 4-byte status: Cmd_L Cmd_M Cmd_H Chk */
        *cmd_word_out = resp[0] | ((uint32_t)resp[1] << 8) | ((uint32_t)resp[2] << 16);
        /* Verify checksum */
        uint8_t calc_chk = bs300_checksum(resp, 3);
        if (calc_chk != resp[3]) return false;
        return true;
    } else if (len == BS300_DATA_RESP_LEN) {
        /* 52-byte data: Cmd_L Cmd_M Cmd_H [48B data] Chk */
        *cmd_word_out = resp[0] | ((uint32_t)resp[1] << 8) | ((uint32_t)resp[2] << 16);
        /* Verify checksum over Cmd(3B) + Data(48B) */
        uint8_t buf[51];
        memcpy(buf, resp, 3);
        memcpy(buf + 3, resp + 3, 48);
        uint8_t calc_chk = bs300_checksum(buf, 51);
        if (calc_chk != resp[51]) return false;
        if (data_out) memcpy(data_out, resp + 3, 48);
        return true;
    }
    return false;
}

/* ============================================================
 * Program Burn Read Flow
 * ============================================================ */

/**
 * Read all 11 packets of program burn data for a given program index.
 *
 * Flow:
 *   1. Send Read Start command 0x80Y031 (Y = program_index 0–3)
 *   2. Wait 60ms, poll FURPROC until ready
 *   3. Wait 60ms after ready
 *   4. Read packets 0–10: each 52-byte I2C read, extract 48-byte data
 *
 * @param program_index  Program number 0–3
 * @param data_out       Output buffer for 528 bytes (11 × 48)
 *                       or NULL to discard data (status check only)
 * @return true on success, false on I2C error or timeout
 */
bool bs300_program_read(uint8_t program_index, uint8_t *data_out)
{
    if (program_index > 3) return false;

    uint8_t frame[BS300_ADV_WRITE_LEN];
    uint8_t resp[BS300_DATA_RESP_LEN];
    uint32_t cmd_word;
    int retry;

    /* ---- Step 1: Send Read Start command ---- */
    uint32_t start_cmd = BS300_CMD_READ_PROG(program_index);
    bs300_build_simple_cmd(start_cmd, frame);
    if (!bs300_i2c_write(BS300_I2C_ADDR, frame, BS300_SIMPLE_CMD_LEN)) {
        return false;
    }

    /* ---- Step 2: Wait 60ms, then poll FURPROC until ready ---- */
    bs300_delay_ms(60);

    retry = 50;  /* max ~3s at 60ms intervals */
    do {
        /* Send Read Request (len=0x00) for status */
        bs300_build_read_request(0x00, frame);
        if (!bs300_i2c_write(BS300_I2C_ADDR, frame, BS300_READ_REQ_LEN)) {
            return false;
        }
        /* Read 4-byte status response */
        if (!bs300_i2c_read(BS300_I2C_ADDR, resp, BS300_STATUS_RESP_LEN)) {
            return false;
        }
        if (!bs300_parse_response(resp, BS300_STATUS_RESP_LEN, &cmd_word, NULL)) {
            return false;
        }
        if (!bs300_cmd_furproc(cmd_word)) {
            break;  /* ready */
        }
        bs300_delay_ms(60);
    } while (--retry > 0);

    if (retry <= 0) {
        return false;  /* timeout: FURPROC never cleared */
    }

    /* ---- Step 3: Wait 60ms after ready ---- */
    bs300_delay_ms(60);

    /* ---- Step 4: Read packets 0–10 ---- */
    for (uint8_t pkt = 0; pkt < BS300_PKT_COUNT; pkt++) {
        /* Send Read Request (len=0x10) for data */
        bs300_build_read_request(0x10, frame);
        if (!bs300_i2c_write(BS300_I2C_ADDR, frame, BS300_READ_REQ_LEN)) {
            return false;
        }
        /* Read 52-byte data response */
        if (!bs300_i2c_read(BS300_I2C_ADDR, resp, BS300_DATA_RESP_LEN)) {
            return false;
        }
        if (!bs300_parse_response(resp, BS300_DATA_RESP_LEN, &cmd_word,
                                   data_out ? data_out + (pkt * BS300_PKT_SIZE) : NULL)) {
            return false;
        }

        /* Verify packet number in response matches expected */
        if (bs300_cmd_pktnum(cmd_word) != pkt) {
            return false;
        }
    }

    return true;
}

/* ============================================================
 * Program Data Structures (decoded Flash format)
 * ============================================================ */

#define BS300_WDRC_MAX_CHANNELS   16
#define BS300_WDRC_BANDS          32
#define BS300_ENR_MAX_CHANNELS    16

typedef struct {
    uint8_t frequency_idx;   /* 6-bit, channel frequency table index */
    uint8_t epd_at;          /* 7-bit, attack time index (Table 2-2) */
    uint8_t epd_rt;          /* 7-bit, release time index */
    uint8_t epd_r;           /* 7-bit, ratio index (Table 2-3) */
    uint8_t kp1_th;          /* 7-bit, = value_in_MT (dB) */
    uint8_t kp2_th;          /* 7-bit, = value_in_MT, 0 if 1KP */
    uint8_t kp1_at;          /* 7-bit, attack time index */
    uint8_t kp2_at;          /* 7-bit */
    uint8_t kp1_rt;          /* 7-bit, release time index */
    uint8_t kp2_rt;          /* 7-bit */
    uint8_t kp1_r;           /* 7-bit, ratio index */
    uint8_t kp2_r;           /* 7-bit */
    uint8_t lmt_th;          /* 7-bit, = value_in_MT - 30 (dB) */
    uint8_t lmt_at;          /* 7-bit */
    uint8_t lmt_rt;          /* 7-bit */
    uint8_t lmt_r;           /* 7-bit */
} bs300_wdrc_channel_t;

typedef struct {
    uint8_t  kneepoints_per_channel;  /* 0=1KP, 1=2KP */
    uint8_t  output_limiting_sel;     /* 0=off, 1=on */
    uint8_t  num_channels;            /* 1–16 */
    uint8_t  bin_gain[BS300_WDRC_BANDS];  /* 32 × 7-bit, = 27 + value_in_MT */
    bs300_wdrc_channel_t channels[BS300_WDRC_MAX_CHANNELS];
} bs300_wdrc_t;

typedef struct {
    uint8_t  beep_level;
    uint16_t beep_frequency;
    int8_t   min_volume;
    int8_t   max_volume;
    uint8_t  battery_flat_beep_level;
    uint16_t battery_flat_beep_frequency;
} bs300_volume_t;

typedef struct {
    char     input_type[12];   /* "front_mic", "rear_mic", "telecoil", "dai",
                                  "mm_plus", "ddm2", "dual_mic" */
    /* MM Plus fields */
    uint8_t  mic_mixing_ratio; /* = 50 + value_in_MT */
    uint8_t  mm_type;          /* 0=Telecoil, 1=DAI */
    /* DDM2 fields */
    uint8_t  omni_threshold;
    uint8_t  mode;             /* 0=FDM, 1=ADM */
    uint32_t cutoff_frequency;
} bs300_inputs_t;

typedef struct {
    uint8_t dfbc_mode;         /* 0x01/0x03/0x07/0x09/0x0B/0x0F */
} bs300_dfbc_t;

typedef struct {
    uint8_t  frequency_idx;   /* 6-bit */
    uint8_t  ma;              /* 5-bit, = value_in_MT */
    uint8_t  snrth;           /* 5-bit, = value_in_MT */
    uint8_t  nt;              /* 6-bit, = value_in_MT - 10 */
    uint8_t  unt;             /* 6-bit, = value_in_MT - 40 */
    uint8_t  etr;             /* 7-bit, = value_in_MT × 100 */
    uint8_t  nrr;             /* 4-bit, = value_in_MT × 10 */
} bs300_enr_channel_t;

typedef struct {
    uint8_t  nfsf;             /* 4-bit, = value_in_MT - 1 */
    uint8_t  nhsf;             /* 4-bit */
    uint8_t  nnsf;             /* 4-bit */
    uint8_t  num_channels;
    uint8_t  snasf;            /* 4-bit, = value_in_MT - 1 */
    bs300_enr_channel_t channels[BS300_ENR_MAX_CHANNELS];
} bs300_enr_t;

typedef struct {
    uint8_t iss_threshold;     /* = value_in_MT */
} bs300_iss_t;

typedef struct {
    uint8_t dual_mic_mode_sel;
    uint8_t suppression_strength_preset;  /* = value_in_MT */
} bs300_wnr_t;

typedef struct {
    uint16_t attack_time;      /* uint12, ms */
    uint16_t release_time;     /* uint12, ms */
    uint8_t  threshold;        /* = abs(value_in_MT) */
} bs300_agco_t;

typedef struct {
    bs300_wdrc_t   wdrc;
    bs300_volume_t volume;
    bs300_inputs_t inputs;
    bool           has_dfbc;
    bs300_dfbc_t   dfbc;
    bool           has_enr;
    bs300_enr_t    enr;
    bool           has_iss;
    bs300_iss_t    iss;
    bool           has_wnr;
    bs300_wnr_t    wnr;
    bool           has_agco;
    bs300_agco_t   agco;
} bs300_program_data_t;

/* ============================================================
 * BitReader (LSB-first, cross-byte)
 * ============================================================ */

typedef struct {
    const uint8_t *data;
    uint16_t       bit_pos;    /* global bit position */
    uint16_t       data_len;
} bs300_bitreader_t;

static void bs300_br_init(bs300_bitreader_t *br, const uint8_t *data, uint16_t len)
{
    br->data = data;
    br->bit_pos = 0;
    br->data_len = len;
}

static uint32_t bs300_br_read(bs300_bitreader_t *br, uint8_t n_bits)
{
    uint32_t result = 0;
    for (uint8_t i = 0; i < n_bits; i++) {
        uint16_t byte_idx = br->bit_pos / 8;
        uint8_t  bit_idx  = br->bit_pos % 8;
        if (byte_idx >= br->data_len) return result;
        uint8_t bit = (br->data[byte_idx] >> bit_idx) & 1;
        result |= ((uint32_t)bit << i);
        br->bit_pos++;
    }
    return result;
}

static void bs300_br_skip(bs300_bitreader_t *br, uint8_t n_bits)
{
    br->bit_pos += n_bits;
}

/* ============================================================
 * Module Decode Functions
 * ============================================================ */

/* Module command table */
typedef enum {
    BS300_MODULE_WDRC             = 0x12,
    BS300_MODULE_VOLUME           = 0x07,
    BS300_MODULE_INPUT_FRONT_MIC  = 0x03,
    BS300_MODULE_INPUT_REAR_MIC   = 0x04,
    BS300_MODULE_INPUT_TELECOIL   = 0x05,
    BS300_MODULE_INPUT_DAI        = 0x06,
    BS300_MODULE_INPUT_MM_PLUS    = 0x17,
    BS300_MODULE_INPUT_DDM2       = 0x1B,
    BS300_MODULE_INPUT_DUAL_MIC   = 0x1E,
    BS300_MODULE_DFBC             = 0x14,
    BS300_MODULE_ENR              = 0x1C,
    BS300_MODULE_NOISE_GEN2       = 0x21,
    BS300_MODULE_ISS              = 0x1D,
    BS300_MODULE_WNR              = 0x1F,
    BS300_MODULE_ACCLIMATIZATION  = 0x26,
    BS300_MODULE_AGCO             = 0x23,
} bs300_module_cmd_t;

static bool _decode_wdrc_channel(bs300_bitreader_t *br, bs300_wdrc_channel_t *ch)
{
    ch->frequency_idx = (uint8_t)bs300_br_read(br, 6);
    ch->epd_at = (uint8_t)bs300_br_read(br, 7);
    ch->epd_rt = (uint8_t)bs300_br_read(br, 7);
    ch->epd_r  = (uint8_t)bs300_br_read(br, 7);

    if (bs300_br_read(br, 2) != 0x2) return false;  /* P1 marker */

    ch->kp1_th = (uint8_t)bs300_br_read(br, 7);
    ch->kp2_th = (uint8_t)bs300_br_read(br, 7);

    if (bs300_br_read(br, 2) != 0x2) return false;  /* P2 marker */

    ch->kp1_at = (uint8_t)bs300_br_read(br, 7);
    ch->kp2_at = (uint8_t)bs300_br_read(br, 7);

    if (bs300_br_read(br, 2) != 0x2) return false;  /* P3 marker */

    ch->kp1_rt = (uint8_t)bs300_br_read(br, 7);
    ch->kp2_rt = (uint8_t)bs300_br_read(br, 7);

    if (bs300_br_read(br, 2) != 0x2) return false;  /* P4 marker */

    ch->kp1_r = (uint8_t)bs300_br_read(br, 7);
    ch->kp2_r = (uint8_t)bs300_br_read(br, 7);

    ch->lmt_th = (uint8_t)bs300_br_read(br, 7);
    ch->lmt_at = (uint8_t)bs300_br_read(br, 7);
    ch->lmt_rt = (uint8_t)bs300_br_read(br, 7);
    ch->lmt_r  = (uint8_t)bs300_br_read(br, 7);

    return true;
}

static bool _decode_wdrc(const uint8_t *data, uint16_t len, bs300_wdrc_t *wdrc)
{
    bs300_bitreader_t br;
    bs300_br_init(&br, data, len);

    /* Byte 0 header */
    bs300_br_skip(&br, 1);                          /* bit0: fixed 1 */
    wdrc->output_limiting_sel = (uint8_t)bs300_br_read(&br, 1);
    wdrc->kneepoints_per_channel = (uint8_t)bs300_br_read(&br, 1);
    bs300_br_skip(&br, 5);                          /* bits 7:3: 0 */

    /* Byte 1 bit0 marker */
    bs300_br_skip(&br, 1);

    /* 32 band bin_gain (each 7-bit) */
    for (uint8_t i = 0; i < BS300_WDRC_BANDS; i++) {
        wdrc->bin_gain[i] = (uint8_t)bs300_br_read(&br, 7);
    }

    /* Byte 29[5:1]: num_channels */
    wdrc->num_channels = (uint8_t)bs300_br_read(&br, 5);
    bs300_br_skip(&br, 1);  /* B29[6]: reserved */
    /* B29[7] is ch1 freq[0] — consumed by next _decode_wdrc_channel */

    if (wdrc->num_channels > BS300_WDRC_MAX_CHANNELS) return false;

    for (uint8_t i = 0; i < wdrc->num_channels; i++) {
        if (!_decode_wdrc_channel(&br, &wdrc->channels[i])) return false;
    }

    return true;
}

static void _decode_volume(const uint8_t *data, bs300_volume_t *vol)
{
    vol->beep_level     = data[0];
    vol->beep_frequency = (uint16_t)(data[1] | (data[2] << 8));
    vol->min_volume     = (int8_t)data[3];
    vol->max_volume     = (int8_t)data[4];
    vol->battery_flat_beep_level     = data[5];
    vol->battery_flat_beep_frequency = (uint16_t)(data[6] | (data[7] << 8));
}

static void _decode_inputs(uint8_t cmd_data, const uint8_t *data,
                            uint16_t len, bs300_inputs_t *inp)
{
    memset(inp, 0, sizeof(*inp));

    switch (cmd_data) {
    case BS300_MODULE_INPUT_FRONT_MIC:  strcpy(inp->input_type, "front_mic");  break;
    case BS300_MODULE_INPUT_REAR_MIC:   strcpy(inp->input_type, "rear_mic");   break;
    case BS300_MODULE_INPUT_TELECOIL:   strcpy(inp->input_type, "telecoil");   break;
    case BS300_MODULE_INPUT_DAI:        strcpy(inp->input_type, "dai");        break;
    case BS300_MODULE_INPUT_DUAL_MIC:   strcpy(inp->input_type, "dual_mic");   break;
    case BS300_MODULE_INPUT_MM_PLUS:
        strcpy(inp->input_type, "mm_plus");
        if (len >= 3) {
            inp->mic_mixing_ratio = data[0];  /* = 50 + value_in_MT */
            inp->mm_type = data[1];           /* 0=Telecoil, 1=DAI */
        }
        break;
    case BS300_MODULE_INPUT_DDM2:
        strcpy(inp->input_type, "ddm2");
        if (len >= 6) {
            inp->omni_threshold  = data[1];
            inp->mode            = (data[2] >> 5) & 1;    /* 0=FDM, 1=ADM */
            inp->cutoff_frequency = (uint32_t)data[3] | ((uint32_t)data[4] << 8)
                                   | ((uint32_t)data[5] << 16);
        }
        break;
    default:
        strcpy(inp->input_type, "unknown");
        break;
    }
}

static void _decode_dfbc(const uint8_t *data, bs300_dfbc_t *dfbc)
{
    dfbc->dfbc_mode = data[0];
}

static bool _decode_enr(const uint8_t *data, uint16_t len, bs300_enr_t *enr)
{
    bs300_bitreader_t br;
    bs300_br_init(&br, data, len);

    enr->nfsf = (uint8_t)bs300_br_read(&br, 4);
    enr->nhsf = (uint8_t)bs300_br_read(&br, 4);
    enr->nnsf = (uint8_t)bs300_br_read(&br, 4);

    uint8_t num_ch_low  = (uint8_t)bs300_br_read(&br, 4);
    uint8_t num_ch_high = (uint8_t)bs300_br_read(&br, 2);
    uint8_t num_ch_minus1 = num_ch_low | (num_ch_high << 4);

    enr->num_channels = num_ch_minus1 + 1;
    if (enr->num_channels > BS300_ENR_MAX_CHANNELS) return false;

    for (uint8_t i = 0; i < enr->num_channels; i++) {
        enr->channels[i].frequency_idx = (uint8_t)bs300_br_read(&br, 6);
        enr->channels[i].ma    = (uint8_t)bs300_br_read(&br, 5);
        enr->channels[i].snrth = (uint8_t)bs300_br_read(&br, 5);
        enr->channels[i].nt    = (uint8_t)bs300_br_read(&br, 6);
        enr->channels[i].unt   = (uint8_t)bs300_br_read(&br, 6);
        enr->channels[i].etr   = (uint8_t)bs300_br_read(&br, 7);
        enr->channels[i].nrr   = (uint8_t)bs300_br_read(&br, 4);
    }

    enr->snasf = (uint8_t)bs300_br_read(&br, 4);
    return true;
}

static void _decode_iss(const uint8_t *data, bs300_iss_t *iss)
{
    iss->iss_threshold = data[0];
}

static void _decode_wnr(const uint8_t *data, bs300_wnr_t *wnr)
{
    wnr->dual_mic_mode_sel          = data[0];
    wnr->suppression_strength_preset = data[1];
}

static void _decode_agco(const uint8_t *data, bs300_agco_t *agco)
{
    agco->attack_time  = (uint16_t)(data[0] | ((data[1] & 0x0F) << 8));
    agco->release_time = (uint16_t)(((data[1] & 0xF0) >> 4) | (data[2] << 4));
    agco->threshold    = data[3];
}

/* ============================================================
 * Program Burn Data Parser
 * ============================================================ */

/**
 * Parse the concatenated 528-byte raw program data into decoded struct.
 *
 * Data layout (from Step 2 guide):
 *   Segment 1 (1B):    packet count
 *   Segment 2 (3B):    0x80 0x00 (N+1) — module command count
 *   Segment 3 (3×m B): m module entries {cmd_data, 0x00, length_in_words}
 *   Segment 4 (2B):    0xFB 0x00 — end marker
 *   Segment 5 (var):   module data (concatenated in order)
 *   Segment 6 (pad):   0x00 fill to 528 bytes
 *
 * @param raw    528-byte raw data (11 packets concatenated)
 * @param prog   Output: decoded program data
 * @return true on success
 */
bool bs300_program_parse(const uint8_t *raw, bs300_program_data_t *prog)
{
    if (!raw || !prog) return false;

    memset(prog, 0, sizeof(*prog));

    /* Parse header */
    /* raw[0] = packet count */
    /* raw[1:4] = 0x80 0x00 (N+1) */
    uint8_t num_cmds = raw[3] - 1;  /* N+1 → N */

    /* Parse module command list */
    uint16_t pos = 4;
    typedef struct {
        uint8_t cmd_data;
        uint8_t length_words;
    } cmd_entry_t;
    cmd_entry_t cmds[16];  /* max 15 modules */
    uint8_t cmd_count = 0;

    for (uint8_t i = 0; i < num_cmds; i++) {
        if (pos + 3 > BS300_TOTAL_DATA) return false;
        cmds[i].cmd_data     = raw[pos];
        cmds[i].length_words = raw[pos + 2];
        pos += 3;
        cmd_count++;
    }

    /* End marker */
    if (pos + 2 > BS300_TOTAL_DATA) return false;
    if (raw[pos] != 0xFB || raw[pos + 1] != 0x00) return false;
    pos += 2;

    /* Decode each module in order */
    for (uint8_t i = 0; i < cmd_count; i++) {
        uint16_t length_bytes = cmds[i].length_words * 3;
        if (pos + length_bytes > BS300_TOTAL_DATA) return false;

        switch (cmds[i].cmd_data) {
        case BS300_MODULE_WDRC:
            if (!_decode_wdrc(raw + pos, length_bytes, &prog->wdrc)) return false;
            break;

        case BS300_MODULE_VOLUME:
            if (length_bytes >= 9) {
                _decode_volume(raw + pos, &prog->volume);
            }
            break;

        case BS300_MODULE_INPUT_FRONT_MIC:
        case BS300_MODULE_INPUT_REAR_MIC:
        case BS300_MODULE_INPUT_TELECOIL:
        case BS300_MODULE_INPUT_DAI:
        case BS300_MODULE_INPUT_MM_PLUS:
        case BS300_MODULE_INPUT_DDM2:
        case BS300_MODULE_INPUT_DUAL_MIC:
            _decode_inputs(cmds[i].cmd_data, raw + pos, length_bytes, &prog->inputs);
            break;

        case BS300_MODULE_DFBC:
            prog->has_dfbc = true;
            _decode_dfbc(raw + pos, &prog->dfbc);
            break;

        case BS300_MODULE_ENR:
            prog->has_enr = true;
            if (!_decode_enr(raw + pos, length_bytes, &prog->enr)) return false;
            break;

        case BS300_MODULE_ISS:
            prog->has_iss = true;
            _decode_iss(raw + pos, &prog->iss);
            break;

        case BS300_MODULE_WNR:
            prog->has_wnr = true;
            _decode_wnr(raw + pos, &prog->wnr);
            break;

        case BS300_MODULE_AGCO:
            prog->has_agco = true;
            _decode_agco(raw + pos, &prog->agco);
            break;

        /* noise_gen2, acclimatization: skip (parse on demand) */
        default:
            break;
        }

        pos += length_bytes;
    }

    return true;
}

/* ============================================================
 * Convenience: Read + Parse in one call
 * ============================================================ */

/**
 * Read program burn data from chip and parse into decoded struct.
 *
 * @param program_index  Program number 0–3
 * @param prog           Output: decoded program data
 * @return true on success
 */
bool bs300_program_read_and_parse(uint8_t program_index, bs300_program_data_t *prog)
{
    static uint8_t raw[BS300_TOTAL_DATA];

    if (!bs300_program_read(program_index, raw)) {
        return false;
    }

    return bs300_program_parse(raw, prog);
}
