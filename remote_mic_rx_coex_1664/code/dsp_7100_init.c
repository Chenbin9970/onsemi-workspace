/* Ezairo 7100 通讯子系统（移植自 remote_mic_rx_coex，阶段一：通讯层）。
 *
 * dsp_7100_boot_init()     ：开机同步跑「首个 A7 之前」的引导步。
 * 读回 4 程序×(WDRC/DFBC/降噪)：推进统一由主循环的 dsp_7100_rb_poll() 做，两个相位
 * 各由一条 DIO13 边沿驱动（发完等上升沿、读完等下降沿），app_process.c 的
 * dsp_7100_rb_tick() 只置 200ms 超时标志兜底。一轮完成后解析打印并停止。
 *
 * ⚠ 2026-10-09：**时序与底层原语都和命令会话（dsp_7100_cmd.c）统一了** ——
 *   读相位放行前要求 DIO13 是高的、SEND 门线高就先兜底读、收尾也兜底读到线低；
 *   读一帧 / 发 82 都走共享原语（dsp_7100_read_frame / dsp_7100_send_end）。
 *   与命令会话剩下的差别**只有**：多校验一个长度（`hlen == dlen`）、读失败不封顶。
 *
 * 读路径只读不写；写路径（动态 A7 编码 / 会话状态机）留待阶段二。
 * 延时 ≥1ms 分段喂狗。 */

#include "dsp_7100_init.h"
#include "dsp_7100_storage.h"
#include "dsp_7100_cmd.h"     /* dsp_7100_read_frame / dsp_7100_send_end / 门限常量（两边共用） */
#include "ble_rempro_cmd.h"   /* rempro_push_7100_notify()：兜底读到的通知顺手上报（BLE 未连内部跳过） */
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
 *   ⚠ 读回 HDR 行（前缀 + RB_DUMP data 两段，82 已挪走）实测上界 ≈165B < 200，
 *     余量只剩 35B —— 再往这行加字段或调大 DSP_DUMP_MAX 必须重新核算。 */
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

/* 读回过程中把**真发到线上**的字节打出来（tag = I2C 首字节，如写帧的 04 / 读到的 data）。
 * ⚠ 收尾的 `82` **不走这里**了（2026-10-09）：由共享原语 dsp_7100_send_end() 自己打
 *   （`[7100] W (1B ok=1): 82`）—— 读回原先裸发字节，那行日志就只此一家，不好对齐。
 * 1 = 开。⚠ 串口 115200：一轮多出约 4KB ≈ 0.35s，
 *   **开着时的时间戳不能用来比较调速效果**（会把 1.0s 的一轮拖到 1.4s 上下）。测速前改回 0。 */
#define RB_DUMP_BYTES   1

#if (RB_DUMP_BYTES)
#define RB_DUMP(tag, p, n)   do { PRINTF(" %s:", (tag)); dump_hex((p), (n)); } while (0)
#else
#define RB_DUMP(tag, p, n)   ((void)0)
#endif

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

static dsp_7100_rb_bufs_t s_rb_bufs;

enum { RB_SEND, RB_READ };
static uint8_t  s_rb_st  = RB_SEND;
static uint16_t s_rb_idx = 0;
static uint8_t  s_rb_done = 0;
static uint8_t  s_save_pending = 0; /* 1 = 读回完成，待主循环落盘 */
static uint8_t  s_rb_timeout = 0;   /* 200ms tick 置位：边沿没来，poll 无条件走一步当兜底 */
static uint8_t  s_rb_retry = 0;     /* 1 = 上次读失败过：RB_READ 相位不再认边沿，只等 tick */
static uint32_t s_rb_wait_rise;     /* 发命令前记的上升沿数：RB_READ 相位等它变（方案 B） */
static uint32_t s_rb_wait_fall;     /* 发 82 前记的下降沿数：RB_SEND 相位等它变（方案 A） */
static uint8_t  s_rb_low_ticks = 0; /* 本步「tick 放行但线低」连吞了几拍（同命令族，见 rb_poll） */
static uint8_t  s_rb_rescue_cnt = 0;/* 本步已做的兜底读次数（DSP7100_RESCUE_MAX 封顶，同命令族） */

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

/* 兜底读一帧（线高 = 7100 压着一帧）：读头 3B + 按 len16 补读 payload → 打印 → 上报通知
 * → 82。与命令会话的 a7_rescue_read() **同形、同原语**（2026-10-09 统一）：
 * 读走那帧就是还 credit，不做这一步它会被后面的 rebase / 下一相位吞掉。
 * 计数与封顶由调用方管（每步 DSP7100_RESCUE_MAX 次），本函数不管循环。
 * 只在 I2C 正常时进来，故这里不再重复判 ok —— 就地照实打印。 */
static void rb_rescue_read(void)
{
    uint8_t  rx[DSP_INIT_RX_BUF];
    uint16_t hlen = 0;
    uint8_t  rd;

    PRINTF("[RB] %u/%u 线高（7100 压着一帧）→ 兜底读 #%u\r\n",
           s_rb_idx + 1, dsp_rb_cmd_cnt, (unsigned)s_rb_rescue_cnt);

    rd = dsp_7100_read_frame(rx, DSP_INIT_RX_BUF, &hlen);   /* 头 rx[0..2] + payload rx[3..] */
    if (rd == DSP7100_RDF_ERR_HDR) {
        PRINTF("[RB] 兜底读失败（头）—— 不发 82（没读到就不欠 credit）\r\n");
        return;
    }
    PRINTF("[RB] 兜底 HDR %02X %02X %02X len16=%u ok=%u\r\n",
           rx[0], rx[1], rx[2], hlen, (unsigned)(rd != DSP7100_RDF_ERR_PAY));
    if (hlen > 0) {
        PRINTF("[RB] 兜底 data:");
        dump_hex(rx + 3, hlen);
        PRINTF("\r\n");
    }

    if (rd == DSP7100_RDF_OK && hlen > 0) {
        /* 7100 本地按键改程序/音量走的也是这条通道，顺手解一次别丢（同 sniff / 命令会话） */
        rempro_push_7100_notify(rx + 3, (uint8_t)((hlen > 3u) ? 3u : hlen));
    }

    s_rb_wait_fall = dsp_7100_dio13_fall_cnt();   /* 先记沿：这条 82 的下降沿还没到 */
    (void)dsp_7100_send_end();
}

void dsp_7100_rb_seq_tick(void)
{
    const dsp_a7_cmd_t *g;
    uint8_t  rx[DSP_INIT_RX_BUF] = {0};   /* 头 3B 在 rx[0..2]，payload 从 rx+3 起（读法 = 共享原语）
                                           * 清零：读失败时别把栈上的垃圾当数据打出来（同命令族） */
    uint16_t dlen, hlen;
    uint8_t  rd_code;               /* dsp_7100_read_frame 的结果码 DSP7100_RDF_* */
    bool okh, okp = true;
    bool pass = false;

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
        s_rb_low_ticks = 0;            /* 本步等待计数清零（tick 兜底连吞多少拍，见 rb_poll） */
        s_rb_rescue_cnt = 0;           /* 本步兜底读次数清零（DSP7100_RESCUE_MAX 是**每步**上限） */
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

    /* 读法与命令会话共用（dsp_7100_read_frame）：头 3B + 按 len16 补读 payload。
     * 打印保持本模块的分块形状 —— 375B 合成一行会冲爆 printf 的 200B 缓冲。 */
    rd_code = dsp_7100_read_frame(rx, DSP_INIT_RX_BUF, &hlen);
    okh = (rd_code != DSP7100_RDF_ERR_HDR);
    okp = (rd_code != DSP7100_RDF_ERR_PAY);

    /* 先记下当前下降沿数，再发 82：这条 82 的下降沿此刻还没到，等它到了
     * dsp_7100_rb_poll 就会立刻发下一条，不必等满 200ms（方案 A，只加速这半场）。
     * 记在发之前是必须的 —— 记在之后万一边沿快到来不及，这一步就永远等不到「新的」下降沿。 */
    s_rb_wait_fall = dsp_7100_dio13_fall_cnt();

    (void)dsp_7100_send_end();      /* 04 82 收尾（共享原语，自己打印） */

    /* 校验：首字节 46（7100 接下了）+ **长度**（头里的 len16 必须等于该块的字节数）。
     * 长度这一条是读回独有、也是它**唯一**能发现读错位的手段（375/306/174B 的内容本身
     * 不校验；历史上头错位一字节 → hlen 被夹到 700 → 第 28 步永久卡死）。
     * dlen 取自读命令里的 16 位块地址（`A7 01 00 77 01` → 0x0177 = 375），恰好是字节数。 */
    if (okh && okp && rx[0] == DSP7100_RSP_OK && hlen == dlen) pass = true;

    /* 每程序 7 条命令：0=选程序 1/3/5=选模块 2/4/6=读 WDRC/DFBC/降噪。
     * 读到即解析成参数，原始块读完即弃。payload 在 rx+3 起（头 3B 在 rx[0..2]）。 */
    if (pass && hlen >= dlen) {
        uint8_t prog = (uint8_t)(s_rb_idx / 7u);
        uint8_t off  = (uint8_t)(s_rb_idx % 7u);

        if (off == 2) {
            rb_parse_wdrc(prog, rx + 3);        /* read 77 01 → LL/HL/OL */
        } else if (off == 4) {
            rb_parse_dfbc(prog, rx + 3);        /* read 32 01 → 开关 */
        } else if (off == 6) {
            rb_parse_noise(prog, rx + 3);       /* read AE 00 → 使能/档位，本程序解析完 */
            s_rb_bufs.valid[prog] = 1;
        }
    }

    PRINTF("[RB] %u/%u HDR %02X %02X %02X dlen=%u DATA%uB ok=%u %s",
           s_rb_idx + 1, dsp_rb_cmd_cnt, rx[0], rx[1], rx[2], dlen,
           hlen, okp, pass ? "PASS->next" : "retry");
    if (hlen > 0) RB_DUMP("data", rx + 3, hlen);   /* 读到的 payload，超 32B 截断打 .. */
    PRINTF("\r\n");

    if (pass) {
        s_rb_idx++;
        if (s_rb_idx >= dsp_rb_cmd_cnt) {   /* 一轮完成，停止并解析 */
            /* 收尾兜底读（同命令会话 a7_session_finish）：跑完时线还高 = 7100 压着一帧，
             * 先读掉再收尾，否则那一帧从此没人读（读回一 done，rb_poll 就永远不再进来）。
             * 要**读到线落回低或到上限**为止，不能只兜一次 —— 线高说明它手上还有帧。
             * ⚠ 同步循环，约 30ms 不喂 BLE/音频（同命令会话的收尾兜底）。 */
            if (DIO_DATA->ALIAS[13] == 1) {
                s_rb_rescue_cnt = 0;        /* 收尾这一轮单独计数，上限同每步的 DSP7100_RESCUE_MAX */
                while (DIO_DATA->ALIAS[13] == 1 && s_rb_rescue_cnt < DSP7100_RESCUE_MAX) {
                    s_rb_rescue_cnt++;
                    rb_rescue_read();
                }
                if (DIO_DATA->ALIAS[13] == 1) {
                    PRINTF("[RB] 收尾兜底读已满 %u 次线仍高 → 到此为止\r\n",
                           (unsigned)DSP7100_RESCUE_MAX);
                }
            }
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
 *                            可以立刻发下一条。**还要线是低的**：线高 = 7100 又抬线
 *                            压着一帧 → 先兜底读掉再发（2026-10-09，同命令会话的 SEND 门）。
 *   RB_READ 等**上升沿** —— 命令刚发出去，上升沿表示「7100 收到了」，可以试着读。
 *                            **放行时还要求线是高的**：线低 = 回执还没出来，读了只会拿到
 *                            `00 00 00` 还白发一条 82（2026-10-09 起，同命令会话）。
 *
 * 为什么 RB_READ 读失败后（s_rb_retry）不再认边沿：82 自己就带一个上升沿，
 * 认了会自激（见 dsp_7100_rb_seq_tick 失败分支）。锁上之后重试只走 tick，
 * 200ms 一次，这是刻意的 —— 读回**不封顶**重试（开机一次性，照旧重试到成功）。
 *
 * 这样安排的兜底性质：万一「上升沿到了」时响应其实还没备好，读会空一次 → retry →
 * 退回 tick 节奏，这条命令退化成方案 A 的 ~210ms，**不会比 A 更差**。
 * 连等 DSP7100_WAIT_RISE_MAX_TICKS 拍线仍低时**盲读一次**降级（同命令族的路径 A）。 */
void dsp_7100_rb_poll(void)
{
    if (s_rb_done) {
        s_rb_timeout = 0;
        return;
    }

    if (s_rb_st == RB_SEND) {
        if (!s_rb_timeout && dsp_7100_dio13_fall_cnt() == s_rb_wait_fall) return;
        /* 门开（下降沿到了，或 tick 兜底）——**还要线是低的**才发下一条。
         * 线高 = 那条 82 之后 7100 又抬了线，它压着一帧：先把那帧读掉再回来发。
         * ⚠ 读回**第一条**命令前面没有 82，它的 s_rb_wait_fall 基准是
         *   dsp_7100_cache_try_load() 里设的 —— 第一条同样走这道门，照旧（实测
         *   28 步 1.029s 就含它开头那一拍 200ms）。 */
        if (DIO_DATA->ALIAS[13] == 1) {
            if (s_rb_rescue_cnt < DSP7100_RESCUE_MAX) {
                s_rb_rescue_cnt++;
                s_rb_timeout = 0;      /* 这一拍已经做了事，清掉 tick 标志免得连烧 */
                rb_rescue_read();
                return;                /* 回到本相位：等这条 82 的下降沿 + 线低 */
            }
            PRINTF("[RB] %u/%u 兜底读已满 %u 次线仍高 → 照发下一条\r\n",
                   s_rb_idx + 1, dsp_rb_cmd_cnt, (unsigned)DSP7100_RESCUE_MAX);
        }
    } else if (s_rb_retry) {
        if (!s_rb_timeout) return;     /* 重试中：不认边沿，只等 200ms 兜底 */
    } else {
        if (!s_rb_timeout && dsp_7100_dio13_rise_cnt() == s_rb_wait_rise) return;
        /* 到这里 = 沿到了，或 tick 兜底放行。两种来源**都要求线是高的**才读：
         *   沿放行但线低 —— 抬过又落回，那不是本步要读的帧（比如上一条 82 的回声沿）；
         *   tick 放行但线低 —— 7100 还没把回执放出来，**不读**（读了只会拿到 00 00 00
         *                       还白发一条 82，真正的回执稍后抬线却没人读 → 滞留）。
         * 吞掉这一拍继续等，连等 DSP7100_WAIT_RISE_MAX_TICKS 拍仍线低 → 盲读一次降级
         * （照命令族路径 A：有 46 + 长度双校验兜着，读到空帧只会走 retry，不会当成功）。 */
        if (DIO_DATA->ALIAS[13] == 0) {
            if (!s_rb_timeout) return;                 /* 沿放了又落回：等下一个 */
            if (++s_rb_low_ticks >= DSP7100_WAIT_RISE_MAX_TICKS) {
                PRINTF("[RB] %u/%u 等抬线超时（%u 拍线仍低）→ 盲读一次\r\n",
                       s_rb_idx + 1, dsp_rb_cmd_cnt, (unsigned)s_rb_low_ticks);
            } else {
                s_rb_timeout = 0;                      /* 吞掉这一拍，等下一拍/下一个沿 */
                return;
            }
        }
        s_rb_low_ticks = 0;
    }

    s_rb_timeout = 0;
    dsp_7100_rb_seq_tick();
}

/* ---- 读回结果查询（供上层取用；阶段二 Rempro 会用到）---- */
bool dsp_7100_rb_done(void)
{
    return s_rb_done != 0;
}

const dsp_7100_prog_t *dsp_7100_get_prog(uint8_t prog)
{
    if (prog >= RB_PROGS || !s_rb_bufs.valid[prog]) return NULL;
    return &s_rb_bufs.prog[prog];
}

/* 2026-10-09 起：flash 缓存**不再短路开机**，读回每次都走。
 *   读回结果与 flash 槽逐字节对比（storage 层），有差异才覆盖、无差异不碰 flash。
 * flash 旧值仍要载入：
 *   ① 读回解析**不碰** eq_low/mid/high（见 rb_parse_*），这三个字段唯一来源就是 flash
 *      槽 —— 不载入则每次开机归零，与 flash 必然判成「有差异」（每次都擦写），
 *      且 App 的 EQ 增量（a7_build_session 的 s_eq_delta）跟着算错。
 *   ② 读回中途失败时 RAM 里还留着能用的一份旧值。
 * ⚠ s_rb_done **不能**在这里置 1：app.c 一见 done 就把 DIO13 交给被动监听，
 *   读回就再也跑不动了（读回唯一的推进点是 dsp_7100_rb_poll）。 */
void dsp_7100_cache_try_load(void)
{
    if (dsp_7100_cache_load()) {
        PRINTF("[7100-cache] flash 旧值已载入（本次仍走读回对比）\r\n");
        rb_parse_print();       /* 旧值先打一遍，便于与读回后的新值对照 */
    } else {
        PRINTF("[7100-cache] flash 无有效旧值（首次开机？）\r\n");
    }

    /* 以当前下降沿数为基准：引导期已攒了一堆（握手 1 次 + 106 步里的 82），
     * 不设基准的话第一条命令会被当成「新边沿到了」立刻发出去。 */
    s_rb_wait_fall = dsp_7100_dio13_fall_cnt();
    PRINTF("[7100-cache] 走 I2C 读回（28 步），完成后与 flash 对比\r\n");
}

/* 运行时改了参数后请求落盘（实际擦写留给主循环 process_deferred） */
void dsp_7100_cache_save_request(void)
{
    s_save_pending = 1;
}

/* 主循环调：读回跑完一轮 → 落盘（flash 擦写阻塞且关中断，不能放定时器上下文）。
 * 逐槽结论（覆盖/跳过/失败）由 dsp_7100_cache_save 自己打，这里只报总失败。 */
void dsp_7100_process_deferred(void)
{
    if (!s_save_pending) return;
    s_save_pending = 0;

    if (!dsp_7100_cache_save()) {
        PRINTF("[7100-cache] save FAIL\r\n");
    }
}
