/* 7100 运行时命令层。
 * 波形解析（program1-4.csv / volume.csv）：
 *   写帧  A2 00 <reg> <val>        （reg: 16=程序, 12=音量）
 *   读回  43 03 00 00 <reg> <val>   （从机确认）
 *   结束  82
 * 调音量 = 写帧 → 读确认 → 82（3 帧）
 * 切程序 = 写帧 → 读确认 → 82 → 读状态 → 82（5 帧，对照 program1 波形） */

#include "dsp_7100_cmd.h"
#include "app.h"
#include "i2c_7100_hal.h"

#include <string.h>

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

#define DSP7100_CMD_PARAM_WRITE  0xA2
#define DSP7100_REG_PROGRAM      0x16
#define DSP7100_REG_VOLUME       0x12
#define DSP7100_CMD_END          0x82
#define DSP7100_RX_LEN           6

/* 6 档音量 → 0-100 值：round(档位 * 100 / 6) = 17/33/50/67/83/100 */
static const uint8_t s_volume_value[6] = {0x11, 0x21, 0x32, 0x43, 0x53, 0x64};

static uint8_t s_cur_prog = 1;
static uint8_t s_cur_vol_level = 6;

static bool dsp_7100_write_cmd(uint8_t reg, uint8_t value)
{
    uint8_t wr[4] = {DSP7100_CMD_PARAM_WRITE, 0x00, reg, value};
    bool ok = i2c_7100_write(I2C_7100_ADDR, wr, sizeof(wr));
    PRINTF("[7100] TX: %02X %02X %02X %02X (ok=%u)\r\n",
           wr[0], wr[1], wr[2], wr[3], ok);
    return ok;
}

static bool dsp_7100_read6(uint8_t *rx)
{
    bool ok = i2c_7100_read(I2C_7100_ADDR, rx, DSP7100_RX_LEN);
    PRINTF("[7100] rd: %02X %02X %02X %02X %02X %02X (ok=%u)\r\n",
           rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], ok);
    return ok;
}

/* 调音量：写帧 → 2ms → 读确认 → 1ms → 82 */
bool dsp_7100_set_volume(uint8_t level)
{
    uint8_t rx[DSP7100_RX_LEN];
    uint8_t end = DSP7100_CMD_END;

    if (level < 1 || level > 6) return false;
    if (!dsp_7100_write_cmd(DSP7100_REG_VOLUME, s_volume_value[level - 1])) return false;
    i2c_7100_delay_ms(2);
    if (!dsp_7100_read6(rx)) return false;
    i2c_7100_delay_ms(1);
    if (!i2c_7100_write(I2C_7100_ADDR, &end, sizeof(end))) return false;
    s_cur_vol_level = level;
    return true;
}

/* 切程序（对照 program1 完整 5 帧）：
 * 写帧 → 80ms → 读确认 → 1ms → 82 → 1ms → 读状态 → 1ms → 82 */
bool dsp_7100_switch_program(uint8_t prog)
{
    uint8_t rx[DSP7100_RX_LEN];
    uint8_t end = DSP7100_CMD_END;

    if (prog < 1 || prog > 4) return false;
    if (!dsp_7100_write_cmd(DSP7100_REG_PROGRAM, prog)) return false;
    i2c_7100_delay_ms(80);
    if (!dsp_7100_read6(rx)) return false;
    i2c_7100_delay_ms(1);
    if (!i2c_7100_write(I2C_7100_ADDR, &end, sizeof(end))) return false;
    i2c_7100_delay_ms(1);
    if (!dsp_7100_read6(rx)) return false;
    i2c_7100_delay_ms(1);
    if (!i2c_7100_write(I2C_7100_ADDR, &end, sizeof(end))) return false;
    s_cur_prog = prog;
    return true;
}

uint8_t dsp_7100_get_program(void)      { return s_cur_prog; }
uint8_t dsp_7100_get_volume_level(void) { return s_cur_vol_level; }

/* ============================================================================
 * 7100 A7 参数读回（开机一次性，DSP7100_READBACK_ENABLE 控制）。
 * 帧序列对照 docs/7100协议/ 降噪/DFBC/WDRC 读取文档（参考机全量读抓包）。
 * 每条 A7 事务 = 写命令 → 延时 → 读响应 → 04 82 收尾。
 * ========================================================================== */

#if defined(DSP7100_READBACK_ENABLE)

#define RB_RX_SHORT      3      /* 短响应 46 00 00（选程序/静音等） */
#define RB_PAY_OFF       3      /* 块读响应: 46 <blklo> <blkhi> + payload[0] */
#define RB_BUF_LEN       384    /* >= WDRC 0x177 读长 378 */
#define RB_LL_BIT_BASE   414    /* WDRC payload 内 ch2 LowLevelGain 起始 bit */
#define RB_CH_BIT_STEP   147    /* WDRC 每通道 bit 间隔 */
#define RB_TX_GAP_MS     60     /* 相邻 A7 事务间隔（参考抓包 40-120ms） */

dsp7100_prog_read_t g_dsp7100_read[DSP7100_READBACK_PROGS];

typedef struct {
    uint8_t  sub;       /* A7 03 ... 37 <sub> <P> 子 id */
    uint8_t  blo;       /* 读块地址低字节（0x77/0x32/0xAE） */
    uint8_t  bhi;       /* 读块地址高字节 */
    uint16_t rlen;      /* 读响应数据字节数 */
} rb_blk_t;

static const rb_blk_t rb_wdrc = {0x07, 0x77, 0x01, 378};
static const rb_blk_t rb_dfbc = {0x0A, 0x32, 0x01, 309};
static const rb_blk_t rb_noi  = {0x09, 0xAE, 0x00, 177};

/* 一条 A7 事务：事务间隔 → 写命令 wl 字节 → 延时 dms → 读 rl 字节 → 04 82 收尾。 */
static bool rb_cmd(uint16_t dms, const uint8_t *wr, uint16_t wl,
                   uint8_t *rx, uint16_t rl)
{
    static const uint8_t end = 0x82;
    bool ok;

    i2c_7100_delay_ms(RB_TX_GAP_MS);          /* 相邻发包间隔对齐参考 40-120ms */
    if (!i2c_7100_write(I2C_7100_ADDR, wr, wl)) return false;
    i2c_7100_delay_ms(dms);
    ok = i2c_7100_read(I2C_7100_ADDR, rx, rl);
    i2c_7100_delay_ms(3);
    i2c_7100_write(I2C_7100_ADDR, &end, 1);
    return ok;
}

/* 选程序 P（P = 程序号 + 1）：A7 02 00 00 00 12 P */
static bool rb_sel_prog(uint8_t p)
{
    uint8_t w[7] = {0xA7, 0x02, 0x00, 0x00, 0x00, 0x12, p};
    uint8_t rx[RB_RX_SHORT];
    return rb_cmd(20, w, sizeof(w), rx, sizeof(rx));
}

/* 选中模块 blk->sub（A7 03 ... 37 <sub> <P>）后读块（A7 01 00 blo bhi 38）。 */
static bool rb_read_block(const rb_blk_t *blk, uint8_t p, uint8_t *rx)
{
    uint8_t w[8];
    uint8_t ack[RB_RX_SHORT];

    w[0] = 0xA7; w[1] = 0x03; w[2] = 0x00; w[3] = 0x00;
    w[4] = 0x00; w[5] = 0x37; w[6] = blk->sub; w[7] = p;
    if (!rb_cmd(20, w, sizeof(w), ack, sizeof(ack))) return false;

    w[0] = 0xA7; w[1] = 0x01; w[2] = 0x00;
    w[3] = blk->blo; w[4] = blk->bhi; w[5] = 0x38;
    return rb_cmd(100, w, 6, rx, blk->rlen);   /* 大块读前等 100ms 让 DSP 准备响应 */
}

/* 从 payload p 取 bit 起 nbits（字节内 MSB-first）；nbits==7 时按有符号补码。 */
static int8_t rb_field(const uint8_t *p, int32_t bit, uint8_t nbits)
{
    int32_t v = 0;
    uint8_t k;

    for (k = 0; k < nbits; k++) {
        int32_t b = bit + k;
        v = (v << 1) | ((p[b >> 3] >> (7 - (b & 7))) & 1u);
    }
    if (nbits == 7 && v >= 64) v -= 128;
    return (int8_t)v;
}

/* WDRC 0x177 payload → 各通道 LowLevel/HighLevelGain（ch1..16）。 */
static void rb_parse_wdrc(const uint8_t *rx, dsp7100_prog_read_t *out)
{
    const uint8_t *p = rx + RB_PAY_OFF;
    uint8_t n;

    for (n = 0; n < DSP7100_WDRC_CHANNELS; n++) {
        int32_t base = RB_LL_BIT_BASE + (int32_t)n * RB_CH_BIT_STEP - RB_CH_BIT_STEP;
        out->wdrc_low[n]  = rb_field(p, base, 7);
        out->wdrc_high[n] = rb_field(p, base + 15, 7);
    }
}

/* 读回单个程序 P：选程序 → 读 0x03 → 静音 → 逐块读(尽量全试) → 解除静音。
 * 每块失败打印实际读回的 rx 头/read ok，便于定位；valid=1 仅三块 header 全对。 */
static bool rb_read_prog(uint8_t p, dsp7100_prog_read_t *out)
{
    static const uint8_t poll03[] = {0xA7, 0x01, 0x00, 0x03, 0x00, 0x02};
    static const uint8_t mute[]   = {0xA7, 0x01, 0x00, 0x00, 0x00, 0x25};
    static const uint8_t unmute[] = {0xA7, 0x01, 0x00, 0x00, 0x00, 0x26};
    uint8_t rx[RB_BUF_LEN];
    uint8_t ack[RB_RX_SHORT];
    bool rd, wok, dok, nok;

    memset(out, 0, sizeof(*out));
    if (!rb_sel_prog(p)) { PRINTF("[RB] P%u sel FAIL\r\n", p); return false; }
    rd = rb_cmd(20, poll03, sizeof(poll03), rx, 6);      /* 选程序后读 0x03（参考会话有） */
    if (!rd || rx[0] != 0x46 || rx[1] != 0x03)
        PRINTF("[RB] P%u poll03 rd=%u rx=%02X %02X %02X\r\n", p, rd, rx[0], rx[1], rx[2]);
    if (!rb_cmd(20, mute, sizeof(mute), ack, sizeof(ack))) { PRINTF("[RB] P%u mute FAIL\r\n", p); return false; }

    rd = rb_read_block(&rb_wdrc, p, rx);
    wok = rd && rx[0] == 0x46 && rx[1] == 0x77 && rx[2] == 0x01;
    if (wok) rb_parse_wdrc(rx, out);
    else PRINTF("[RB] P%u WDRC rd=%u rx=%02X %02X %02X %02X %02X %02X\r\n",
                p, rd, rx[0], rx[1], rx[2], rx[3], rx[4], rx[5]);

    rd = rb_read_block(&rb_dfbc, p, rx);
    dok = rd && rx[0] == 0x46 && rx[1] == 0x32 && rx[2] == 0x01;
    if (dok) out->dfbc_on = (rx[RB_PAY_OFF] & 0x80u) ? 1u : 0u;
    else PRINTF("[RB] P%u DFBC rd=%u rx=%02X %02X %02X %02X %02X %02X\r\n",
                p, rd, rx[0], rx[1], rx[2], rx[3], rx[4], rx[5]);

    rd = rb_read_block(&rb_noi, p, rx);
    nok = rd && rx[0] == 0x46 && rx[1] == 0xAE && rx[2] == 0x00;
    if (nok) {
        uint8_t v = (rx[RB_PAY_OFF] >> 3) & 0x0Fu;   /* 48×4bit 首组 = 3×(档位+1) */
        out->noise_en    = (rx[RB_PAY_OFF] & 0x80u) ? 1u : 0u;
        out->noise_level = (v >= 3u) ? (uint8_t)((v / 3u) - 1u) : 0u;
    } else PRINTF("[RB] P%u NOISE rd=%u rx=%02X %02X %02X %02X %02X %02X\r\n",
                  p, rd, rx[0], rx[1], rx[2], rx[3], rx[4], rx[5]);

    rb_cmd(20, unmute, sizeof(unmute), ack, sizeof(ack));
    out->valid = (wok && dok && nok) ? 1u : 0u;
    return (bool)out->valid;
}

/* 打印某程序 WDRC 各通道 Low/High 值。 */
static void rb_print_wdrc(uint8_t i)
{
    const dsp7100_prog_read_t *r = &g_dsp7100_read[i];
    uint8_t n;

    PRINTF("[RB] prog%u WDRC Low :", i);
    for (n = 0; n < DSP7100_WDRC_CHANNELS; n++) PRINTF(" %d", r->wdrc_low[n]);
    PRINTF("\r\n[RB] prog%u WDRC High:", i);
    for (n = 0; n < DSP7100_WDRC_CHANNELS; n++) PRINTF(" %d", r->wdrc_high[n]);
    PRINTF("\r\n");
}

/* 开机一次性读回 4 程序 × (降噪/DFBC/WDRC)，结果入 g_dsp7100_read 并打印。 */
void dsp_7100_readback_all(void)
{
    uint8_t i;

    PRINTF("\r\n[RB] 7100 A7 readback start\r\n");
    i2c_7100_delay_ms(200);   /* 上电 init 后留 200ms 让 7100 稳定 */
    for (i = 0; i < DSP7100_READBACK_PROGS; i++) {
        bool ok = rb_read_prog((uint8_t)(i + 1), &g_dsp7100_read[i]);
        PRINTF("[RB] prog%u %s: noise=%s lvl=%u dfbc=%s\r\n",
               i, ok ? "ok" : "FAIL",
               g_dsp7100_read[i].noise_en ? "on" : "off",
               g_dsp7100_read[i].noise_level,
               g_dsp7100_read[i].dfbc_on ? "on" : "off");
        rb_print_wdrc(i);
    }
    /* 会话尾部复位程序指针（参考读抓包亦回 P=01） */
    rb_sel_prog(1);
    PRINTF("[RB] done\r\n");
}

#endif /* DSP7100_READBACK_ENABLE */

