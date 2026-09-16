/* 7100 运行时命令层（切程序 / 调音量），移植自 peripheral_server_sleep7160test。
 *
 * 帧序列见 include/dsp_7100_cmd.h。均为阻塞调用（内含 ms 级延时），
 * 由 Rempro 命令处理（主循环上下文）调用。 */

#include "dsp_7100_cmd.h"
#include "dsp_7100_init.h"
#include "app.h"
#include "i2c_7100_hal.h"
#include <printf.h>
#include <string.h>

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

#define DSP7100_CMD_PARAM_WRITE  0xA2
#define DSP7100_REG_PROGRAM      0x16
#define DSP7100_REG_VOLUME       0x12
#define DSP7100_REG_TONE_MODE    0x2E   /* 测听模式寄存器：进测听 0x00 / 退出 0x58 */
#define DSP7100_TONE_MODE_ON     0x00
#define DSP7100_TONE_MODE_OFF    0x58
#define DSP7100_CMD_END          0x82
#define DSP7100_RX_LEN           6

/* 6 档音量 → 0-100 值：round(档位 * 100 / 6) = 17/33/50/67/83/100 */
static const uint8_t s_volume_value[6] = {0x11, 0x21, 0x32, 0x43, 0x53, 0x64};

static uint8_t s_cur_prog = 1;
static uint8_t s_cur_vol_level = 6;

/* ---- I2C 收发打印（切程序/音量/降噪/DFBC 四条链路共用）----
 * 只打数据字节；I2C 地址字节由 HAL 在 START 后发出，不在字节流内
 * （逻辑分析仪上对应的就是日志里没显示的 0x04/0x05）。
 *
 * ⚠ 必须分块：pack printf.c 用 vsprintf 写入 200B 静态缓冲（TX_BUFFER_SIZE），
 *   无边界检查。降噪写块 300B → 900+ 字符会冲爆缓冲导致死机。
 *   I2C_DUMP_CHUNK=32 → 每行 ≤ 98 字符，加前缀 < 200。 */
#define I2C_DUMP_CHUNK 32

static void print_i2c(char dir, const uint8_t *d, uint16_t len, bool ok)
{
    static const char hexd[] = "0123456789ABCDEF";
    char line[I2C_DUMP_CHUNK * 3 + 2];
    uint16_t i = 0;
    uint8_t first = 1;

    while (i < len) {
        uint16_t n = len - i;
        uint16_t w = 0;
        uint16_t k;

        if (n > I2C_DUMP_CHUNK) n = (uint16_t)I2C_DUMP_CHUNK;
        for (k = 0; k < n; k++) {
            line[w++] = ' ';
            line[w++] = hexd[d[i + k] >> 4];
            line[w++] = hexd[d[i + k] & 0x0Fu];
        }
        line[w] = '\0';

        if (first) {
            PRINTF("[7100] %c (%uB ok=%u):%s\r\n", dir, len, ok, line);
            first = 0;
        } else {
            PRINTF("      %s\r\n", line);
        }
        i += n;
    }

    if (len == 0) {
        PRINTF("[7100] %c (0B ok=%u)\r\n", dir, ok);
    }
}

#define print_w(p, l, ok)   print_i2c('W', (p), (l), (ok))
#define print_r(p, l, ok)   print_i2c('R', (p), (l), (ok))

static bool dsp_7100_write_cmd(uint8_t reg, uint8_t value)
{
    uint8_t wr[4] = {DSP7100_CMD_PARAM_WRITE, 0x00, reg, value};
    bool ok = i2c_7100_write(I2C_7100_ADDR, wr, sizeof(wr));

    print_w(wr, sizeof(wr), ok);
    return ok;
}

static bool dsp_7100_read6(uint8_t *rx)
{
    bool ok = i2c_7100_read(I2C_7100_ADDR, rx, DSP7100_RX_LEN);

    print_r(rx, DSP7100_RX_LEN, ok);
    return ok;
}

/* 发一帧结束 82，并打印 */
static bool dsp_7100_send_end(void)
{
    uint8_t end = DSP7100_CMD_END;
    bool ok = i2c_7100_write(I2C_7100_ADDR, &end, sizeof(end));

    print_w(&end, sizeof(end), ok);
    return ok;
}

/* 调音量：写帧 → 2ms → 读确认 → 1ms → 82 */
bool dsp_7100_set_volume(uint8_t level)
{
    uint8_t rx[DSP7100_RX_LEN];

    if (level < 1 || level > 6) return false;
    PRINTF("[7100] --- SetVolume level=%u ---\r\n", level);
    if (!dsp_7100_write_cmd(DSP7100_REG_VOLUME, s_volume_value[level - 1])) return false;
    i2c_7100_delay_ms(2);
    if (!dsp_7100_read6(rx)) return false;
    i2c_7100_delay_ms(1);
    if (!dsp_7100_send_end()) return false;
    s_cur_vol_level = level;
    PRINTF("[7100] --- SetVolume done ---\r\n");
    return true;
}

/* 切程序（对照 program1 完整 5 帧）：
 * 写帧 → 80ms → 读确认 → 1ms → 82 → 1ms → 读状态 → 1ms → 82 */
bool dsp_7100_switch_program(uint8_t prog)
{
    uint8_t rx[DSP7100_RX_LEN];

    if (prog < 1 || prog > 4) return false;
    PRINTF("[7100] --- SwitchProgram prog=%u ---\r\n", prog);
    if (!dsp_7100_write_cmd(DSP7100_REG_PROGRAM, prog)) return false;
    i2c_7100_delay_ms(80);
    if (!dsp_7100_read6(rx)) return false;
    i2c_7100_delay_ms(1);
    if (!dsp_7100_send_end()) return false;
    i2c_7100_delay_ms(1);
    if (!dsp_7100_read6(rx)) return false;
    i2c_7100_delay_ms(1);
    if (!dsp_7100_send_end()) return false;
    s_cur_prog = prog;
    PRINTF("[7100] --- SwitchProgram done ---\r\n");
    return true;
}

uint8_t dsp_7100_get_program(void)      { return s_cur_prog; }
uint8_t dsp_7100_get_volume_level(void) { return s_cur_vol_level; }


/* ============================================================================
 * A7 参数设置会话（降噪 / DFBC）—— 照 remote_mic_rx_coex 已验证的 tick 模型
 *
 * rx_coex 的 dsp_7100_set_seq_tick()：命令表 + 200ms tick，一条命令一个 tick：
 *   发命令 → （下个 tick）读 3B 应答(46 00 00) → 写 04 82 → 进下一条
 * 会话序列（与 rx_coex 写会话骨架一致，无 0x03 状态读）：
 *   静音 → 选程序 → 写块准备 → 写块 → confirm → 解除静音 → 选回程序0 → commit
 * ========================================================================== */

/* EQ 高音段最长：静音+选程序+11 通道×2 参数×2 命令+确认+解除+选回+提交 = 50 条
 * （命令缓冲 50×10B = 500B，取 640 留余量） */
#define A7_MAX_CMDS      56
#define A7_CMD_BUF_SZ    640
#define A7_RX_ACK        3      /* 应答固定 3B（46 00 00） */

#define A7_OPT_MUTE      0x25
#define A7_OPT_UNMUTE    0x26
#define A7_OPT_COMMIT    0x0C
#define A7_OPT_SELECT    0x12
#define A7_OPT_CONFIRM   0x10
#define A7_BLK_DENOISE   0x09
#define A7_BLK_DFBC      0x0A
#define A7_BLK_WDRC      0x07   /* WDRC 参数（LL/HL）写块 */

/* 会话类型 */
#define A7_KIND_DENOISE  0
#define A7_KIND_DFBC     1
#define A7_KIND_EQ       2
#define A7_KIND_WDRC     3
#define A7_KIND_TONE     4
#define A7_KIND_MUTE     5

/* WDRC 参数号：LowLevelGain(ch N) = 0x15 + 0x11×(N−1)，HighLevelGain = +2，OutputLimit = +3
 * （N 从 1 起；docs/7100协议/WDRC/7100_WDRC设置.md §1、§2）
 * 选块帧地址 16 位大端：A7 05 … 05 07 <prog> <addr_hi> <addr_lo>（ch16 OL = 0x117 → 01 17） */
#define A7_WDRC_LL_ADDR(n1)   (0x15u + 0x11u * ((n1) - 1u))
#define A7_WDRC_HL_ADDR(n1)   (A7_WDRC_LL_ADDR(n1) + 2u)
#define A7_WDRC_OL_ADDR(n1)   (A7_WDRC_LL_ADDR(n1) + 3u)

/* WDRC 单参数会话：用 s_sess_val 选择要写哪个参数 */
#define A7_WDRC_P_LL     0
#define A7_WDRC_P_HL     1
#define A7_WDRC_P_OL     2

/* EQ 三段 → WDRC 通道（数组下标 = 通道号-1，即 1-based 通道号减一）
 *   低音 ch1,ch2  |  中音 ch3,ch4,ch5  |  高音 ch6..ch16 */
static const uint8_t s_eq_ch_low[]  = { 0, 1 };
static const uint8_t s_eq_ch_mid[]  = { 2, 3, 4 };
static const uint8_t s_eq_ch_high[] = { 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };

static const uint8_t *s_eq_ch[3]   = { s_eq_ch_low, s_eq_ch_mid, s_eq_ch_high };
static const uint8_t  s_eq_ch_n[3] = { 2, 3, 11 };

/* EQ 调整量上限（±dB），超出钳位并告警 */
#define A7_EQ_MAX_DB     10

/* WDRC 值范围（dB，1 LSB = 1 dB）
 *   LowLevelGain  -30 ~ 60   （瑞听 SetGain 0-90 → 减 30；docs/7100协议/瑞听设置指令.md）
 *   HighLevelGain -30 ~ 60
 *   OutputLimit   -60 ~ 0
 * LL 原写 0..127（编码范围），与读取文档的 -30~60 不符；2026-09-15 按确认真实范围改为 -30~60。
 * ⚠ 负值的字节编码（7bit 还是 8bit 补码）尚未实测，当前按 8bit 补码写（同 HL）。 */
#define A7_WDRC_LL_MIN   (-30)
#define A7_WDRC_LL_MAX   60
#define A7_WDRC_HL_MIN   (-30)
#define A7_WDRC_HL_MAX   60
#define A7_WDRC_OL_MIN   (-60)
#define A7_WDRC_OL_MAX   0

/* OutputLimit 值字段的 4 字节（docs/7100协议/WDRC/7100_WDRC设置.md §3.2）：
 *   byte0 = OL 本身（int8 补码）          ← 按目标值算，不在表里
 *   byte1 = OL + K_ch                     ← 表里存 OL=-6 时的 byte1，按差值平移
 *   byte2 / byte3 = 逐通道常量，与 OL 无关
 * 表取自 p0setlowlevelgainallchannel0.txt（默认状态，OL=-6），
 * ch16 用 p0channel16set0.txt（OL=0）复核过。
 * ⚠ 只来自一次默认状态抓包 —— 若这些字节被其它工具改过，表即失准。 */
static const uint8_t s_wdrc_ol_tail[DSP7100_WDRC_CH][3] = {
    {0xEA, 0x57, 0x4E},   /* ch1  */
    {0xE5, 0xEA, 0xFE},   /* ch2  */
    {0xE3, 0x08, 0x46},   /* ch3  */
    {0xE2, 0x88, 0x64},   /* ch4  */
    {0xE2, 0xFD, 0x80},   /* ch5  */
    {0xE4, 0x06, 0x11},   /* ch6  */
    {0xE8, 0x35, 0xE8},   /* ch7  */
    {0xE9, 0x79, 0x7B},   /* ch8  */
    {0xE8, 0xFD, 0x84},   /* ch9  */
    {0xE8, 0xFD, 0x71},   /* ch10 */
    {0xE9, 0x17, 0xAF},   /* ch11 */
    {0xEA, 0xDC, 0xD6},   /* ch12 */
    {0xEB, 0x0B, 0x47},   /* ch13 */
    {0xEB, 0xEE, 0x18},   /* ch14 */
    {0xEC, 0x79, 0xC1},   /* ch15 */
    {0xED, 0x9F, 0x52},   /* ch16 */
};

/* 上表 byte1 对应的 OL 基准值（即抓包时的 OL 设置） */
#define A7_WDRC_OL_BASE   (-6)

/* ---- 纯音（测听）电平表 ----
 * 出纯音的 24bit 电平：level = round(4.39902 × 10^((db + C(freq))/20))
 *   C(freq) = 逐频率的**整数 dB 修正**（从 tone*.txt 抓包反解，见 docs/7100协议/纯音测听.md）
 * 索引 k = db + C，db ∈ 20..100、C ∈ -5..+14 → k ∈ 15..114
 * 与抓包 15 个点逐条比对**全部命中**（偏差 < 0.5 LSB）。
 * 用查表而非 powf：本工程未链接 libm（RSL10 Cortex-M3 无 FPU），
 * 与 1654 的 bs300_beep_frac24_table 同一做法。
 * ⚠ K = 4.39902 是拟合值（可行区间仅 9e-6 宽），**可能随整机/接收器不同**，换机需重标。 */
typedef struct
{
    uint16_t freq;
    int8_t   c;
} a7_tone_cal_t;

static const a7_tone_cal_t s_tone_cal[] = {
    { 500, -1 }, { 1000, 0 }, { 2000, 0 }, { 3000, -5 }, { 4000, 2 }, { 6000, 14 },
};

#define A7_TONE_CAL_N     (sizeof(s_tone_cal) / sizeof(s_tone_cal[0]))
#define A7_TONE_DB_MIN    20
#define A7_TONE_DB_MAX    100
#define A7_TONE_IDX_MIN   15
#define A7_TONE_IDX_MAX   114

static const uint32_t s_tone_level[A7_TONE_IDX_MAX - A7_TONE_IDX_MIN + 1] = {
          25,      28,      31,      35,      39,      44,
          49,      55,      62,      70,      78,      88,
          98,     110,     124,     139,     156,     175,
         196,     220,     247,     278,     311,     349,
         392,     440,     494,     554,     621,     697,
         782,     878,     985,    1105,    1240,    1391,
        1561,    1751,    1965,    2205,    2474,    2776,
        3114,    3494,    3921,    4399,    4936,    5538,
        6214,    6972,    7823,    8777,    9848,   11050,
       12398,   13911,   15608,   17513,   19650,   22047,
       24738,   27756,   31143,   34943,   39206,   43990,
       49358,   55380,   62138,   69720,   78227,   87772,
       98482,  110498,  123981,  139109,  156083,  175128,
      196497,  220473,  247375,  277559,  311427,  349427,
      392063,  439902,  493578,  553804,  621378,  697198,
      782269,  877720,  984818, 1104984, 1239812, 1391092,
     1560831, 1751281, 1964970, 2204733,
};

/* 纯音动作（放进 s_sess_val） */
#define A7_TONE_STOP     0
#define A7_TONE_PLAY     1

/* 降噪 5 档的三元组（49 个重复；XX = 3×档位+3）
 * 来源 docs/7100协议/降噪/7100_降噪设置.md §3 */
static const uint8_t s_noise_tri[5][3] = {
    {0x2F, 0x6E, 0xCD},   /* 档位 0 */
    {0x4C, 0x58, 0xCB},   /* 档位 1 */
    {0x60, 0xD1, 0x00},   /* 档位 2 */
    {0x6F, 0x4E, 0xC9},   /* 档位 3 */
    {0x79, 0x91, 0x1C},   /* 档位 4 */
};

typedef struct
{
    const uint8_t *data;
    uint16_t       len;
} a7_step_t;

static a7_step_t s_steps[A7_MAX_CMDS];
static uint8_t   s_step_cnt;
static uint8_t   s_step_idx;
static bool      s_step_read;       /* false=发命令  true=读应答+82 */
static bool      s_sess_active;

static uint8_t   s_cmd_buf[A7_CMD_BUF_SZ];
static uint16_t  s_cmd_used;

/* 会话元信息（成功后回写缓存用） */
static uint8_t   s_sess_prog;       /* 1-4 */
static uint8_t   s_sess_kind;       /* A7_KIND_* */
static uint8_t   s_sess_val;        /* 降噪档位 / DFBC 开关 / EQ 段 / WDRC 参数选择 */
static int8_t    s_sess_db;         /* EQ 调整量 ±dB */
/* WDRC 会话的待写项。**必须拷一份**：会话是异步跑的，
 * 调用方的数组（BLE 负载缓冲）到收尾回写缓存时可能已经失效。 */
static dsp_7100_wdrc_item_t s_wdrc_items[DSP7100_WDRC_CH];
static uint8_t              s_wdrc_n;

/* 纯音会话的频点与电平（异步跑，拷一份） */
static uint16_t s_tone_freq;
static uint8_t  s_tone_db;

static bool s_build_fail;           /* 命令表/缓冲放不下时置位 */

/* 从命令缓冲切一段并挂到步骤表 */
static uint8_t *a7_put(uint16_t len)
{
    uint8_t *p;

    if (s_step_cnt >= A7_MAX_CMDS) { s_build_fail = true; return NULL; }
    if ((uint32_t)s_cmd_used + len > A7_CMD_BUF_SZ) { s_build_fail = true; return NULL; }

    p = s_cmd_buf + s_cmd_used;
    s_cmd_used = (uint16_t)(s_cmd_used + len);
    s_steps[s_step_cnt].data = p;
    s_steps[s_step_cnt].len  = len;
    s_step_cnt++;
    return p;
}

/* A7 01 00 00 00 <opt> */
static void a7_add_simple(uint8_t opt)
{
    uint8_t *p = a7_put(6);

    if (p == NULL) return;
    p[0] = 0xA7; p[1] = 0x01; p[2] = 0x00;
    p[3] = 0x00; p[4] = 0x00; p[5] = opt;
}

/* A7 02 00 00 00 <opt> <P> */
static void a7_add_prog(uint8_t opt, uint8_t prog)
{
    uint8_t *p = a7_put(7);

    if (p == NULL) return;
    p[0] = 0xA7; p[1] = 0x02; p[2] = 0x00;
    p[3] = 0x00; p[4] = 0x00; p[5] = opt; p[6] = prog;
}

/* A7 05 00 00 00 05 <blk> <P> <a> <b> */
static void a7_add_prep(uint8_t blk, uint8_t prog, uint8_t a, uint8_t b)
{
    uint8_t *p = a7_put(10);

    if (p == NULL) return;
    p[0] = 0xA7; p[1] = 0x05; p[2] = 0x00; p[3] = 0x00; p[4] = 0x00;
    p[5] = 0x05; p[6] = blk;  p[7] = prog; p[8] = a;    p[9] = b;
}

/* WDRC 参数写：准备 + 写值（两条命令）
 *   A7 05 00 00 00 05 07 <P> <addr_hi> <addr_lo>
 *   A7 04 00 00 00 08 00 00 <val> */
static void a7_add_wdrc_param(uint8_t prog, uint16_t addr, uint8_t val)
{
    uint8_t *p = a7_put(9);

    a7_add_prep(A7_BLK_WDRC, prog, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF));
    if (p == NULL) return;
    p[0] = 0xA7; p[1] = 0x04; p[2] = 0x00; p[3] = 0x00; p[4] = 0x00;
    p[5] = 0x08; p[6] = 0x00; p[7] = 0x00; p[8] = val;
}

/* OutputLimit 写：准备 + 写值（值字段 4 字节，帧头是 A7 07 不是 A7 04）
 *   A7 05 00 00 00 05 07 <P> <addr_hi> <addr_lo>
 *   A7 07 00 00 00 08 00 00 <b0> <b1> <b2> <b3>
 * 参考抓包 p0channel1set-9 / p0channel2set-8 / p0channel16set0 */
static void a7_add_wdrc_ol(uint8_t prog, uint16_t addr, uint8_t ch1, int8_t val)
{
    const uint8_t *tail = s_wdrc_ol_tail[ch1 - 1];
    int16_t        b1;
    uint8_t       *p;

    a7_add_prep(A7_BLK_WDRC, prog, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF));
    p = a7_put(12);
    if (p == NULL) return;

    /* byte1 = OL + K_ch；K_ch 由表里 OL=-6 时的 byte1 反推，故按 (val - BASE) 平移 */
    b1 = (int16_t)(int8_t)tail[0] + ((int16_t)val - A7_WDRC_OL_BASE);
    if (b1 < -128) b1 = -128;
    if (b1 >  127) b1 =  127;

    p[0]  = 0xA7; p[1] = 0x07; p[2] = 0x00; p[3] = 0x00; p[4] = 0x00;
    p[5]  = 0x08; p[6] = 0x00; p[7] = 0x00;
    p[8]  = (uint8_t)val;
    p[9]  = (uint8_t)(int8_t)b1;
    p[10] = tail[1];
    p[11] = tail[2];
}

/* WDRC 多通道写：每项两条命令（选块 + 写值）。
 * 静音 / 选程序 / 提交 / 解除静音 是公共帧，由 a7_build_session 只加一次。 */
static void a7_add_wdrc_items(uint8_t prog, uint8_t which)
{
    uint8_t i;

    for (i = 0; i < s_wdrc_n; i++) {
        uint8_t ch = s_wdrc_items[i].ch;
        int8_t  v  = s_wdrc_items[i].val;

        if (which == A7_WDRC_P_OL) {
            a7_add_wdrc_ol(prog, A7_WDRC_OL_ADDR(ch), ch, v);
        } else if (which == A7_WDRC_P_HL) {
            a7_add_wdrc_param(prog, A7_WDRC_HL_ADDR(ch), (uint8_t)v);
        } else {
            a7_add_wdrc_param(prog, A7_WDRC_LL_ADDR(ch), (uint8_t)v);
        }
    }
}

/* 纯音帧：A7 07 00 00 00 2E <en> <freq16-BE> <level24-BE>
 *   出音 en=01 + 频率 + 电平；停音 en=00 + 全 0（频率电平一起清零）
 * 抓包：tone*hz*db.txt（15 条） */
static void a7_add_tone(uint8_t enable, uint16_t freq, uint32_t level)
{
    uint8_t *p = a7_put(12);

    if (p == NULL) return;
    p[0]  = 0xA7; p[1] = 0x07; p[2] = 0x00; p[3] = 0x00; p[4] = 0x00;
    p[5]  = 0x2E;
    p[6]  = enable ? 0x01 : 0x00;
    p[7]  = (uint8_t)(freq >> 8);
    p[8]  = (uint8_t)(freq & 0xFFu);
    p[9]  = (uint8_t)((level >> 16) & 0xFFu);
    p[10] = (uint8_t)((level >> 8) & 0xFFu);
    p[11] = (uint8_t)(level & 0xFFu);
}

/* 查电平表；返回 false = 该频点没有标定值 */
static bool a7_tone_level(uint16_t freq, uint8_t db, uint32_t *out)
{
    uint8_t i;
    int16_t k;

    for (i = 0; i < A7_TONE_CAL_N; i++) {
        if (s_tone_cal[i].freq != freq) continue;

        k = (int16_t)db + s_tone_cal[i].c;
        if (k < A7_TONE_IDX_MIN) k = A7_TONE_IDX_MIN;
        if (k > A7_TONE_IDX_MAX) k = A7_TONE_IDX_MAX;
        *out = s_tone_level[k - A7_TONE_IDX_MIN];
        return true;
    }
    return false;
}

/* 生成 300B 降噪写块（照 docs/7100协议/降噪/7100_降噪设置.md §3 结构）：
 *   [0..7]    头 A7 27 01 00 00 08 00 00
 *   [8..152]  49 × XX（间隔 2 字节 0）
 *   [153..299] 49 × 档位三元组 */
static uint8_t *a7_add_noise_block(uint8_t level)
{
    const uint8_t xx = (uint8_t)(3u * level + 3u);
    uint8_t *p = a7_put(300);
    uint8_t i;

    if (p == NULL) return NULL;

    memset(p, 0, 300);
    p[0] = 0xA7; p[1] = 0x27; p[2] = 0x01; p[3] = 0x00;
    p[4] = 0x00; p[5] = 0x08; p[6] = 0x00; p[7] = 0x00;

    for (i = 0; i < 49; i++) p[8 + (uint16_t)i * 3] = xx;
    for (i = 0; i < 49; i++)
        memcpy(p + 153 + (uint16_t)i * 3, s_noise_tri[level], 3);
    return p;
}

/* 会话骨架：静音 → 选程序 → [写块] → confirm → 解除静音 →
 * 选回程序0 → commit。写块由 kind 决定。 */
static bool a7_build_session(uint8_t kind, uint8_t prog, uint8_t val)
{
    s_step_cnt = 0;
    s_step_idx = 0;
    s_step_read = false;
    s_cmd_used = 0;
    s_build_fail = false;

    if (kind == A7_KIND_MUTE) {             /* 静音 / 解除静音：同样只发这一条 */
        a7_add_simple(val ? A7_OPT_MUTE : A7_OPT_UNMUTE);
        return !s_build_fail;
    }

    if (kind == A7_KIND_TONE) {             /* 纯音：只发这一条，无任何公共帧 */
        if (val == A7_TONE_PLAY) {
            uint32_t lvl;

            if (!a7_tone_level(s_tone_freq, s_tone_db, &lvl)) {
                PRINTF("[7100] 纯音 %u Hz 无标定值\r\n", s_tone_freq);
                s_build_fail = true;
                return false;
            }
            a7_add_tone(1, s_tone_freq, lvl);
        } else {
            a7_add_tone(0, 0, 0);
        }
        return !s_build_fail;
    }

    a7_add_simple(A7_OPT_MUTE);
    a7_add_prog(A7_OPT_SELECT, prog);

    if (kind == A7_KIND_DENOISE) {          /* 降噪：块 09 + A7 27(300B) */
        a7_add_prep(A7_BLK_DENOISE, prog, 0x00, 0x01);
        a7_add_noise_block(val);
    } else if (kind == A7_KIND_DFBC) {      /* DFBC：块 0A + A7 04(08 00 00 X) */
        uint8_t *p;
        a7_add_prep(A7_BLK_DFBC, prog, 0x00, 0x00);
        p = a7_put(9);
        if (p != NULL) {
            p[0] = 0xA7; p[1] = 0x04; p[2] = 0x00; p[3] = 0x00; p[4] = 0x00;
            p[5] = 0x08; p[6] = 0x00; p[7] = 0x00; p[8] = val ? 0x01 : 0x00;
        }
    } else if (kind == A7_KIND_WDRC) {      /* WDRC：n 个通道的同一参数（LL / HL / OL） */
        a7_add_wdrc_items(prog, val);
    } else {                                /* EQ：该段全部通道的 LL+HL */
        const dsp_7100_prog_t *bp = dsp_7100_get_prog((uint8_t)(prog - 1));
        uint8_t n;
        uint8_t i;

        if (bp == NULL) return false;       /* 无基准值 → 先做读回 */
        if (val > 2) return false;

        n = s_eq_ch_n[val];
        for (i = 0; i < n; i++) {
            uint8_t  ch  = s_eq_ch[val][i];             /* 0-based */
            int32_t  ll  = (int32_t)bp->wdrc_ll[ch] + s_sess_db;
            int32_t  hl  = (int32_t)bp->wdrc_hl[ch] + s_sess_db;

            if (ll < A7_WDRC_LL_MIN) ll = A7_WDRC_LL_MIN;
            if (ll > A7_WDRC_LL_MAX) ll = A7_WDRC_LL_MAX;
            if (hl < A7_WDRC_HL_MIN) hl = A7_WDRC_HL_MIN;
            if (hl > A7_WDRC_HL_MAX) hl = A7_WDRC_HL_MAX;

            a7_add_wdrc_param(prog, A7_WDRC_LL_ADDR(ch + 1), (uint8_t)ll);
            a7_add_wdrc_param(prog, A7_WDRC_HL_ADDR(ch + 1), (uint8_t)(int8_t)hl);
        }
    }

    a7_add_prog(A7_OPT_CONFIRM, prog);
    a7_add_simple(A7_OPT_UNMUTE);
    a7_add_prog(A7_OPT_SELECT, 1);        /* 指针复位回程序0（照抓包恒为 01） */
    a7_add_simple(A7_OPT_COMMIT);

    return !s_build_fail;                 /* 无溢出即构建成功 */
}

/* 会话收尾：成功后同步 RAM 参数并请求落盘（否则下次开机缓存显示旧值） */
static void a7_session_finish(bool ok)
{
    s_sess_active = false;
    PRINTF("[7100] --- session done ok=%u ---\r\n", ok);

    if (!ok) return;

    if (s_sess_kind == A7_KIND_DENOISE) {
        dsp_7100_prog_t *p = &dsp_7100_rb_bufs()->prog[s_sess_prog - 1];
        p->denoise_en  = 1;    /* 设档位即为开启（读回各档使能位均为 1） */
        p->denoise_lvl = s_sess_val;
    } else if (s_sess_kind == A7_KIND_DFBC) {
        dsp_7100_prog_t *p = &dsp_7100_rb_bufs()->prog[s_sess_prog - 1];
        p->dfbc_en = s_sess_val ? 1 : 0;
    } else if (s_sess_kind == A7_KIND_WDRC) {
        dsp_7100_prog_t *p = &dsp_7100_rb_bufs()->prog[s_sess_prog - 1];
        uint8_t i;

        /* 直接设绝对值 → 缓存基准跟着改（值在 a7_wdrc_start 里已钳位）。
         * ⚠ EQ 模型认为"设备值 = 基准 + 该段 eq"，若该段 eq 非 0，本值写下去后两者会差一个 eq 量；
         *   要严格一致应改成 "基准 = 新值 − 该段 eq"。目前 EQ 默认 0，先按简单处理。 */
        for (i = 0; i < s_wdrc_n; i++) {
            uint8_t ch = s_wdrc_items[i].ch;

            if (s_sess_val == A7_WDRC_P_LL)      p->wdrc_ll[ch - 1] = s_wdrc_items[i].val;
            else if (s_sess_val == A7_WDRC_P_HL) p->wdrc_hl[ch - 1] = s_wdrc_items[i].val;
            else                                 p->wdrc_ol[ch - 1] = s_wdrc_items[i].val;
        }
    } else if (s_sess_kind == A7_KIND_TONE || s_sess_kind == A7_KIND_MUTE) {
        /* 纯音 / 静音**不改缓存里的任何参数** → 直接返回。
         * 否则会走下面的 cache_save_request()，把整份读回缓存（4 个程序）重写一遍 flash ——
         * 测听时每播一个频点就擦写一次，纯属浪费 + 磨损 flash。 */
        return;
    } else {                   /* EQ：只存偏移量，基准值不动（避免多次设置累积） */
        dsp_7100_prog_t *p = &dsp_7100_rb_bufs()->prog[s_sess_prog - 1];
        if (s_sess_val == 0)      p->eq_low  = s_sess_db;
        else if (s_sess_val == 1) p->eq_mid  = s_sess_db;
        else                      p->eq_high = s_sess_db;
    }
    dsp_7100_cache_save_request();
}

/* 200ms tick 调：推进一条命令。发命令 → 下一 tick 读应答+82 → 进下一条 */
void dsp_7100_cmd_tick(void)
{
    const a7_step_t *s;
    uint8_t ack[A7_RX_ACK];
    bool ok;

    if (!s_sess_active) return;
    if (s_step_idx >= s_step_cnt) { a7_session_finish(true); return; }

    s = &s_steps[s_step_idx];

    if (!s_step_read) {
        ok = i2c_7100_write(I2C_7100_ADDR, s->data, s->len);
        print_w(s->data, s->len, ok);
        if (!ok) { a7_session_finish(false); return; }
        s_step_read = true;
        return;
    }

    ok = i2c_7100_read(I2C_7100_ADDR, ack, sizeof(ack));
    print_r(ack, sizeof(ack), ok);
    if (!dsp_7100_send_end()) ok = false;

    if (!ok) { a7_session_finish(false); return; }

    s_step_read = false;
    s_step_idx++;
    if (s_step_idx >= s_step_cnt) a7_session_finish(true);
}

bool dsp_7100_cmd_busy(void)
{
    return s_sess_active;
}

static const char *a7_kind_name(uint8_t kind)
{
    if (kind == A7_KIND_DENOISE) return "Denoise";
    if (kind == A7_KIND_DFBC)    return "DFBC";
    if (kind == A7_KIND_WDRC)    return "WDRC";
    if (kind == A7_KIND_TONE)    return "Tone";
    if (kind == A7_KIND_MUTE)    return "Mute";
    return "EQ";
}

/* 启动会话；返回 true = 已受理（异步，完成情况看日志） */
static bool a7_session_start(uint8_t kind, uint8_t prog, uint8_t val)
{
    if (s_sess_active) {
        PRINTF("[7100] 上一会话未完成，忽略本次设置\r\n");
        return false;
    }
    if (!a7_build_session(kind, prog, val)) {
        PRINTF("[7100] 会话构建失败（无基准值或命令缓冲不足）\r\n");
        return false;
    }

    s_sess_prog = prog;
    s_sess_kind = kind;
    s_sess_val  = val;
    s_sess_active = true;
    PRINTF("[7100] --- session start: %s prog=%u val=%u db=%d cmds=%u ---\r\n",
           a7_kind_name(kind), prog, val, s_sess_db, s_step_cnt);
    return true;
}

bool dsp_7100_set_denoise(uint8_t prog, uint8_t level)
{
    if (prog < 1 || prog > 4 || level > 4) return false;
    s_sess_db = 0;
    return a7_session_start(A7_KIND_DENOISE, prog, level);
}

bool dsp_7100_set_dfbc(uint8_t prog, uint8_t onoff)
{
    if (prog < 1 || prog > 4) return false;
    s_sess_db = 0;
    return a7_session_start(A7_KIND_DFBC, prog, onoff ? 1 : 0);
}

/* 三段均衡器：band 0=低音 1=中音 2=高音，db 为 ±dB 调整量。
 * 基准取读回值，写入 基准+db 到该段的 2 个通道（LL 与 HL 同时平移）。 */
bool dsp_7100_set_eq(uint8_t prog, uint8_t band, int8_t db)
{
    if (prog < 1 || prog > 4 || band > 2) return false;
    if (db > A7_EQ_MAX_DB)  { db = A7_EQ_MAX_DB; }
    if (db < -A7_EQ_MAX_DB) { db = -A7_EQ_MAX_DB; }

    s_sess_db = db;
    return a7_session_start(A7_KIND_EQ, prog, band);
}

/* ---- WDRC 参数（LL / HL / OL），单通道或多通道，异步会话，骨架同降噪 ---- */

static const char *a7_wdrc_name(uint8_t which)
{
    if (which == A7_WDRC_P_LL) return "LowLevelGain";
    if (which == A7_WDRC_P_HL) return "HighLevelGain";
    return "OutputLimit";
}

/* 钳到该参数的有效范围。写入和收尾回写缓存都用这一个，保证两边值一致 */
static void a7_wdrc_clamp(uint8_t which, int8_t *val)
{
    int8_t lo;
    int8_t hi;

    if (which == A7_WDRC_P_LL)      { lo = A7_WDRC_LL_MIN; hi = A7_WDRC_LL_MAX; }
    else if (which == A7_WDRC_P_HL) { lo = A7_WDRC_HL_MIN; hi = A7_WDRC_HL_MAX; }
    else                            { lo = A7_WDRC_OL_MIN; hi = A7_WDRC_OL_MAX; }

    if (*val < lo) { *val = lo; }
    if (*val > hi) { *val = hi; }
}

/* prog 1-4；items[0..n-1]，n 1..16；返回 true = 会话已启动 */
static bool a7_wdrc_start(uint8_t prog, uint8_t which,
                          const dsp_7100_wdrc_item_t *items, uint8_t n)
{
    uint8_t i;

    if (prog < 1 || prog > DSP7100_RB_PROGS) return false;
    if (items == NULL || n < 1 || n > DSP7100_WDRC_CH) return false;

    for (i = 0; i < n; i++) {
        if (items[i].ch < 1 || items[i].ch > DSP7100_WDRC_CH) return false;
        s_wdrc_items[i] = items[i];
        a7_wdrc_clamp(which, &s_wdrc_items[i].val);
    }
    s_wdrc_n = n;

    PRINTF("[7100] --- WDRC set %s prog=%u n=%u ---\r\n",
           a7_wdrc_name(which), prog, n);

    s_sess_db = 0;
    return a7_session_start(A7_KIND_WDRC, prog, which);
}

bool dsp_7100_set_low_level_gain(uint8_t prog, const dsp_7100_wdrc_item_t *items, uint8_t n)
{
    return a7_wdrc_start(prog, A7_WDRC_P_LL, items, n);
}

bool dsp_7100_set_high_level_gain(uint8_t prog, const dsp_7100_wdrc_item_t *items, uint8_t n)
{
    return a7_wdrc_start(prog, A7_WDRC_P_HL, items, n);
}

bool dsp_7100_set_output_limit(uint8_t prog, const dsp_7100_wdrc_item_t *items, uint8_t n)
{
    return a7_wdrc_start(prog, A7_WDRC_P_OL, items, n);
}

/* ---- 纯音（测听）：只有一条命令的会话，无 mute / 选程序 / commit ---- */

bool dsp_7100_play_tone(uint16_t freq_hz, uint8_t db)
{
    uint32_t lvl;

    if (db < A7_TONE_DB_MIN || db > A7_TONE_DB_MAX) {
        PRINTF("[7100] 纯音 %u dB 超范围（%u-%u）\r\n", db, A7_TONE_DB_MIN, A7_TONE_DB_MAX);
        return false;
    }
    if (!a7_tone_level(freq_hz, db, &lvl)) {
        PRINTF("[7100] 纯音 %u Hz 无标定值\r\n", freq_hz);
        return false;
    }

    s_tone_freq = freq_hz;
    s_tone_db   = db;

    PRINTF("[7100] --- tone play %u Hz %u dB ---\r\n", freq_hz, db);
    s_sess_db = 0;
    return a7_session_start(A7_KIND_TONE, dsp_7100_get_program(), A7_TONE_PLAY);
}

bool dsp_7100_stop_tone(void)
{
    PRINTF("[7100] --- tone stop ---\r\n");
    s_sess_db = 0;
    return a7_session_start(A7_KIND_TONE, dsp_7100_get_program(), A7_TONE_STOP);
}

/* 测听模式寄存器 `0x2E` —— A2 00 2E <val> → 读确认 → 82（同切程序/音量的三帧序列）
 *   on  → 0x2E = 0x00   进测听
 *   off → 0x2E = 0x58   退出 / 停音
 * 来源：tonestar.txt / tonestop.txt 抓包，见 docs/7100协议/纯音测听.md */
bool dsp_7100_set_tone_mode(bool on)
{
    uint8_t rx[DSP7100_RX_LEN];
    uint8_t val = on ? DSP7100_TONE_MODE_ON : DSP7100_TONE_MODE_OFF;

    PRINTF("[7100] --- tone mode %s (0x2E=%02X) ---\r\n", on ? "on" : "off", val);

    if (!dsp_7100_write_cmd(DSP7100_REG_TONE_MODE, val)) return false;
    i2c_7100_delay_ms(2);
    if (!dsp_7100_read6(rx)) return false;
    i2c_7100_delay_ms(1);
    return dsp_7100_send_end();
}

/* 静音 / 解除静音 —— 单命令会话（`A7 01 00 00 00 25` / `26`），无公共帧。
 * 对应 BLE 的 SetMuteData (21)：mute=true → 静音。
 * 参考设备也这么单发：tonestar/tonestop 抓包里 `A7 01 00 00 00 26` 就是独立一条（写→读→82）。 */
bool dsp_7100_set_mute(bool mute)
{
    PRINTF("[7100] --- %s ---\r\n", mute ? "mute" : "unmute");
    s_sess_db = 0;
    return a7_session_start(A7_KIND_MUTE, dsp_7100_get_program(), mute ? 1u : 0u);
}
