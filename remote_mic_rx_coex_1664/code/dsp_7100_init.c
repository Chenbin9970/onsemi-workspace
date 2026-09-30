/* Ezairo 7100 通讯子系统（移植自 remote_mic_rx_coex，阶段一：通讯层）。
 *
 * dsp_7100_boot_init()     ：开机同步跑「首个 A7 之前」的引导步。
 * 读回 4 程序×(WDRC/DFBC/降噪)：推进统一由主循环的 dsp_7100_rb_poll() 做，两个相位
 * 各由一条 DIO13 边沿驱动（发完等上升沿、读完等下降沿），app_process.c 的
 * dsp_7100_rb_tick() 只置 200ms 超时标志兜底。一轮完成后解析打印并停止。
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
 *   DSP_DUMP_MAX×3+4 必须远小于 200（32 → 100 字符）。
 *   调用点的行首前缀（含 wait=/len=/ok=）实测上限约 51 字符，
 *   整行最坏 ≈151B < 200：加大 dump 或往前缀里塞字段时须重新核算。
 *   ⚠ 读回 HDR 行（RB_DUMP data + 82 三段合起来最长）实测上界 ≈175B < 200，
 *     余量只剩 25B —— 再往这行加字段或调大 DSP_DUMP_MAX 必须重新核算。 */
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

/* 读回过程中把**真发到线上**的字节打出来（tag = I2C 首字节：写 04 / 读 05 / 收尾 82）。
 * 1 = 开。⚠ 串口 115200：一轮多出约 4KB ≈ 0.35s，
 *   **开着时的时间戳不能用来比较调速效果**（会把 1.0s 的一轮拖到 1.4s 上下）。测速前改回 0。 */
#define RB_DUMP_BYTES   1

#if (RB_DUMP_BYTES)
#define RB_DUMP(tag, p, n)   do { PRINTF(" %s:", (tag)); dump_hex((p), (n)); } while (0)
#else
#define RB_DUMP(tag, p, n)   ((void)0)
#endif

/* I2C 上的收尾字节 0x82（HAL 会在前面补一个地址字节 → 线上是 04 82）。
 * 读回每条命令读完后发一条收尾。 */
static const uint8_t s_end82[] = { 0x82 };

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
            PRINTF("[7100-init] %u/%u wait=%uus TX len=%u ok=%u:",
                   i + 1, n, (unsigned)st->delay_us, st->len, ok);
            dump_hex(st->data, st->len);
            PRINTF("\r\n");
        } else {
            uint8_t rx[DSP_INIT_RX_BUF];
            bool ok = i2c_7100_read(I2C_7100_ADDR, rx, st->len);
            PRINTF("[7100-init] %u/%u wait=%uus RX len=%u ok=%u:",
                   i + 1, n, (unsigned)st->delay_us, st->len, ok);
            dump_hex(rx, st->len);
            PRINTF("\r\n");
        }

        /* 逐条打印 DIO13 边沿增量（只在计数变化时输出）：定位是哪条命令让它跳。
         * 7100 的边沿可能滞后于本步 I2C 操作，落在下一步开头 —— 归属看 ±1 步。 */
        dsp_7100_dio13_irq_poll();
    }

    PRINTF("[7100-init] sync pre-A7 done\r\n");
}

/* ---- DIO13 边沿中断：观测 7100 的「响应就绪」边沿 ----
 * 3 号线采上升沿、2 号线采下降沿 —— 同一根 DIO13 挂两条中断线
 * （DIO->INT_CFG[index] 是数组，两个 slot 各自独立配事件，互不干扰）。
 * 为什么采双边沿：协议只说「窄脉冲」，没说极性；而 7100 在上电握手里是**拉低**
 * DIO13 表示 ready（app.c：7100 先拉低等待，RSL10 在 DIO11 应答后它才回高），
 * 所以「低=有效」的极性同样说得通。参考设计只采了上升沿、且没留下结论 ——
 * 照抄它的极性等于赌一把，一次上板未必有答案。双边沿则一次上板同时拿到
 * 「有没有边沿」和「什么极性」两个信息。测完把开关改回 0。
 * ISR 里只加计数：不碰 I2C、不打印。打印留给 poll，共三处：boot_init 每步一条
 * （逐命令定位），app.c 握手后一条作基线、主循环里一条作总数。 */
#define DSP7100_DIO13_RISE_IDX   3
#define DSP7100_DIO13_FALL_IDX   2
#define DSP7100_DIO13_DIO        13

#if DSP7100_DIO13_IRQ_ENABLE

static volatile uint32_t s_d13_rise;
static volatile uint32_t s_d13_fall;
static uint32_t          s_d13_rise_shown;
static uint32_t          s_d13_fall_shown;

/* 边沿中断服务：只加计数。参考 blinky 样例与参考设计，DIO 中断不需显式清标志
 * （NVIC 进中断自动清 pending）。若上板发现它持续重入，再补清标志。 */
void DIO3_IRQHandler(void)      /* DIO13 上升沿 */
{
    s_d13_rise++;
}

void DIO2_IRQHandler(void)      /* DIO13 下降沿 */
{
    s_d13_fall++;
}

void dsp_7100_dio13_irq_arm(void)
{
    s_d13_rise = 0;
    s_d13_fall = 0;
    s_d13_rise_shown = 0;
    s_d13_fall_shown = 0;

    Sys_DIO_Config(DSP7100_DIO13_DIO,
                   DIO_MODE_INPUT | DIO_WEAK_PULL_UP | DIO_LPF_DISABLE);

    Sys_DIO_IntConfig(DSP7100_DIO13_RISE_IDX,
                      DIO_DEBOUNCE_DISABLE | DIO_SRC(DSP7100_DIO13_DIO) |
                      DIO_EVENT_RISING_EDGE, 0, 0);
    Sys_DIO_IntConfig(DSP7100_DIO13_FALL_IDX,
                      DIO_DEBOUNCE_DISABLE | DIO_SRC(DSP7100_DIO13_DIO) |
                      DIO_EVENT_FALLING_EDGE, 0, 0);

    NVIC_SetPriority(DIO3_IRQn, 3);
    NVIC_SetPriority(DIO2_IRQn, 3);
    NVIC_ClearPendingIRQ(DIO3_IRQn);
    NVIC_ClearPendingIRQ(DIO2_IRQn);
    NVIC_EnableIRQ(DIO3_IRQn);
    NVIC_EnableIRQ(DIO2_IRQn);

    PRINTF("[7100-irq] DIO13 edge armed: rise=DIO3_IRQn fall=DIO2_IRQn\r\n");
}

void dsp_7100_dio13_irq_poll(void)
{
    uint32_t r = s_d13_rise;
    uint32_t f = s_d13_fall;

    if (r == s_d13_rise_shown && f == s_d13_fall_shown) return;

    PRINTF("[7100-irq] DIO13 rise +%lu/%lu fall +%lu/%lu\r\n",
           (unsigned long)(r - s_d13_rise_shown), (unsigned long)r,
           (unsigned long)(f - s_d13_fall_shown), (unsigned long)f);
    s_d13_rise_shown = r;
    s_d13_fall_shown = f;
}

/* 上升/下降沿累计数：读回拿它们当「7100 已吃下这一步」的加速判据
 * （见 dsp_7100_rb_poll）。中断没开时恒为 0 ⇒ 加速路径永不触发，自动退回 200ms。 */
uint32_t dsp_7100_dio13_rise_cnt(void)
{
    return s_d13_rise;
}

uint32_t dsp_7100_dio13_fall_cnt(void)
{
    return s_d13_fall;
}

#else    /* if DSP7100_DIO13_IRQ_ENABLE */

void dsp_7100_dio13_irq_arm(void)
{
}

void dsp_7100_dio13_irq_poll(void)
{
}

uint32_t dsp_7100_dio13_rise_cnt(void)
{
    return 0;
}

uint32_t dsp_7100_dio13_fall_cnt(void)
{
    return 0;
}

#endif    /* if DSP7100_DIO13_IRQ_ENABLE */

/* ---- 4 程序×(降噪/DFBC/WDRC) 读回：读到即解析成参数，不保留原始块 ---- */
#define RB_PROGS      4
#define RB_CH         16
#define RB_LL_BASE    414        /* WDRC payload 内 ch0 LowLevelGain bit 起点 */
#define RB_CH_STEP    147        /* 每通道 bit 间隔 */
#define RB_LL_OFF     0          /* 通道单元内 LowLevelGain 位偏移 */
#define RB_HL_OFF     14         /* HighLevelGain  8bit 有符号 */
#define RB_OL_OFF     22         /* OutputLimit    8bit 有符号 */

/* ★ 一次性实验（2026-09-29）：读回时故意漏发第 N 条命令的 0x82。
 * 验证假设「0x82 = 主机确认读走 → 7100 释放读缓冲」。判据是**下一条**命令：
 *   漏发后第 N+1 条拿不到 0x46（一直 retry 或返回 65）→ 假设成立，0x82 是解锁字节；
 *   照样拿到 0x46 且一轮跑完 → 0x82 只是事务收尾的礼节字节，不承担解锁职责。
 * 判据只认**签名**：基线偶发重试返回 65 01 00（7100 明说未就绪），而漏发 82 后拿到的是
 * 00 00 00（7100 完全不响应）—— 复现这个新签名才算数，光看「重试了一次」分不开，
 * 一轮里本来就会偶尔重试。
 *
 * **实验已结束，假设成立**（漏第 4 条、漏第 11 条各复现一次 00 00 00，补发即愈）。
 * 设 >= 28（RB_PROGS*7）即等于关闭 —— 关闭后这段实验分支永久不进入。 */
#define RB_SKIP_END82_IDX   28

static dsp_7100_rb_bufs_t s_rb_bufs;

enum { RB_SEND, RB_READ };
static uint8_t  s_rb_st  = RB_SEND;
static uint16_t s_rb_idx = 0;
static uint8_t  s_rb_done = 0;
static uint8_t  s_rb_needed = 1;    /* 1 = 需走 I2C 读回（缓存未命中） */
static uint8_t  s_save_pending = 0; /* 1 = 读回完成，待主循环落盘 */
static uint8_t  s_rb_timeout = 0;   /* 200ms tick 置位：边沿没来，poll 无条件走一步当兜底 */
static uint8_t  s_rb_retry = 0;     /* 1 = 上次读失败过：RB_READ 相位不再认边沿，只等 tick */
static uint32_t s_rb_wait_rise;     /* 发命令前记的上升沿数：RB_READ 相位等它变（方案 B） */
static uint32_t s_rb_wait_fall;     /* 发 82 前记的下降沿数：RB_SEND 相位等它变（方案 A） */

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
    bool ok82 = false;             /* 本步是否真发了 04 82（漏发实验时置假，打印据此如实反映） */
    bool pass = false;

    if (!s_rb_needed) return;      /* flash 缓存命中，无需 I2C 读回 */
    if (s_rb_done) return;
    if (dsp_rb_cmd_cnt == 0) return;
    g = &dsp_rb_cmds[s_rb_idx];
    dlen = (uint16_t)g->wr[3] + ((uint16_t)g->wr[4] << 8);

    if (s_rb_st == RB_SEND) {
        /* 先记上升沿数再发：这条命令的上升沿（7100 收到了）此刻还没到，到了
         * dsp_7100_rb_poll 就会立刻去读，不必等满 200ms（方案 B 的下半场）。
         * 同 RB_READ 里记下降沿的道理：必须记在动作之前。 */
        s_rb_wait_rise = dsp_7100_dio13_rise_cnt();
        s_rb_retry = 0;                /* 新命令：这个相位重新认上升沿 */
        bool okw = i2c_7100_write(I2C_7100_ADDR, g->wr, g->wl);
        (void)okw;
        PRINTF("[RB] %u/%u TX ok=%u (dlen=%u)", s_rb_idx + 1,
               dsp_rb_cmd_cnt, okw, dlen);
        /* 表里存的是 payload；线上首字节 04 由 i2c_7100_write 内部按 7bit 地址左移补上
         * （见表头注释），这里照线上原样打，方便对着抓包看。 */
        RB_DUMP("04", g->wr, g->wl);
        PRINTF("\r\n");
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
    /* 先记下当前下降沿数，再发 82：这条 82 的下降沿此刻还没到，等它到了
     * dsp_7100_rb_poll 就会立刻发下一条，不必等满 200ms（方案 A，只加速这半场）。
     * 记在发之前是必须的 —— 记在之后万一边沿快到来不及，这一步就永远等不到「新的」下降沿。 */
    s_rb_wait_fall = dsp_7100_dio13_fall_cnt();

    if (s_rb_idx == RB_SKIP_END82_IDX) {
        /* ★ 实验步：漏发 04 82。看下一条命令还能不能拿到 46（见 RB_SKIP_END82_IDX） */
        PRINTF("[RB] %u/%u ★实验：本步漏发 04 82\r\n", s_rb_idx + 1, dsp_rb_cmd_cnt);
    } else {
        (void)i2c_7100_write(I2C_7100_ADDR, s_end82, sizeof(s_end82));   /* 04 82 收尾 */
        ok82 = true;    /* 下面打在 HDR 行尾，让「写了 82 / 故意漏发」在日志里一目了然 */
    }

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

    PRINTF("[RB] %u/%u HDR %02X %02X %02X dlen=%u DATA%uB ok=%u %s",
           s_rb_idx + 1, dsp_rb_cmd_cnt, hdr[0], hdr[1], hdr[2], dlen,
           rd, okp, pass ? "PASS->next" : "retry");
    if (rd > 0) RB_DUMP("data", rx, rd);          /* 读到的 payload，超 32B 截断打 .. */
    if (ok82)   RB_DUMP("82", s_end82, sizeof(s_end82));
    PRINTF("\r\n");

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
    } else {
        /* 读失败：本相位改为只等 tick 兜底。
         * 必须这么做 —— 刚才那记 04 82 自己就产生一个上升沿（7100 收到 82 的脉冲
         * 含上升沿），而 s_rb_wait_rise 是发命令前记的、本相位不再刷新，于是
         * dsp_7100_rb_poll 会把这个 82 的边沿当成「新命令的上升沿」立刻再读一次。
         * 实测退化成 28~30ms 一次的重试风暴；读回 hlen=0（00 00 00，只读 3 字节头）
         * 时更是 ~1ms 一次 —— 等于不停写 82 冲 7100。锁上之后 retry 回到 200ms。 */
        s_rb_retry = 1;
    }
}

/* 200ms tick（app_process.c）：只置超时标志，不推进 —— 推进统一由 dsp_7100_rb_poll 做。
 * 为什么不让 tick 直接走一步：那样 tick 和主循环就成了两个推进点，会互相抢。
 * 主循环在 T+210ms 加速发了下一条之后，T+400ms 的 tick 若仍无条件走一步，
 * 本该做的「读」就被这一跳顶掉了，节奏反而拖到 390ms。单推进点没有这个问题。
 *
 * 兜底是必需的，不是保险起见：实测「等 82 的下降沿」有两次没等到 —— 漏发 82 那次
 * 高电平挂了 820ms 不落，cmd 8 基线重试那次也是 810ms。 */
void dsp_7100_rb_tick(void)
{
    s_rb_timeout = 1;
}

/* 主循环（app.c）：读回唯一的推进点。两个相位都由 DIO13 边沿驱动（方案 B）。
 *
 *   RB_SEND 等**下降沿** —— 读完了、82 也发了，下降沿表示「7100 吃下了这条事务」，
 *                            可以立刻发下一条。
 *   RB_READ 等**上升沿** —— 命令刚发出去，上升沿表示「7100 收到了」，可以试着读。
 *
 * 为什么 RB_READ 读失败后（s_rb_retry）不再认边沿：82 自己就带一个上升沿，
 * 认了会自激（见 dsp_7100_rb_seq_tick 失败分支）。锁上之后重试只走 tick，
 * 200ms 一次，这是刻意的。
 *
 * 这样安排的兜底性质：万一「上升沿到了」时响应其实还没备好，读会空一次 → retry →
 * 退回 tick 节奏，这条命令退化成方案 A 的 ~210ms，**不会比 A 更差**。 */
void dsp_7100_rb_poll(void)
{
    if (s_rb_done || !s_rb_needed) {
        s_rb_timeout = 0;
        return;
    }

    if (s_rb_st == RB_SEND) {
        if (!s_rb_timeout && dsp_7100_dio13_fall_cnt() == s_rb_wait_fall) return;
    } else if (s_rb_retry) {
        if (!s_rb_timeout) return;     /* 重试中：不认边沿，只等 200ms 兜底 */
    } else {
        if (!s_rb_timeout && dsp_7100_dio13_rise_cnt() == s_rb_wait_rise) return;
    }

    s_rb_timeout = 0;
    dsp_7100_rb_seq_tick();
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
        /* 以当前下降沿数为基准：引导期已攒了一堆（握手 1 次 + 106 步里的 82），
         * 不设基准的话第一条命令会被当成「新边沿到了」立刻发出去。 */
        s_rb_wait_fall = dsp_7100_dio13_fall_cnt();
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
