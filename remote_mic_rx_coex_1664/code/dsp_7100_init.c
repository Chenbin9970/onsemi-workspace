/* Ezairo 7100 通讯子系统（移植自 remote_mic_rx_coex，阶段一：通讯层）。
 *
 * dsp_7100_boot_init()     ：开机同步跑「首个 A7 之前」的引导步。
 * dsp_7100_rb_seq_tick()   ：200ms tick 调，推进 4 程序×(WDRC/DFBC/降噪) 读回；
 *                            一轮完成后解析打印并停止。
 *
 * 读路径只读不写；写路径（动态 A7 编码 / 会话状态机）留待阶段二。
 * 延时 ≥1ms 分段喂狗。 */

#include "dsp_7100_init.h"
#include "dsp_7100_storage.h"
#include "app.h"
#include "i2c_7100_hal.h"
#include <printf.h>
#include <string.h>

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

/* µs 忙等延时：保留亚 ms 级间隔。≥1ms 时每 1ms 喂一次狗。 */
static void delay_us_feed(uint32_t us)
{
    uint64_t left   = (uint64_t)us * (uint64_t)(SystemCoreClock / 1000000UL);
    const uint64_t chunk = SystemCoreClock / 1000UL;

    while (left > 0) {
        if (left >= chunk) {
            Sys_Watchdog_Refresh();
            Sys_Delay_ProgramROM((uint32_t)chunk);
            left -= chunk;
        } else {
            Sys_Delay_ProgramROM((uint32_t)left);
            left = 0;
        }
    }
}

static bool is_a7_query(const dsp_init_step_t *s)
{
    return (s->op == DSP_INIT_TX) && (s->len >= 5) && (s->data[0] == 0xA7);
}

/* 拼成一行的十六进制 dump（超过 DSP_DUMP_MAX 截断），一次 PRINTF 输出。
 * 逐字节 PRINTF 会因 UART DMA 阻塞而拖慢 106 步引导。
 * ⚠ pack printf.c 的 vsprintf 写 200B 静态缓冲且无边界检查 ——
 *   DSP_DUMP_MAX×3+4 必须远小于 200（32 → 100 字符）。 */
#define DSP_DUMP_MAX    32
static void dump_hex(const uint8_t *p, uint16_t len)
{
    static const char hexd[] = "0123456789ABCDEF";
    char line[DSP_DUMP_MAX * 3 + 4];
    uint16_t n = (len > DSP_DUMP_MAX) ? DSP_DUMP_MAX : len;
    uint16_t w = 0;

    for (uint16_t i = 0; i < n; i++) {
        line[w++] = ' ';
        line[w++] = hexd[p[i] >> 4];
        line[w++] = hexd[p[i] & 0x0Fu];
    }
    if (len > n) {
        line[w++] = ' ';
        line[w++] = '.';
        line[w++] = '.';
    }
    line[w] = '\0';
    PRINTF("%s", line);
}

void dsp_7100_boot_init(void)
{
    uint16_t n = dsp_init_step_cnt;
#if (DSP7100_INIT_MAX_STEPS > 0)
    if (n > (uint16_t)DSP7100_INIT_MAX_STEPS) n = (uint16_t)DSP7100_INIT_MAX_STEPS;
#endif
    PRINTF("[7100-init] boot init, steps=%u (同步 A7 前的普通步)\r\n", n);

    /* 同步跑首个 A7 之前的所有普通步（A7 不在这里处理） */
    for (uint16_t i = 0; i < n; i++) {
        const dsp_init_step_t *st = &dsp_init_steps[i];
        if (is_a7_query(st)) break;

        delay_us_feed(st->delay_us);
        if (st->op == DSP_INIT_TX) {
            bool ok = i2c_7100_write(I2C_7100_ADDR, st->data, st->len);
            PRINTF("[7100-init] %u/%u TX len=%u ok=%u:", i + 1, n, st->len, ok);
            dump_hex(st->data, st->len);
            PRINTF("\r\n");
        } else {
            uint8_t rx[DSP_INIT_RX_BUF];
            bool ok = i2c_7100_read(I2C_7100_ADDR, rx, st->len);
            PRINTF("[7100-init] %u/%u RX len=%u ok=%u:", i + 1, n, st->len, ok);
            dump_hex(rx, st->len);
            PRINTF("\r\n");
        }
    }
    PRINTF("[7100-init] sync pre-A7 done\r\n");
}

/* ---- 4 程序×(降噪/DFBC/WDRC) 读回：读到即解析成参数，不保留原始块 ---- */
#define RB_PROGS      4
#define RB_CH         16
#define RB_LL_BASE    414        /* WDRC payload 内 ch0 LowLevelGain bit 起点 */
#define RB_CH_STEP    147        /* 每通道 bit 间隔 */
#define RB_LL_OFF     0          /* 通道单元内 LowLevelGain 位偏移 */
#define RB_HL_OFF     14         /* HighLevelGain  8bit 有符号 */
#define RB_OL_OFF     22         /* OutputLimit    8bit 有符号 */

static dsp_7100_rb_bufs_t s_rb_bufs;

enum { RB_SEND, RB_READ };
static uint8_t  s_rb_st  = RB_SEND;
static uint16_t s_rb_idx = 0;
static uint8_t  s_rb_done = 0;
static uint8_t  s_rb_needed = 1;    /* 1 = 需走 I2C 读回（缓存未命中） */
static uint8_t  s_save_pending = 0; /* 1 = 读回完成，待主循环落盘 */
static const uint8_t s_end82[] = { 0x82 };

dsp_7100_rb_bufs_t *dsp_7100_rb_bufs(void)
{
    return &s_rb_bufs;
}

/* payload 内取 bit（MSB-first）；nbits==7/8 按有符号补码 */
static int8_t rb_field(const uint8_t *p, int32_t bit, uint8_t nbits)
{
    int32_t v = 0;
    uint8_t k;
    for (k = 0; k < nbits; k++) {
        int32_t b = bit + k;
        v = (v << 1) | ((p[b >> 3] >> (7 - (b & 7))) & 1u);
    }
    if (nbits == 7 && v >= 64) v -= 128;
    if (nbits == 8 && v >= 128) v -= 256;
    return (int8_t)v;
}

/* WDRC 通道单元起点（payload 坐标系，ch 从 0 起）→ 该通道 LowLevelGain 位 */
static int32_t rb_ch_base(uint8_t ch)
{
    return RB_LL_BASE + (int32_t)ch * RB_CH_STEP - RB_CH_STEP;
}

/* 解析 WDRC 块（375B）→ LL / HL / OL ×16。
 * 字段定义见 docs/7100协议/WDRC/7100_WDRC读取.md §6。 */
static void rb_parse_wdrc(uint8_t prog, const uint8_t *wp)
{
    dsp_7100_prog_t *p = &s_rb_bufs.prog[prog];
    uint8_t ch;
    for (ch = 0; ch < RB_CH; ch++) {
        int32_t base = rb_ch_base(ch);
        p->wdrc_ll[ch] = rb_field(wp, base + RB_LL_OFF, 7);
        p->wdrc_hl[ch] = rb_field(wp, base + RB_HL_OFF, 8);
        p->wdrc_ol[ch] = rb_field(wp, base + RB_OL_OFF, 8);
    }
}

/* 解析 DFBC 块（306B）→ 开关（payload[0] bit7，其余字节恒同） */
static void rb_parse_dfbc(uint8_t prog, const uint8_t *dp)
{
    s_rb_bufs.prog[prog].dfbc_en = (dp[0] & 0x80u) ? 1u : 0u;
}

/* 解析降噪块（174B）→ 使能 + 档位（payload[0]：bit7 使能，其下 4bit = 3×(档位+1)） */
static void rb_parse_noise(uint8_t prog, const uint8_t *np)
{
    dsp_7100_prog_t *p = &s_rb_bufs.prog[prog];
    uint8_t v = (uint8_t)((np[0] >> 3) & 0x0Fu);

    p->denoise_en  = (np[0] & 0x80u) ? 1u : 0u;
    p->denoise_lvl = (v >= 3u) ? (uint8_t)(v / 3u - 1u) : 0u;
}

static void rb_parse_print(void)
{
    uint8_t prog;

    for (prog = 0; prog < RB_PROGS; prog++) {
        const dsp_7100_prog_t *p = &s_rb_bufs.prog[prog];

        PRINTF("[RB] P%u 降噪 en=%u lvl=%u | DFBC=%s | WDRC LL/HL/OL:",
               prog + 1, p->denoise_en, p->denoise_lvl,
               p->dfbc_en ? "on" : "off");
        for (uint8_t ch = 0; ch < RB_CH; ch++) {
            PRINTF(" %d/%d/%d", p->wdrc_ll[ch], p->wdrc_hl[ch], p->wdrc_ol[ch]);
        }
        PRINTF("\r\n");
    }
}

void dsp_7100_rb_seq_tick(void)
{
    const dsp_a7_cmd_t *g;
    uint8_t hdr[3];
    uint8_t rx[DSP_INIT_RX_BUF];
    uint16_t dlen, hlen, rd;
    bool okh, okp = true;
    bool pass = false;

    if (!s_rb_needed) return;      /* flash 缓存命中，无需 I2C 读回 */
    if (s_rb_done) return;
    if (dsp_rb_cmd_cnt == 0) return;
    g = &dsp_rb_cmds[s_rb_idx];
    dlen = (uint16_t)g->wr[3] + ((uint16_t)g->wr[4] << 8);

    if (s_rb_st == RB_SEND) {
        bool okw = i2c_7100_write(I2C_7100_ADDR, g->wr, g->wl);
        (void)okw;
        PRINTF("[RB] %u/%u TX ok=%u (dlen=%u)\r\n", s_rb_idx + 1,
               dsp_rb_cmd_cnt, okw, dlen);
        s_rb_st = RB_READ;
        return;
    }

    okh = i2c_7100_read(I2C_7100_ADDR, hdr, sizeof(hdr));
    hlen = (uint16_t)hdr[1] + ((uint16_t)hdr[2] << 8);
    rd = 0;
    if (hlen > 0) {
        rd = (hlen > DSP_INIT_RX_BUF) ? DSP_INIT_RX_BUF : hlen;
        okp = i2c_7100_read(I2C_7100_ADDR, rx, rd);
    }
    i2c_7100_write(I2C_7100_ADDR, s_end82, sizeof(s_end82));   /* 04 82 收尾 */

    if (okh && okp && hdr[0] == 0x46 && hlen == dlen) pass = true;

    /* 每程序 7 条命令：0=选程序 1/3/5=选模块 2/4/6=读 WDRC/DFBC/降噪。
     * 读到即解析成参数，原始块读完即弃。 */
    if (pass && rd >= dlen) {
        uint8_t prog = (uint8_t)(s_rb_idx / 7u);
        uint8_t off  = (uint8_t)(s_rb_idx % 7u);

        if (off == 2) {
            rb_parse_wdrc(prog, rx);        /* read 77 01 → LL/HL/OL */
        } else if (off == 4) {
            rb_parse_dfbc(prog, rx);        /* read 32 01 → 开关 */
        } else if (off == 6) {
            rb_parse_noise(prog, rx);       /* read AE 00 → 使能/档位，本程序解析完 */
            s_rb_bufs.valid[prog] = 1;
        }
    }

    PRINTF("[RB] %u/%u HDR %02X %02X %02X dlen=%u DATA%uB ok=%u %s\r\n",
           s_rb_idx + 1, dsp_rb_cmd_cnt, hdr[0], hdr[1], hdr[2], dlen,
           rd, okp, pass ? "PASS->next" : "retry");

    if (pass) {
        s_rb_idx++;
        if (s_rb_idx >= dsp_rb_cmd_cnt) {   /* 一轮完成，停止并解析 */
            s_rb_done = 1;
            s_save_pending = 1;             /* 交主循环落盘（勿在定时器上下文擦写 flash） */
            PRINTF("[RB] round done, parse:\r\n");
            rb_parse_print();
            return;
        }
        s_rb_st = RB_SEND;
    }
}

/* ---- 读回结果查询（供上层取用；阶段二 Rempro 会用到）---- */
bool dsp_7100_rb_done(void)
{
    return s_rb_done != 0;
}

bool dsp_7100_prog_valid(uint8_t prog)
{
    if (prog >= RB_PROGS) return false;
    return s_rb_bufs.valid[prog] != 0;
}

const dsp_7100_prog_t *dsp_7100_get_prog(uint8_t prog)
{
    if (prog >= RB_PROGS || !s_rb_bufs.valid[prog]) return NULL;
    return &s_rb_bufs.prog[prog];
}

/* ---- flash 缓存：开机读一次，读全落盘，之后开机直接用 ---- */
bool dsp_7100_rb_needed(void)
{
    return s_rb_needed != 0;
}

void dsp_7100_cache_try_load(void)
{
    if (dsp_7100_cache_load()) {
        s_rb_needed = 0;
        s_rb_done   = 1;        /* 视同读回已完成 */
        PRINTF("[7100-cache] hit: 用 flash 缓存，跳过 I2C 读回\r\n");
        rb_parse_print();       /* 缓存命中：把存的参数打印出来 */
    } else {
        s_rb_needed = 1;
        PRINTF("[7100-cache] miss: 走 I2C 读回，完成后落盘\r\n");
    }
}

/* 运行时改了参数后请求落盘（实际擦写留给主循环 process_deferred） */
void dsp_7100_cache_save_request(void)
{
    s_save_pending = 1;
}

/* 主循环调：读回跑完一轮 → 落盘（flash 擦写阻塞且关中断，不能放定时器上下文） */
void dsp_7100_process_deferred(void)
{
    if (!s_save_pending) return;
    s_save_pending = 0;

    if (dsp_7100_cache_save()) {
        PRINTF("[7100-cache] saved to flash\r\n");
    } else {
        PRINTF("[7100-cache] save FAIL\r\n");
    }
}
