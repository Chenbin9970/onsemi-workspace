/* 7100 上电初始化 + 7 组 A7 推进。
 *
 * dsp_7100_boot_init()：同步跑"首个 A7 之前"的普通步（握手写+读配置+A1/A2 参数）。
 * dsp_7100_a7_seq_tick()：200ms tick 调 —— 推进 7 组 A7，每组连续重发直到
 *   读回逐字节全等，才 04 82 进下一条；7 组走完循环。
 * 延时 ≥1ms 分段喂狗。 */

#include "dsp_7100_init.h"
#include "app.h"
#include "i2c_7100_hal.h"
#include <printf.h>

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

/* ---- DIO13 上升沿中断（逐步加） ---- */
static volatile bool s_a7_rdy = false;
static bool s_irq_on = false;

void DIO3_IRQHandler(void)
{
    s_a7_rdy = true;
}

/* 使能 DIO13 上升沿中断(index3→DIO3_IRQn)，清标志 */
void dsp_7100_a7_arm(void)
{
    Sys_DIO_Config(13, DIO_MODE_INPUT | DIO_WEAK_PULL_UP | DIO_LPF_DISABLE);
    if (!s_irq_on) {
        Sys_DIO_IntConfig(3, DIO_DEBOUNCE_DISABLE | DIO_SRC_DIO_13 |
                              DIO_EVENT_RISING_EDGE, 0, 0);
        NVIC_SetPriority(DIO3_IRQn, 3);
        NVIC_EnableIRQ(DIO3_IRQn);
        s_irq_on = true;
    }
    s_a7_rdy = false;
    NVIC_ClearPendingIRQ(DIO3_IRQn);
}

/* 主循环每圈：检测 DIO13 上升沿标志（先只打印，确认能触发） */
bool dsp_7100_a7_poll(void)
{
    if (!s_a7_rdy) return false;
    s_a7_rdy = false;
    PRINTF("[A7-06] DIO13 rising\r\n");
    return true;
}

/* ---- 7 组 A7 推进：两段读校验 ----
 * 写端 A7 的字节 3/4 = 数据长度(len16)；读时分两段：
 *   ① 先读 3 字节头(46 <len_lo> <len_hi>)；
 *   ② 头长度字段 == 写端长度 → 再读该长度数据，即算本组通过。 */
typedef struct {
    const uint8_t *wr;   uint8_t wl;
} a7_grp_t;

static const uint8_t g1w[] = {0xA7,0x01,0x00,0x06,0x00,0x19};
static const uint8_t g2w[] = {0xA7,0x01,0x00,0x02,0x00,0x03};
static const uint8_t g3w[] = {0xA7,0x01,0x00,0x03,0x00,0x02};
static const uint8_t g4w[] = {0xA7,0x01,0x00,0x26,0x00,0x20};
static const uint8_t g5w[] = {0xA7,0x01,0x00,0x0A,0x00,0x30};
static const uint8_t g6w[] = {0xA7,0x01,0x00,0x02,0x00,0x03};
static const uint8_t g7w[] = {0xA7,0x01,0x00,0x0C,0x00,0x28};

static const a7_grp_t s_groups[] = {
    { g1w, sizeof(g1w) }, { g2w, sizeof(g2w) }, { g3w, sizeof(g3w) },
    { g4w, sizeof(g4w) }, { g5w, sizeof(g5w) }, { g6w, sizeof(g6w) },
    { g7w, sizeof(g7w) },
};

enum { A7_SEQ_SEND, A7_SEQ_READ };
static uint8_t  s_seq_st  = A7_SEQ_SEND;
static uint8_t  s_seq_idx = 0;
static const uint8_t s_end82[] = { 0x82 };

static uint16_t grp_dlen(const a7_grp_t *g)
{
    return (uint16_t)g->wr[3] + ((uint16_t)g->wr[4] << 8);
}

/* 200ms tick 调：A7 只发一次；随后两段读校验：
 *   读3头 → 头长度==写端长度则读数据并本组通过(进下一组发新A7)；否则不重发A7，
 *   下个 tick 继续两段读(每次读后 04 82)。循环。 */
void dsp_7100_a7_seq_tick(void)
{
    const a7_grp_t *g = &s_groups[s_seq_idx];
    uint8_t hdr[3];
    uint8_t rx[DSP_INIT_RX_BUF];
    uint16_t dlen = grp_dlen(g);
    uint16_t hlen;
    bool okh, okp;
    bool pass = false;
    uint8_t k;

    if (s_seq_st == A7_SEQ_SEND) {
        bool okw = i2c_7100_write(I2C_7100_ADDR, g->wr, g->wl);
        (void)okw;
        PRINTF("[A7] G%u TX ok=%u (len=%u)\r\n", s_seq_idx + 1, okw, dlen);
        s_seq_st = A7_SEQ_READ;
        return;
    }

    /* 段① 读 3 字节头 */
    okh = i2c_7100_read(I2C_7100_ADDR, hdr, sizeof(hdr));
    hlen = (uint16_t)hdr[1] + ((uint16_t)hdr[2] << 8);
    PRINTF("[A7] G%u HDR ok=%u: %02X %02X %02X (expect len=%u)\r\n",
           s_seq_idx + 1, okh, hdr[0], hdr[1], hdr[2], dlen);

    okp = true;
    if (hlen > 0) {
        /* 段② 读该长度数据（无论头是否匹配都读掉，避免残留） */
        uint16_t rd = (hlen > DSP_INIT_RX_BUF) ? DSP_INIT_RX_BUF : hlen;
        okp = i2c_7100_read(I2C_7100_ADDR, rx, rd);
        PRINTF("[A7] G%u DATA(%uB ok=%u):", s_seq_idx + 1, rd, okp);
        for (k = 0; k < rd; k++) PRINTF(" %02X", rx[k]);
        PRINTF("\r\n");
    }

    i2c_7100_write(I2C_7100_ADDR, s_end82, sizeof(s_end82));   /* 04 82 结束 */

    /* 通过条件：状态46 且 头长度==写端长度（数据读满即算本组过） */
    if (okh && hdr[0] == 0x46 && hlen == dlen) pass = true;

    if (pass) {
        PRINTF("[A7] G%u pass -> next\r\n", s_seq_idx + 1);
        s_seq_idx = (s_seq_idx + 1) % (uint8_t)(sizeof(s_groups) /
                                                sizeof(s_groups[0]));
        s_seq_st = A7_SEQ_SEND;
    }
    /* 不通过：不重发 A7，下个 tick 继续两段读 */
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
            (void)ok;
        } else {
            uint8_t rx[DSP_INIT_RX_BUF];
            bool ok = i2c_7100_read(I2C_7100_ADDR, rx, st->len);
            (void)ok;
        }
    }
    PRINTF("[7100-init] sync pre-A7 done\r\n");
}

/* ---- parm1604 推进：单次连续读（3 头 + 数据，一次总线读不 STOP），命令来自 dsp_parm_cmds ---- */
enum { PARM_SEND, PARM_READ };
static uint8_t  s_parm_st  = PARM_SEND;
static uint16_t s_parm_idx = 0;

void dsp_7100_parm_seq_tick(void)
{
    const dsp_a7_cmd_t *g;
    uint8_t hdr[3];
    uint8_t rx[DSP_INIT_RX_BUF];
    uint16_t dlen, hlen, rd;
    bool okh, okp = true;
    bool pass = false;

    if (dsp_parm_cmd_cnt == 0) return;
    g = &dsp_parm_cmds[s_parm_idx];
    dlen = (uint16_t)g->wr[3] + ((uint16_t)g->wr[4] << 8);

    if (s_parm_st == PARM_SEND) {
        bool okw = i2c_7100_write(I2C_7100_ADDR, g->wr, g->wl);
        (void)okw;
        PRINTF("[PARM] %u/%u TX ok=%u (dlen=%u)\r\n", s_parm_idx + 1,
               dsp_parm_cmd_cnt, okw, dlen);
        s_parm_st = PARM_READ;
        return;
    }

    /* 段① 读 3B 头 */
    okh = i2c_7100_read(I2C_7100_ADDR, hdr, sizeof(hdr));
    hlen = (uint16_t)hdr[1] + ((uint16_t)hdr[2] << 8);

    /* 段② 按头长读数据（防残留也读掉） */
    rd = 0;
    if (hlen > 0) {
        rd = (hlen > DSP_INIT_RX_BUF) ? DSP_INIT_RX_BUF : hlen;
        okp = i2c_7100_read(I2C_7100_ADDR, rx, rd);
    }
    i2c_7100_write(I2C_7100_ADDR, s_end82, sizeof(s_end82));   /* 04 82 */

    if (okh && okp && hdr[0] == 0x46 && hlen == dlen) pass = true;
    PRINTF("[PARM] %u/%u HDR %02X %02X %02X dlen=%u DATA%uB ok=%u %s\r\n",
           s_parm_idx + 1, dsp_parm_cmd_cnt, hdr[0], hdr[1], hdr[2], dlen,
           rd, okp, pass ? "PASS->next" : "retry");

    if (pass) {
        s_parm_idx = (uint16_t)((s_parm_idx + 1) % dsp_parm_cmd_cnt);
        s_parm_st = PARM_SEND;
    }
}
