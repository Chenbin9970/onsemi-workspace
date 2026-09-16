#include "app.h"
#include "ble_rempro.h"
#include "ble_rempro_cmd.h"
#include "dsp_7100_cmd.h"
#include "dsp_7100_init.h"   /* DSP7100_WDRC_CH */

#include <printf.h>   /* Rempro 收发日志跟随 OUTPUT_INTERFACE */
#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

/* Reassembly + response buffers */
#define REASM_BUF_SIZE  100
static uint8_t reasm_buf[REASM_BUF_SIZE];
static uint8_t reasm_len;
static bool    reasm_pending;
static uint8_t s_device_on = 1;   /* tracks MUTE/ACTIVE state */

void rempro_reasm_append(const uint8_t *data, uint8_t len)
{
    if (reasm_len + len > REASM_BUF_SIZE) {
        reasm_len = 0;   /* overflow — drop and restart */
        reasm_pending = false;
        return;
    }
    PRINTF("[REMPRO RX] +%uB:", len);
    for (uint8_t i = 0; i < len; i++) PRINTF(" %02X", data[i]);
    PRINTF("\r\n");
    memcpy(reasm_buf + reasm_len, data, len);
    reasm_len += len;
    reasm_pending = true;
}

void rempro_reasm_reset(void)
{
    reasm_len = 0;
    reasm_pending = false;
}

#define TX_BUF_SIZE     200
static uint8_t tx_buf[TX_BUF_SIZE];

static uint8_t s_tx_frame[TX_BUF_SIZE];   /* pending stuffed frame */
static uint8_t s_tx_frame_len;
static uint8_t s_tx_offset;
static bool    s_tx_in_progress;

/* -------- helpers -------- */

static void print_hex(const char *tag, const uint8_t *buf, uint8_t len)
{
    PRINTF("[REMPRO] %s (%uB):", tag, len);
    for (uint8_t i = 0; i < len; i++) PRINTF(" %02X", buf[i]);
    PRINTF("\r\n");
}

static uint8_t hdlc_fcs(const uint8_t *data, uint8_t len)
{
    uint8_t sum = 0;
    while (len--) sum += *data++;
    return sum;
}

/* HDLC byte-stuffing: 7E → 7D 5E,  7D → 7D 5D.
 * Writes to dst, returns stuffed length. */
static uint8_t hdlc_stuff(uint8_t *dst, const uint8_t *src, uint8_t len)
{
    uint8_t w = 0;
    for (uint8_t r = 0; r < len; r++) {
        uint8_t b = src[r];
        if (b == 0x7E)      { dst[w++] = 0x7D; dst[w++] = 0x5E; }
        else if (b == 0x7D) { dst[w++] = 0x7D; dst[w++] = 0x5D; }
        else                { dst[w++] = b; }
    }
    return w;
}

static void rempro_tx_send_next(void);

/* Build, stuff, and send an HDLC response in ≤20B chunks. */
static void hdlc_response(uint16_t cmd_id, uint8_t flag,
                          const uint8_t *data, uint8_t data_len)
{
    /* Step 1: build unstuffed frame body */
    uint8_t raw[TX_BUF_SIZE];
    raw[0] = HDLC_SYS_ID;
    raw[1] = (uint8_t)(cmd_id & 0xFF);
    raw[2] = (uint8_t)((cmd_id >> 8) & 0xFF);
    raw[3] = flag;
    uint8_t raw_len = 4;
    if (data_len) {
        memcpy(raw + raw_len, data, data_len);
        raw_len += data_len;
    }
    raw[raw_len] = hdlc_fcs(raw, raw_len);
    raw_len++;

    /* Step 2: stuff body + FCS, wrap with 7E delimiters */
    uint8_t *p = tx_buf;
    *p++ = HDLC_SEPARATOR;
    p += hdlc_stuff(p, raw, raw_len);
    *p++ = HDLC_SEPARATOR;
    uint8_t frame_len = (uint8_t)(p - tx_buf);

    /* Step 3: start chunked send — first chunk now, rest driven by
     * rempro_env.sentSuccess (GATTC notification complete) in Main_Loop.
     * No ke_timer, so low-power sleep is unaffected. */
    print_hex("TX frame", tx_buf, frame_len);
    if (s_tx_in_progress) {
        PRINTF("[REMPRO] TX busy — dropping previous pending frame\r\n");
    }
    s_tx_frame_len = frame_len;
    memcpy(s_tx_frame, tx_buf, frame_len);
    s_tx_offset = 0;
    s_tx_in_progress = true;
    rempro_env.sentSuccess = 0;
    rempro_tx_send_next();
}

/* Send next ≤20B chunk of the pending frame. */
static void rempro_tx_send_next(void)
{
    if (ble_env.state != APPM_CONNECTED) {
        s_tx_in_progress = false;
        return;
    }

    uint8_t chunk = s_tx_frame_len - s_tx_offset;
    if (chunk > 20) chunk = 20;

    print_hex("TX chunk", s_tx_frame + s_tx_offset, chunk);
    RemproService_SendNotification(ble_env.conidx,
                                   REMPRO_IDX_ONOFF_VALUE_VAL,
                                   s_tx_frame + s_tx_offset, chunk);
    s_tx_offset += chunk;

    if (s_tx_offset >= s_tx_frame_len)
        s_tx_in_progress = false;
}

/* Poll from Main_Loop: send the next chunk once the previous notification
 * completed (GATTC_CmpEvt sets rempro_env.sentSuccess). Each chunk then goes
 * out in its own connection event — no ke_timer, no extra wake-ups. */
void rempro_tx_poll(void)
{
    if (!s_tx_in_progress) return;
    if (ble_env.state != APPM_CONNECTED) { s_tx_in_progress = false; return; }
    if (!rempro_env.sentSuccess) return;

    rempro_env.sentSuccess = 0;
    rempro_tx_send_next();
}

/* HDLC byte-unstuffing: 7D 5E → 7E,  7D 5D → 7D.
 * Processes buf[start .. end-1] in-place, returns new length. */
static uint8_t hdlc_unstuff(uint8_t *buf, uint8_t start, uint8_t end)
{
    uint8_t w = start;
    for (uint8_t r = start; r < end; r++) {
        if (buf[r] == 0x7D && (r + 1) < end) {
            uint8_t next = buf[r + 1];
            if (next == 0x5E)      { buf[w++] = 0x7E; r++; }
            else if (next == 0x5D) { buf[w++] = 0x7D; r++; }
            else                   { buf[w++] = buf[r]; }
        } else {
            buf[w++] = buf[r];
        }
    }
    return w;
}

/* Parse a complete HDLC frame from buf[0..len-1].
 * Returns pointer to data payload, or NULL on failure.
 * On success, sets *cmd_id and *data_len, and *consumed = raw frame length
 * (before unstuffing). */
static const uint8_t *hdlc_parse_frame(const uint8_t *buf, uint8_t len,
                                       uint16_t *cmd_id, uint8_t *data_len,
                                       uint8_t *consumed)
{
    *consumed = 0;
    if (len < 6) return NULL;
    if (buf[0] != HDLC_SEPARATOR) return NULL;

    uint8_t fcs_pos_raw = 0;
    for (uint8_t i = 1; i < len; i++) {
        if (buf[i] == HDLC_SEPARATOR) {
            fcs_pos_raw = i - 1;
            *consumed = i + 1;
            break;
        }
    }
    if (fcs_pos_raw < 4) return NULL;

    /* Unstuff frame body (SYS..FCS, between delimiters).
     * Must copy because the raw data is const (in reasm_buf). */
    uint8_t unstuffed[REASM_BUF_SIZE];
    uint8_t body_len = fcs_pos_raw;
    memcpy(unstuffed, buf + 1, body_len);
    body_len = hdlc_unstuff(unstuffed, 0, body_len);

    if (body_len < 4) return NULL;   /* need at least SY + CMDL + CMDH + FCS */
    if (unstuffed[0] != HDLC_SYS_ID) return NULL;

    *cmd_id = unstuffed[1] | ((uint16_t)unstuffed[2] << 8);

    /* FCS is the last byte of unstuffed body */
    uint8_t fcs_pos = body_len - 1;
    if (fcs_pos < 3) return NULL;

    uint8_t exp = hdlc_fcs(unstuffed, fcs_pos);
    if (unstuffed[fcs_pos] != exp) {
        PRINTF("[REMPRO] FCS err got=%02X exp=%02X\r\n", unstuffed[fcs_pos], exp);
        *consumed = 0;  /* wait for more data — FCS may be split across chunks */
        return NULL;
    }

    *data_len = fcs_pos - 3;
    if (*data_len > 0) {
        /* Copy unstuffed data into reasm_buf (safe: reasm_buf is not const) */
        memcpy((uint8_t *)buf, unstuffed + 3, *data_len);
        return buf;  /* data now sits at start of buf */
    }
    return NULL;
}

/* Build, stuff, and send an HDLC push frame (device→app, SYS_ID=1, no Flag).
 * Same chunking logic as hdlc_response, but SYS_ID=1 and no Flag byte. */
static void hdlc_push(uint16_t cmd_id, const uint8_t *data, uint8_t data_len)
{
    uint8_t raw[TX_BUF_SIZE];
    raw[0] = HDLC_SYS_ID_DEVICE;
    raw[1] = (uint8_t)(cmd_id & 0xFF);
    raw[2] = (uint8_t)((cmd_id >> 8) & 0xFF);
    uint8_t raw_len = 3;
    if (data_len) {
        memcpy(raw + raw_len, data, data_len);
        raw_len += data_len;
    }
    raw[raw_len] = hdlc_fcs(raw, raw_len);
    raw_len++;

    uint8_t *p = tx_buf;
    *p++ = HDLC_SEPARATOR;
    p += hdlc_stuff(p, raw, raw_len);
    *p++ = HDLC_SEPARATOR;
    uint8_t frame_len = (uint8_t)(p - tx_buf);

    print_hex("TX push", tx_buf, frame_len);
    uint8_t offset = 0;
    while (offset < frame_len) {
        uint8_t chunk = frame_len - offset;
        if (chunk > 20) chunk = 20;
        RemproService_SendNotification(ble_env.conidx,
                                       REMPRO_IDX_ONOFF_VALUE_VAL,
                                       tx_buf + offset, chunk);
        offset += chunk;
    }
}

/* Active push: notify app of program switch (CMD=5, SYS_ID=1).
 * Protocol: SYS_ID(1) + CMD_ID(2) + Scene_ID(1) */
void rempro_push_scene_change(uint8_t scene_id)
{
    if (ble_env.state != APPM_CONNECTED) return;
    PRINTF("[REMPRO] push scene=%u\r\n", scene_id);
    hdlc_push(CMD_PUSH_SCENE, &scene_id, 1);
}

/* Active push: notify app of volume change (CMD=4, SYS_ID=1).
 * Protocol: SYS_ID(1) + CMD_ID(2) + Current_Number(1) + Device_Type(1)
 *         + Volume(1) + Volume2(1) */
void rempro_push_volume_change(uint8_t prog, uint8_t volume)
{
    if (ble_env.state != APPM_CONNECTED) return;
    uint8_t d[4];
    d[0] = prog;       /* Current_Number */
    d[1] = 1;          /* Device_Type: 1=left (single device) */
    d[2] = volume;     /* Volume */
    d[3] = volume;     /* Volume2 (same as Volume for single device) */
    PRINTF("[REMPRO] push vol: prog=%u vol=%u\r\n", prog, volume);
    hdlc_push(CMD_PUSH_VOLUME, d, 4);
}

/* Active push: notify app of initial status done (CMD=6, SYS_ID=1).
 * Protocol: SYS_ID(1) + CMD_ID(2) + Device_Type(1) + Initial_Status(1)
 *   Device_Type=1 (left), Initial_Status=2 (初始化完成) */
void rempro_push_initial_status_done(void)
{
    if (ble_env.state != APPM_CONNECTED) return;
    uint8_t d[2];
    d[0] = 1;   /* Device_Type: 1=left (single device) */
    d[1] = 2;   /* Initial_Status: 2=初始化完成 */
    PRINTF("[REMPRO] push initial status done\r\n");
    hdlc_push(CMD_PUSH_INITIAL_STATUS, d, 2);
}

/* Active push: notify app of audiometry exit (CMD=6, SYS_ID=1).
 * Protocol: SYS_ID(1) + CMD_ID(2) + Device_Type(1) + Initial_Status(1)
 *   Device_Type=1 (left), Initial_Status=1 (未初始化) */
void rempro_push_audiometry_exit(void)
{
    if (ble_env.state != APPM_CONNECTED) return;
    uint8_t d[2];
    d[0] = 1;   /* Device_Type: 1=left (single device) */
    d[1] = 1;   /* Initial_Status: 1=未初始化 */
    PRINTF("[REMPRO] push audiometry exit\r\n");
    hdlc_push(CMD_PUSH_INITIAL_STATUS, d, 2);
}

/* ---- 延时推送：测听进入/退出完成后，隔 1s 再推 ----
 * 用 APP_7100_HB_Handler 的 200ms 周期 tick 计数（5 tick = 1s），不引入新的 ke_timer。
 * 原因：切程序（dsp_7100_switch_program）是阻塞调用且 7100 侧要消化一下，
 * 推早了 App 会拿到还没生效的状态；同时也避免推送抢在 CMD 40 的应答前面。 */
#define AUD_PUSH_TICKS   5
#define AUD_PUSH_ENTER   1
#define AUD_PUSH_EXIT    2

static uint8_t s_aud_push_cnt;       /* 倒计时，0 = 无待推 */
static uint8_t s_aud_push_what;      /* AUD_PUSH_* */

void rempro_deferred_tick(void)
{
    if (s_aud_push_cnt == 0) return;

    s_aud_push_cnt--;
    if (s_aud_push_cnt != 0) return;

    if (s_aud_push_what == AUD_PUSH_ENTER)      rempro_push_initial_status_done();
    else if (s_aud_push_what == AUD_PUSH_EXIT)  rempro_push_audiometry_exit();

    s_aud_push_what = 0;
}

static void aud_push_schedule(uint8_t what)
{
    s_aud_push_what = what;
    s_aud_push_cnt  = AUD_PUSH_TICKS;
    PRINTF("[REMPRO] audiometry push scheduled (%u) in %u ms\r\n",
           what, AUD_PUSH_TICKS * 200u);
}

/* ================================================================
 * Command Handlers
 * ================================================================ */

/* 设置类命令的统一应答：Flag(1) + status(1)
 * 协议文档：Flag 0=操作成功 / 非0=操作失败；status 0=不支持 / 非0=操作成功。
 * （SYS_ID、CMD_ID 由 hdlc_response 补）
 * ⚠ 2026-09-15：此前只有 SetVolume / SetCurrentScene 的**成功**分支回 status，
 *   其余分支和 SetDenoise / SetFeedbackOnOff / SetEqualizer 只回 Flag。
 *   这里按协议文档统一补齐 —— **Flag 的取值一律沿用原逻辑**，只补上缺失的 status 字节。 */
static void hdlc_response_set(uint16_t cmd_id, bool ok)
{
    uint8_t status = ok ? 1 : 0;

    hdlc_response(cmd_id, ok ? 0 : 1, &status, 1);
}

/* ID:33  GetDeviceOnOff */
static void cmd_getdeviceonoff(void)
{
    uint8_t resp[2];
    resp[0] = s_device_on;   /* Left_OnOff */
    resp[1] = s_device_on;   /* Right_OnOff (same as left) */
    PRINTF("[REMPRO] GetDeviceOnOff: L=%u R=%u\r\n", resp[0], resp[1]);
    hdlc_response(CMD_GETDEVICEONOFF, 0, resp, 2);
}

/* ID:4  GetBatteryInfo — 1664 无电池 AD 采样，固定回 100%（回 flag=1 会导致 App 连不上） */
static void cmd_getbatteryinfo_7100(void)
{
    uint8_t resp_data[2];
    resp_data[0] = 100;   /* Left_Battery */
    resp_data[1] = 100;   /* Right_Battery (single device) */
    PRINTF("[REMPRO] GetBatteryInfo: 100%%\r\n");
    hdlc_response(CMD_GETBATTERYINFO, 0, resp_data, 2);
}

/* ID:2  SetVolume — App vol 0-5 → 7100 档位 1-6（Volume_Number=5） */
static void cmd_setvolume_7100(const uint8_t *data, uint8_t len)
{
    uint8_t dev_type;
    uint8_t volume;
    uint8_t level;
    bool ok;

    if (len < 3) { hdlc_response_set(CMD_SETVOLUME, false); return; }

    dev_type = data[0];
    volume   = data[1];

    if (dev_type != 0 && dev_type != 1) {   /* 仅左右/左，右耳不支持 */
        /* 沿用原行为：右耳按"已受理但无动作"回，Flag 不置错 */
        hdlc_response_set(CMD_SETVOLUME, true);
        return;
    }
    if (volume > 5) volume = 5;

    level = (uint8_t)(volume + 1);          /* App 0-5 → 档位 1-6 */

    ok = dsp_7100_set_volume(level);
    PRINTF("[REMPRO] SetVolume7100: vol=%u -> level=%u ok=%u\r\n",
           volume, level, ok);

    hdlc_response_set(CMD_SETVOLUME, true);
}

/* ID:16  SetCurrentScene — App scene 0-3 → 7100 程序 1-4 */
static void cmd_setcurrentscene_7100(const uint8_t *data, uint8_t len)
{
    uint8_t scene_id;
    bool ok;

    if (len < 2) { hdlc_response_set(CMD_SETCURRENTSCENE, false); return; }

    scene_id = data[1];

    if (scene_id >= 4) {
        hdlc_response_set(CMD_SETCURRENTSCENE, false);
        return;
    }

    ok = dsp_7100_switch_program((uint8_t)(scene_id + 1));
    PRINTF("[REMPRO] SetCurrentScene7100: scene=%u -> prog=%u ok=%u\r\n",
           scene_id, scene_id + 1, ok);

    hdlc_response_set(CMD_SETCURRENTSCENE, true);
}

/* ID:9  SetDenoise — App prog 0-3 → 7100 程序 1-4，level 0-4 */
static void cmd_setdenoise_7100(const uint8_t *data, uint8_t len)
{
    uint8_t dev_type;
    uint8_t prog;
    uint8_t level;
    bool ok;

    if (len < 3) { hdlc_response_set(CMD_SETDENOISE, false); return; }

    dev_type = data[0];
    prog     = data[1];
    level    = data[2];

    if (prog >= 4) { hdlc_response_set(CMD_SETDENOISE, false); return; }
    if (level > 4) {                     /* 7100 只有 5 档（0-4），超出则钳位 */
        PRINTF("[REMPRO] SetDenoise: level %u 超范围，钳到 4\r\n", level);
        level = 4;
    }

    ok = dsp_7100_set_denoise((uint8_t)(prog + 1), level);
    PRINTF("[REMPRO] SetDenoise7100: dev=%u prog=%u level=%u started=%u\r\n",
           dev_type, prog, level, ok);

    /* 异步会话：ok = 已受理。完成情况见 [7100] session done 日志 */
    hdlc_response_set(CMD_SETDENOISE, ok);
}

/* ID:12  SetFeedbackOnOff — App prog 0-3 → 7100 程序 1-4，onoff 0/1（= DFBC） */
static void cmd_setfeedbackonoff_7100(const uint8_t *data, uint8_t len)
{
    uint8_t dev_type;
    uint8_t prog;
    uint8_t onoff;
    bool ok;

    if (len < 3) { hdlc_response_set(CMD_SETFEEDBACKONOFF, false); return; }

    dev_type = data[0];
    prog     = data[1];
    onoff    = data[2];

    if (prog >= 4) { hdlc_response_set(CMD_SETFEEDBACKONOFF, false); return; }

    ok = dsp_7100_set_dfbc((uint8_t)(prog + 1), onoff ? 1 : 0);
    PRINTF("[REMPRO] SetFeedbackOnOff7100: dev=%u prog=%u onoff=%u started=%u\r\n",
           dev_type, prog, onoff, ok);

    /* 异步会话：ok = 已受理。完成情况见 [7100] session done 日志 */
    hdlc_response_set(CMD_SETFEEDBACKONOFF, ok);
}

/* ID:10  SetEqualizer — App: {Device_Type, Equalizer_Type 0低/1中/2高, Value}
 * Value 0-100 = 正 dB；Value > 100 → Value-256 得负值（均衡器减小）。
 * 映射到当前程序的 WDRC LL/HL（按通道频率分段，1dB/LSB）。 */
static void cmd_setequalizer_7100(const uint8_t *data, uint8_t len)
{
    uint8_t dev_type;
    uint8_t eq_type;
    int16_t db;
    uint8_t prog;
    bool ok;

    if (len < 3) { hdlc_response_set(CMD_SETEQUALIZER, false); return; }

    dev_type = data[0];
    eq_type  = data[1];
    db       = (int16_t)(int8_t)data[2];   /* 253 → -3 */

    if (eq_type > 2) { hdlc_response_set(CMD_SETEQUALIZER, false); return; }

    /* 当前程序：0-based → 7100 程序 1-4 */
    prog = (uint8_t)(dsp_7100_get_program() - 1);
    if (prog > 3) prog = 0;

    ok = dsp_7100_set_eq((uint8_t)(prog + 1), eq_type, (int8_t)db);
    PRINTF("[REMPRO] SetEqualizer7100: dev=%u type=%u db=%d prog=%u started=%u\r\n",
           dev_type, eq_type, (int)db, prog, ok);

    hdlc_response_set(CMD_SETEQUALIZER, ok);
}

/* ============================================================================
 * ID:6 / 29 / 7 — WDRC 参数设置（SetGain / SetHighLevelGainData / SetMPO）
 *
 * 三条 payload 同构（SYS_ID、CMD_ID 已被 HDLC 层剥掉）：
 *   { Device_Type(1), Current_Number(1), { Index(1), Value(1) } × k }
 *   SetGain             ：Index = Spectrum 0-15 → 7100 ch1-16，Value 0-90 → LowLevelGain  -30~60
 *   SetHighLevelGainData：Index = Channel  0-15 → 7100 ch1-16，Value 0-90 → HighLevelGain -30~60
 *   SetMPO              ：Index = Channel  0-15 → 7100 ch1-16，Value 0-60 → OutputLimit   -60~0
 * 一次可带多个 {Index, Value} 对，**共用同一次 7100 会话**（静音/提交/解除静音只发一次）。
 * Index 16-31 忽略；超出 dsp 层范围的值由 dsp_7100_cmd.c 钳位。
 *
 * 应答按协议文档：Flag(1) + status(1)（status 0=不支持，非0=成功）。
 * ⚠ 现有 SetDenoise / SetFeedbackOnOff / SetEqualizer 三个 handler 只回 Flag、不回 status，
 *   与文档不符；这三条按文档实现，那三处未动。
 * 详见 docs/7100协议/瑞听设置指令.md
 * ========================================================================== */

typedef bool (*dsp_7100_wdrc_set_fn)(uint8_t prog, const dsp_7100_wdrc_item_t *items,
                                     uint8_t n);

static void cmd_setwdrc_7100(uint16_t cmd_id, const char *name, const uint8_t *data,
                             uint8_t len, int16_t val_offset, dsp_7100_wdrc_set_fn set_fn)
{
    dsp_7100_wdrc_item_t items[DSP7100_WDRC_CH];
    uint8_t  dev_type;
    uint8_t  prog;
    uint8_t  n = 0;
    uint16_t i;
    bool     ok;

    if (len < 2) { hdlc_response_set(cmd_id, false); return; }

    dev_type = data[0];
    prog     = data[1];

    if (prog >= 4) { hdlc_response_set(cmd_id, false); return; }

    for (i = 2; (i + 1u) < (uint16_t)len; i += 2u) {
        uint8_t  idx = data[i];
        int16_t  val = (int16_t)data[i + 1u] + val_offset;

        if (n >= DSP7100_WDRC_CH) break;

        if (idx >= DSP7100_WDRC_CH) {          /* 只用 0-15；16-31 忽略 */
            PRINTF("[REMPRO] %s: index %u 超范围，忽略\r\n", name, idx);
            continue;
        }

        items[n].ch  = (uint8_t)(idx + 1u);    /* 0-15 → ch1-16 */
        items[n].val = (int8_t)val;
        n++;
    }

    if (n == 0) { hdlc_response_set(cmd_id, false); return; }

    /* App prog 0-3 → 7100 程序 1-4；ok = 会话已受理（异步） */
    ok = set_fn((uint8_t)(prog + 1u), items, n);
    PRINTF("[REMPRO] %s: dev=%u prog=%u n=%u started=%u\r\n",
           name, dev_type, prog, n, ok);

    hdlc_response_set(cmd_id, ok);
}

/* ID:6  SetGain — 增益 → LowLevelGain（Value 0-90 → -30~60） */
static void cmd_setgain_7100(const uint8_t *data, uint8_t len)
{
    cmd_setwdrc_7100(CMD_SETGAIN, "SetGain", data, len, -30, dsp_7100_set_low_level_gain);
}

/* ID:29 SetHighLevelGainData — 高水平增益 → HighLevelGain（Value 0-90 → -30~60） */
static void cmd_sethighlevelgain_7100(const uint8_t *data, uint8_t len)
{
    cmd_setwdrc_7100(CMD_SETHIGHLEVELGAINDATA, "SetHighLevelGain", data, len,
                     -30, dsp_7100_set_high_level_gain);
}

/* ID:7  SetMPO — MPO 最大输出量 → OutputLimit（Value 0-60 → -60~0） */
static void cmd_setmpo_7100(const uint8_t *data, uint8_t len)
{
    cmd_setwdrc_7100(CMD_SETMPO, "SetMPO", data, len, -60, dsp_7100_set_output_limit);
}

/* ============================================================================
 * ID:22 / 30 / 23 — WDRC 读回（GetGainData / GetHighLevelGainData / GetMPOData）
 *
 * 请求：{ Device_Type(1), Current_Number(1) }
 * 应答：Flag(1) + Channel_Number(1) + { Index(1), Value(1) } × Channel_Number
 *   Index 0 起（= 7100 通道号 - 1）
 *   GetGainData          ：Value = LowLevelGain  + 30   （-30~60 → 0~90）
 *   GetHighLevelGainData ：Value = HighLevelGain + 30   （-30~60 → 0~90）
 *   GetMPOData           ：Value = OutputLimit   + 60   （-60~0  → 0~60）
 *
 * 数据取自读回缓存（dsp_7100_get_prog）；**读回尚未完成或该程序无效时回 Flag=1**，
 * App 需等开机读回跑完（见 [7100] 读回日志）再取。
 * 详见 docs/7100协议/瑞听设置指令.md、docs/7100协议/WDRC/7100_WDRC读取.md
 * ========================================================================== */

#define WDRC_RB_LL  0
#define WDRC_RB_HL  1
#define WDRC_RB_OL  2

static void cmd_getwdrc_7100(uint16_t cmd_id, const char *name, const uint8_t *data,
                             uint8_t len, uint8_t which, int16_t val_offset)
{
    const dsp_7100_prog_t *p;
    uint8_t resp[1 + DSP7100_WDRC_CH * 2];
    uint8_t prog;
    uint8_t n = 0;
    uint8_t ch;

    if (len < 2) { hdlc_response(cmd_id, 1, NULL, 0); return; }

    prog = data[1];                        /* data[0] = Device_Type，忽略 */
    if (prog >= DSP7100_RB_PROGS) { hdlc_response(cmd_id, 1, NULL, 0); return; }

    p = dsp_7100_get_prog(prog);           /* App prog 0-3 = 读回缓存索引 0-3 */
    if (p == NULL) {
        PRINTF("[REMPRO] %s: prog=%u 读回数据还不可用\r\n", name, prog);
        hdlc_response(cmd_id, 1, NULL, 0);
        return;
    }

    for (ch = 1; ch <= DSP7100_WDRC_CH; ch++) {
        int16_t v;

        if (which == WDRC_RB_LL)      v = p->wdrc_ll[ch - 1];
        else if (which == WDRC_RB_HL) v = p->wdrc_hl[ch - 1];
        else                          v = p->wdrc_ol[ch - 1];

        v += val_offset;
        if (v < 0)   { v = 0; }            /* 缓存异常时饱和，避免回绕 */
        if (v > 255) { v = 255; }

        resp[1 + n * 2]     = (uint8_t)(ch - 1);   /* Index/Channel 0 起 */
        resp[1 + n * 2 + 1] = (uint8_t)v;
        n++;
    }

    resp[0] = n;                           /* Channel_Number */

    PRINTF("[REMPRO] %s: prog=%u n=%u\r\n", name, prog, n);
    hdlc_response(cmd_id, 0, resp, (uint8_t)(1 + n * 2));
}

/* ID:22  GetGainData — 增益 ← LowLevelGain（+30） */
static void cmd_getgaindata_7100(const uint8_t *data, uint8_t len)
{
    cmd_getwdrc_7100(CMD_GETGAINDATA, "GetGainData", data, len, WDRC_RB_LL, 30);
}

/* ID:30  GetHighLevelGainData — 高水平增益 ← HighLevelGain（+30） */
static void cmd_gethighlevelgain_7100(const uint8_t *data, uint8_t len)
{
    cmd_getwdrc_7100(CMD_GETHIGHLEVELGAINDATA, "GetHighLevelGain", data, len,
                     WDRC_RB_HL, 30);
}

/* ID:23  GetMPOData — MPO ← OutputLimit（+60） */
static void cmd_getmpodata_7100(const uint8_t *data, uint8_t len)
{
    cmd_getwdrc_7100(CMD_GETMPODATA, "GetMPOData", data, len, WDRC_RB_OL, 60);
}

/* ============================================================================
 * ID:21 SetMuteData — 静音开关
 *
 * 请求 { Device_Type(1), Mute(1) }：**Mute 非 0 = 静音**，0 = 取消静音。
 * 应答 Flag + status。
 *
 * ⚠ 方向与 3 号 SetDeviceOnOff **相反**：3 号 data[1] 非 0 是"开"，
 *   这里非 0 是"静音"—— 因为文档把该字段定义为 Mute 且标注"0:关 1:开"。
 * 参考 remote_mic_rx_coex_1654/code/ble_rempro_cmd.c 的 cmd_setmutedata()；
 * 1654 走 bs300_mute()/bs300_active()，这里改成 dsp_7100_set_mute()。
 *
 * ⚠ App 约每 6s 重发一次（实测），所以**只在状态变化时才动 I2C**，否则只回应答 ——
 *   不然会不停打断 7100 的读回/写会话。
 * ========================================================================== */

static void cmd_setmutedata_7100(const uint8_t *data, uint8_t len)
{
    uint8_t dev_type;
    uint8_t mute;
    uint8_t want;          /* 目标 s_device_on */
    uint8_t changed;
    bool    ok = true;

    if (len < 2) { hdlc_response_set(CMD_SETMUTEDATA, false); return; }

    dev_type = data[0];
    mute     = data[1];
    want     = mute ? 0u : 1u;
    changed  = (want != s_device_on) ? 1u : 0u;

    if (changed) {
        ok = dsp_7100_set_mute(mute != 0u);
        if (ok) s_device_on = want;
    }

    PRINTF("[REMPRO] SetMuteData: dev=%u mute=%u on=%u changed=%u ok=%u\r\n",
           dev_type, mute, s_device_on, changed, ok);
    hdlc_response_set(CMD_SETMUTEDATA, ok);
}

/* ============================================================================
 * ID:40 SetAudiometryStatus — 进入/退出纯音测听
 *
 *   Fitting_Status = 0  进入测听：记下当前程序 → 切到程序 3 → `0x2E = 0x00` → 解除静音 → 推
 *                       SYS_ID=1 CMD 6 {Device_Type, Initial_Status=2 初始化完成}
 *   Fitting_Status = 1  退出测听：切回进入前的程序 → `0x2E = 0x58` → 解除静音 → 推
 *                       SYS_ID=1 CMD 6 {Device_Type, Initial_Status=1}（"退出完成"）
 *   2 / 3（协议文档里的"进入/退出试听"）暂未实现。
 *
 * 7100 侧序列按 `tonestar.txt` / `tonestop.txt` 抓包：
 *   进：`A2 00 16 03` → `A2 00 2E 00` → `A7 01 00 00 00 26`
 *   停：出音停帧 → `A2 00 2E 58` → `A7 01 00 00 00 26`
 *   注意两次末尾**都有解除静音 `26` 却没有 `25`** —— 推测是 `0x2E` 那次模式切换本身会静音。
 *
 * 参考 remote_mic_rx_coex_1654/code/ble_rempro_cmd.c 的 cmd_setaudiometrystatus()。
 * 1654 走 BS300（进测听时把降噪/AGCO/反馈抑制整组清零并写平坦通路，再靠 ITG 出纯音）；
 * 1664 已无 BS300，改为**切程序 + 0x2E 模式寄存器**。
 *
 * 顺序（照 1654，也是实测要求）：
 *   **先回 CMD 40 的应答 → 做 I2C → 延 1s 再推 SYS_ID=1 CMD 6**
 *   推早了会抢在应答前面发出去（实测日志：push 先于 TX frame），App 会拿到没生效的状态。
 * ========================================================================== */

#define AUDIOMETRY_PROG   3

static uint8_t s_audiometry_prev_prog;   /* 进入测听前的程序，退出时切回 */
static bool    s_audiometry_active;

static void cmd_setaudiometrystatus(const uint8_t *data, uint8_t len)
{
    uint8_t fitting_status;

    if (len < 2) { hdlc_response_set(CMD_SETAUDIOMETRYSTATUS, false); return; }

    fitting_status = data[1];
    PRINTF("[REMPRO] SetAudiometryStatus: status=%u active=%u\r\n",
           fitting_status, s_audiometry_active);

    /* 会话占着 I2C 时不能插阻塞的切程序调用 */
    if (dsp_7100_cmd_busy()) {
        PRINTF("[REMPRO] SetAudiometryStatus: 7100 会话进行中，忽略\r\n");
        hdlc_response_set(CMD_SETAUDIOMETRYSTATUS, false);
        return;
    }

    /* 先回应答，再做活（切程序是阻塞调用，放后面不影响应答时序） */
    hdlc_response_set(CMD_SETAUDIOMETRYSTATUS, true);

    switch (fitting_status) {
    case 0:   /* 进入测听：切程序 → 0x2E=0x00 → 解除静音（照 tonestar.txt 抓包顺序）*/
        if (s_audiometry_active) break;                  /* 已在测听，幂等 */
        s_audiometry_prev_prog = dsp_7100_get_program();
        if (!dsp_7100_switch_program(AUDIOMETRY_PROG)) {
            PRINTF("[REMPRO] SetAudiometryStatus: 切入程序%u 失败\r\n", AUDIOMETRY_PROG);
            break;
        }
        s_audiometry_active = true;
        if (!dsp_7100_set_tone_mode(true)) {
            PRINTF("[REMPRO] SetAudiometryStatus: 0x2E 置位失败\r\n");
        }
        dsp_7100_set_mute(false);                        /* 抓包末尾恒有 26（解除静音）*/
        aud_push_schedule(AUD_PUSH_ENTER);               /* 1s 后推 Initial_Status = 2 */
        break;

    case 1:   /* 退出测听：切回原程序 → 0x2E=0x58 → 解除静音 */
        if (!s_audiometry_active) break;                 /* 未在测听，幂等 */
        if (!dsp_7100_switch_program(s_audiometry_prev_prog)) {
            PRINTF("[REMPRO] SetAudiometryStatus: 切回程序%u 失败\r\n",
                   s_audiometry_prev_prog);
            break;
        }
        s_audiometry_active = false;
        if (!dsp_7100_set_tone_mode(false)) {
            PRINTF("[REMPRO] SetAudiometryStatus: 0x2E 复位失败\r\n");
        }
        dsp_7100_set_mute(false);                        /* 抓包末尾恒有 26（解除静音）*/
        aud_push_schedule(AUD_PUSH_EXIT);                /* 1s 后推 Initial_Status = 1 */
        break;

    default:
        PRINTF("[REMPRO] SetAudiometryStatus: status=%u 未支持\r\n", fitting_status);
        break;
    }

    PRINTF("[REMPRO] SetAudiometryStatus: prog %u -> %u\r\n",
           s_audiometry_prev_prog, dsp_7100_get_program());
}

/* ============================================================================
 * ID:13 / 14 — 纯音测听（播放 / 停止）
 *
 *   SetPlayVoice (13) 请求 { Device_Type(1), Spectrum(1), Decibel(1) }
 *       Spectrum 0-16 → 250/500/1000/…/8000 Hz（协议文档 Frequency Table）
 *       Decibel 20-100（步进 5）
 *   SetStopVoice (14) 请求 { Device_Type(1) }
 *   应答：Flag + status
 *
 * ⚠ 7100 侧目前只标定了 6 个频点（500/1000/2000/3000/4000/6000），
 *   其余频点会回 Flag=1（见 docs/7100协议/纯音测听.md）。
 * 参考 remote_mic_rx_coex_1654/code/ble_rempro_cmd.c 的 cmd_setplayvoice()/cmd_setstopvoice()
 * （1654 走 BS300 的 ITG，这里改成 dsp_7100_play_tone()）。
 * ========================================================================== */

static const uint16_t s_audiometry_freq[17] = {
    250, 500, 1000, 1500, 2000, 2500, 3000, 3500,
    4000, 4500, 5000, 5500, 6000, 6500, 7000, 7500, 8000
};

/* ID:13  SetPlayVoice — 播放指定频点与声压级的纯音 */
static void cmd_setplayvoice_7100(const uint8_t *data, uint8_t len)
{
    uint8_t  spectrum;
    uint8_t  decibel;
    uint16_t freq_hz = 0;
    bool     ok = false;

    if (len < 3) { hdlc_response_set(CMD_SETPLAYVOICE, false); return; }

    spectrum = data[1];
    decibel  = data[2];

    if (spectrum < 17) freq_hz = s_audiometry_freq[spectrum];

    if (spectrum < 17 && decibel >= 20 && decibel <= 100) {
        ok = dsp_7100_play_tone(freq_hz, decibel);
    } else {
        PRINTF("[REMPRO] SetPlayVoice: 参数越界 spec=%u dB=%u\r\n", spectrum, decibel);
    }

    PRINTF("[REMPRO] SetPlayVoice: spec=%u dB=%u freq=%u started=%u\r\n",
           spectrum, decibel, freq_hz, ok);
    hdlc_response_set(CMD_SETPLAYVOICE, ok);
}

/* ID:14  SetStopVoice — 停止纯音 */
static void cmd_setstopvoice_7100(const uint8_t *data, uint8_t len)
{
    bool ok;

    (void)data;
    (void)len;

    ok = dsp_7100_stop_tone();
    PRINTF("[REMPRO] SetStopVoice: started=%u\r\n", ok);
    hdlc_response_set(CMD_SETSTOPVOICE, ok);
}

/* ID:26  GetDeviceConfig */
static void cmd_getdeviceconfig(void)
{
    uint8_t d[32];
    uint8_t pos = 0;

    /* Device_OnOff / Feedback_OnOff: only for Echo402BT, removed */

    d[pos++] = 1; d[pos++] = 0; d[pos++] = 0; d[pos++] = 0; /* Version 1.0.0.0 */
    d[pos++] = 4;  /* Program_Num（7100 程序 1-4） */

    /* Left side */
    memcpy(d + pos, bdaddr, 6); pos += 6;               /* Address_Left MAC */
    d[pos++] = 21; d[pos++] = 0;                         /* Product_Type = 21 */
    d[pos++] = 6;                                        /* Chip_Type = 6 (E7160SL/7100) */
    d[pos++] = 2;                                        /* Turn_Number */
    d[pos++] = 16;                                       /* Channel_Number */

    /* Right side (same as left) */
    memcpy(d + pos, bdaddr, 6); pos += 6;               /* Address_Right MAC */
    d[pos++] = 21; d[pos++] = 0;                         /* Product_Type = 21 */
    d[pos++] = 6;                                        /* Chip_Type = 6 (E7160SL/7100) */
    d[pos++] = 2;                                        /* Turn_Number */
    d[pos++] = 16;                                       /* Channel_Number */

    d[pos++] = 5;  /* Volume_Number（App 音量 0-5 = 6 档，对应 7100 档位 1-6） */

    PRINTF("[REMPRO] GetDeviceConfig\r\n");
    hdlc_response(CMD_GETDEVICECONFIG, 0, d, pos);
}

/* ID:78  IICDataCommunity — I2C / 1-wire data relay */
static void cmd_iicdatacommunity(const uint8_t *data, uint8_t len)
{
    /* min: Device_Type(1) + Data_Number(1) + Data_Length(2) + SUB_CMD_Type(1) = 5 */
    if (len < 5) { hdlc_response(CMD_IICDATACOMMUNITY, 1, NULL, 0); return; }

    uint8_t dev_type    = data[0];
    uint8_t data_number = data[1];
    uint8_t data_len_lo = data[2];
    uint8_t data_len_hi = data[3];
    uint8_t sub_cmd     = data[4];
    uint16_t sub_len    = (uint16_t)data_len_lo | ((uint16_t)data_len_hi << 8);

    if (data_number != 1) {
        PRINTF("[REMPRO] IICData: only Data_Number=1 supported, got %u\r\n",
               data_number);
        hdlc_response(CMD_IICDATACOMMUNITY, 1, NULL, 0);
        return;
    }

    if (sub_cmd < 1 || sub_cmd > 5) {
        PRINTF("[REMPRO] IICData: bad SUB_CMD_Type=%u\r\n", sub_cmd);
        hdlc_response(CMD_IICDATACOMMUNITY, 1, NULL, 0);
        return;
    }

    /* verify total length: header(5) + sub_len */
    if (len != (uint8_t)(5 + sub_len)) {
        PRINTF("[REMPRO] IICData: len=%u expected=%u\r\n", len, (uint8_t)(5 + sub_len));
        hdlc_response(CMD_IICDATACOMMUNITY, 1, NULL, 0);
        return;
    }

    PRINTF("[REMPRO] IICDataCommunity: dev=%u sub=%u dlen=%u\r\n",
           dev_type, sub_cmd, sub_len);

    /* echo back sub-command data for now — real I2C relay TBD */
    uint8_t resp[250];
    uint8_t pos = 0;
    resp[pos++] = dev_type;
    resp[pos++] = data_number;
    resp[pos++] = data_len_lo;
    resp[pos++] = data_len_hi;
    resp[pos++] = sub_cmd;
    if (sub_len > 0) {
        memcpy(resp + pos, data + 5, sub_len);
        pos += sub_len;
    }

    hdlc_response(CMD_IICDATACOMMUNITY, 0, resp, pos);
}

/* ID:87 SetFOTAStatus — 设置进入 FOTA 状态
 * 请求: [0]=Device_Type (0左右/1左/2右)
 * 响应: [0]=Flag(0成功/非0失败), [1]=status(0不支持/非0操作成功) */
static void cmd_fota_status(const uint8_t *data, uint8_t len)
{
    uint8_t resp[2];
#ifdef CFG_FOTA
    uint8_t dev_type = (len >= 1) ? data[0] : 0;
    resp[0] = 0;   /* Flag: 成功 */
    resp[1] = 1;   /* status: 操作成功 */
    PRINTF("[REMPRO] SetFOTAStatus: dev=%u enter FOTA\r\n", dev_type);
    hdlc_response(CMD_FOTA_STATUS, 0, resp, 2);
    Sys_Fota_StartDfu(1);   /* 进入 DFU（FOTA 子镜像接管） */
#else    /* ifdef CFG_FOTA */
    resp[0] = 1;   /* Flag: 失败 */
    resp[1] = 0;   /* status: 不支持（非 FOTA 固件） */
    PRINTF("[REMPRO] SetFOTAStatus: not supported (no CFG_FOTA)\r\n");
    hdlc_response(CMD_FOTA_STATUS, 0, resp, 2);
#endif    /* ifdef CFG_FOTA */
}

/* ================================================================
 * Main dispatcher
 * ================================================================ */
void rempro_cmd_process(void)
{
    if (!reasm_pending) return;
    reasm_pending = false;

    /* Process as many complete frames as we can */
    while (reasm_len >= 6) {   /* minimum frame: 7E SY CMDL CMDH FCS 7E = 6B */

        /* Skip leading garbage until we find 0x7E */
        if (reasm_buf[0] != HDLC_SEPARATOR) {
            uint8_t skip = 1;
            while (skip < reasm_len && reasm_buf[skip] != HDLC_SEPARATOR)
                skip++;
            PRINTF("[REMPRO] skipping %u leading bytes before 7E\r\n", skip);
            reasm_len -= skip;
            memmove(reasm_buf, reasm_buf + skip, reasm_len);
            if (reasm_len < 7) return;
        }

        uint16_t cmd_id;
        uint8_t data_len = 0, consumed = 0;
        const uint8_t *data = hdlc_parse_frame(reasm_buf, reasm_len,
                                               &cmd_id, &data_len, &consumed);
        if (consumed == 0) {
            /* No closing 7E found — wait for more chunks */
            PRINTF("[REMPRO] waiting: have %u bytes, no end 7E\r\n", reasm_len);
            return;
        }
        /* consumed > 0: frame parsed, data may be NULL if no payload */

        print_hex("RX frame", reasm_buf, consumed);
        PRINTF("[REMPRO] CMD=%u len=%u\r\n", cmd_id, data_len);

        switch (cmd_id) {
        /* ---- 已接 7100 运行时命令（切模式/调音量）---- */
        case CMD_SETVOLUME:
            if (data) cmd_setvolume_7100(data, data_len);
            else hdlc_response_set(CMD_SETVOLUME, false);
            break;
        case CMD_SETCURRENTSCENE:
            if (data) cmd_setcurrentscene_7100(data, data_len);
            else hdlc_response_set(CMD_SETCURRENTSCENE, false);
            break;
        case CMD_SETDENOISE:
            if (data) cmd_setdenoise_7100(data, data_len);
            else hdlc_response_set(CMD_SETDENOISE, false);
            break;
        case CMD_SETFEEDBACKONOFF:
            if (data) cmd_setfeedbackonoff_7100(data, data_len);
            else hdlc_response_set(CMD_SETFEEDBACKONOFF, false);
            break;
        case CMD_SETEQUALIZER:
            if (data) cmd_setequalizer_7100(data, data_len);
            else hdlc_response_set(CMD_SETEQUALIZER, false);
            break;
        case CMD_SETGAIN:
            if (data) cmd_setgain_7100(data, data_len);
            else hdlc_response_set(CMD_SETGAIN, false);
            break;
        case CMD_SETHIGHLEVELGAINDATA:
            if (data) cmd_sethighlevelgain_7100(data, data_len);
            else hdlc_response_set(CMD_SETHIGHLEVELGAINDATA, false);
            break;
        case CMD_SETMPO:
            if (data) cmd_setmpo_7100(data, data_len);
            else hdlc_response_set(CMD_SETMPO, false);
            break;
        case CMD_GETGAINDATA:
            if (data) cmd_getgaindata_7100(data, data_len);
            else hdlc_response(CMD_GETGAINDATA, 1, NULL, 0);
            break;
        case CMD_GETHIGHLEVELGAINDATA:
            if (data) cmd_gethighlevelgain_7100(data, data_len);
            else hdlc_response(CMD_GETHIGHLEVELGAINDATA, 1, NULL, 0);
            break;
        case CMD_GETMPODATA:
            if (data) cmd_getmpodata_7100(data, data_len);
            else hdlc_response(CMD_GETMPODATA, 1, NULL, 0);
            break;

        case CMD_SETMUTEDATA:
            if (data) cmd_setmutedata_7100(data, data_len);
            else hdlc_response_set(CMD_SETMUTEDATA, false);
            break;
        case CMD_SETAUDIOMETRYSTATUS:
            if (data) cmd_setaudiometrystatus(data, data_len);
            else hdlc_response_set(CMD_SETAUDIOMETRYSTATUS, false);
            break;

        case CMD_SETPLAYVOICE:
            if (data) cmd_setplayvoice_7100(data, data_len);
            else hdlc_response_set(CMD_SETPLAYVOICE, false);
            break;
        case CMD_SETSTOPVOICE:
            if (data) cmd_setstopvoice_7100(data, data_len);
            else hdlc_response_set(CMD_SETSTOPVOICE, false);
            break;

        /* ---- 设置类：待实现，回 Flag=1 + status=0（不支持）---- */
        case CMD_SETDEVICEONOFF:
        case CMD_SETCOMPRESSRATIO:
            PRINTF("[REMPRO] CMD=%u 待实现（7100 写路径）\r\n", cmd_id);
            hdlc_response_set(cmd_id, false);
            break;

        /* ---- 读取类：待实现，回 Flag=1 ---- */
        case CMD_GETFEEDBACKONOFF:
        case CMD_GETCURRENTSCENE:
        case CMD_GETFITTINGDATA:
            PRINTF("[REMPRO] CMD=%u 待实现（7100 读路径）\r\n", cmd_id);
            hdlc_response(cmd_id, 1, NULL, 0);
            break;

        /* ---- 与 DSP 无关，保持可用 ---- */
        case CMD_GETDEVICECONFIG:
            cmd_getdeviceconfig();
            break;
        case CMD_GETDEVICEONOFF:
            cmd_getdeviceonoff();
            break;
        case CMD_GETBATTERYINFO:
            cmd_getbatteryinfo_7100();
            break;
        case CMD_FOTA_STATUS:
            cmd_fota_status(data, data_len);
            break;
        case CMD_IICDATACOMMUNITY:
            if (data) cmd_iicdatacommunity(data, data_len);
            else hdlc_response(CMD_IICDATACOMMUNITY, 1, NULL, 0);
            break;
        default:
            PRINTF("[REMPRO] unknown CMD=%u\r\n", cmd_id);
            break;
        }

        /* Remove processed frame from buffer */
        reasm_len -= consumed;
        if (reasm_len > 0) memmove(reasm_buf, reasm_buf + consumed, reasm_len);
    }
}
