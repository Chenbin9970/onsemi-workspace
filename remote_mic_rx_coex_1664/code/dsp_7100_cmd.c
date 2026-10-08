/* 7100 运行时命令层（切程序 / 调音量 / 降噪 / DFBC / WDRC / EQ / 纯音 / 静音 / 测听）。
 *
 * 帧序列见 include/dsp_7100_cmd.h。**全部异步**：所有设置都是往步骤表里塞命令，
 * 由主循环的 dsp_7100_cmd_poll() 在 DIO13 边沿上推进（200ms tick 兜底），
 * 与读回（dsp_7100_init.c 的 dsp_7100_rb_poll）同一套模型。
 * 由 Rempro 命令处理（主循环上下文）调用启动；同一时刻只允许一个会话。 */

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
/* 测听模式寄存器 —— 测听进入 / 退出的第 ② 条命令（`A2 00 2E <00|58>`），
 * 抓包来源 tonestar.txt / tonestop.txt。
 * 2026-09-30 一度随「只切程序」被停用，同日又加回（保留命令、只砍读）。 */
#define DSP7100_REG_TONE_MODE    0x2E
#define DSP7100_TONE_MODE_ON     0x00   /* 进测听时的值 */
#define DSP7100_TONE_MODE_OFF    0x58   /* 退测听时的值 */
#define DSP7100_CMD_END          0x82

/* 应答首字节：46 = 7100 接下了这条命令；65/00 = 未就绪 / 事务错位
 * （签名含义见 dsp_7100_init.c 的读回注释）。路径 A 只认 46。 */
#define DSP7100_RSP_OK           0x46
#define DSP7100_AUDIOMETRY_PROG  3      /* 测听程序号（原 ble_rempro_cmd.c 的 AUDIOMETRY_PROG） */

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

/* 发一帧结束 82，并打印 */
static bool dsp_7100_send_end(void)
{
    uint8_t end = DSP7100_CMD_END;
    bool ok = i2c_7100_write(I2C_7100_ADDR, &end, sizeof(end));

    print_w(&end, sizeof(end), ok);
    /* 紧跟在 82 之后探一次 DIO13（见 DSP7100_DIO13_IRQ_ENABLE）：脉冲若贴在
     * 这一行后面 = 7100 收到 82 就跳；拖到下一行（延时/读之后）才出现 =
     * 7100 应用完才跳。本函数是命令路径里发 82 的唯一出口，一行覆盖全部调用者。 */
    dsp_7100_dio13_irq_poll();
    return ok;
}

/* dsp_7100_set_volume / dsp_7100_switch_program 已改成异步会话，
 * 定义在文件后半段（与其它 set_* 入口放一起）。 */

uint8_t dsp_7100_get_program(void)      { return s_cur_prog; }
uint8_t dsp_7100_get_volume_level(void) { return s_cur_vol_level; }


/* ============================================================================
 * 参数设置会话（降噪 / DFBC / EQ / WDRC / 切程序 / 调音量 / 测听 / 纯音 / 静音）
 *
 * 会话序列（2026-10-08 起**按抓包补齐 0x03 探测**，见开发文档 §25）：
 *   探测 → 静音 → 选程序 → 探测 → 写块准备 → 写块 → confirm → 探测 → 探测
 *   → 解除静音 → 选回程序0 → 探测 → commit → 探测      （DFBC = 14 条）
 *
 * ⚠ 2026-09-30 起**全部异步**：命令表 + 主循环单点推进，200ms tick 只兜底。
 * ⚠ 2026-10-08 起分两条路径（见开发文档 §24）：
 *     路径 A 配置族（降噪 / DFBC / EQ / WDRC）—— 写帧 → 等 A7_ACK_WAIT_MS
 *            → 读 3B → 校验 46 → 82 → 等下降沿；读不到 46 就补发 82 后重读
 *     路径 B 交互族（切程序 / 调音量 / 测听 / 纯音 / 静音）—— 写帧 → 82 → 等下降沿
 *   分族的原因与实测依据全在开发文档 §24；一句话：**A7 写帧没有 DIO13 回铃**，
 *   所以「等边沿再读」不可行，配置族的读只能靠固定延时定位。
 * ========================================================================== */

/* 最长会话 = WDRC 全通道单参数：6 条 0x03 探测 + 静音+选程序+提交+解除+选回+commit
 *            + 16 通道×2 命令 = 44 条
 * （EQ 每段 2 通道 = 20 条；命令缓冲 44×10B = 440B，取 640 留余量） */
#define A7_MAX_CMDS      56
#define A7_CMD_BUF_SZ    640

/* 应答长度**按步区分**（见开发文档 §25）：
 *   setter（addr 0x0000）应答 3B：`46 00 00` = 头 + 地址回显，**零数据字节**
 *   0x03 探测（addr 0x0003）应答 6B：`46 03 00 <x> <busy> 1A`
 * 读少了会把尾巴留在 7100 里，下一条读就错位 —— 每步必须读满自己的长度。 */
#define A7_RX_ACK        3
#define A7_RX_PROBE      6
#define A7_RX_MAX        6      /* 读缓冲上限 */

/* 路径 A 读应答的两个参数（2026-10-08，依据见开发文档 §24.1、§25.2）
 *   A7_ACK_WAIT_MS   写帧后等这么久再读。参考抓包（p0dfbc0-1.csv）实测的
 *                    写帧→读应答间隔：setter 8 条中 6 条落在 44~50ms，另有 71ms、
 *                    98ms 各一条；0x03 探测 94~98ms；首条 250ms（冷启动）。
 *                    ⚠ 不是按命令族分档 —— `A7 02 … 12 01` 是 setter 却 98ms。
 *                    ⚠ 它量的**只是参考 RSL10 自己的读节奏**（读→读间隔基本是
 *                      50ms 台阶），不等于 7100 的应答就绪时间。
 *                    ⚠ A7 写帧**没有** DIO13 回铃（实测），这里只能等固定时间。
 *   A7_ACK_RETRY_MAX 一条命令最多重读几次（照读回：补发 82 后锁住，只等 200ms tick）。
 *                    读回那边无上限（开机一次性），命令会话跑在运行期，必须封顶 ——
 *                    否则一条命令会永久占着 I2C，读回和后续设置全被挡住。 */
#define A7_ACK_WAIT_MS     50
#define A7_ACK_RETRY_MAX   3

#define A7_PROBE_ADDR    0x0003 /* 0x03 状态探测的目标地址（抓包：A7 01 00 03 00 02） */
#define A7_PROBE_VAL     0x02   /* 该帧的 1 字节数据 */

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
#define A7_KIND_PROG         6   /* 切程序（A2 五帧） */
#define A7_KIND_VOL          7   /* 调音量（A2 三帧） */
#define A7_KIND_AUDIO_ENTER  8   /* 进测听：切程序3 + 0x2E=00 + 解除静音（三条，都不读） */
#define A7_KIND_AUDIO_EXIT   9   /* 退测听：切回原程序 + 0x2E=58 + 解除静音 */

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

/* EQ 三段 → WDRC 通道（数组下标，非通道号）
 *   低音 {1,2}  |  中音 {3,4}  |  高音 {6,7}   —— 下标 0、5 不参与 EQ */
static const uint8_t s_eq_ch_low[]  = { 1, 2 };
static const uint8_t s_eq_ch_mid[]  = { 3, 4 };
static const uint8_t s_eq_ch_high[] = { 6, 7 };

static const uint8_t *s_eq_ch[3]   = { s_eq_ch_low, s_eq_ch_mid, s_eq_ch_high };
static const uint8_t  s_eq_ch_n[3] = { 2, 2, 2 };

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

/* 一条命令 = 一个步骤，两步相位（写帧 → 读回+82），与读回
 * dsp_7100_init.c 的 RB_SEND / RB_READ 完全同构。
 *
 * 2026-10-08 起分两条路径（见开发文档 §24）：步表只描述「发什么」，
 * 读不读由**会话族**（s_sess_family）决定，每族一个执行函数。
 *
 *   路径 A（配置族：降噪 / DFBC / EQ / WDRC）—— 要读应答
 *     写帧 → 等 A7_ACK_WAIT_MS → 读 rlen B → 校验 46 → 82 → 等下降沿
 *     读到的不是 46 就重读（照读回：补发 82 后锁住，只等 200ms tick）
 *     rlen 按步区分：setter 3B，0x03 探测 6B（见 A7_RX_ACK / A7_RX_PROBE）
 *
 *   路径 B（交互族：切程序 / 调音量 / 测听 / 纯音 / 静音）—— 不读应答
 *     写帧 → 等 wait_ms（多为 0）→ 82 → 等下降沿
 *
 *   ⚠ 两条路径**都不能**「写完等 DIO13 上升沿再读」——
 *     2026-10-08 上板实测：A7 写帧（7 种形状全试过）**产生零边沿**，
 *     DIO13 只在 `82` 之后 0~2ms 出现一对 rise+fall。见开发文档 §24.1。
 */
typedef struct
{
    const uint8_t *data;     /* 写帧字节；NULL = 本条只读 */
    uint16_t       wlen;     /* 写帧长度；0 = 不写 */
    uint16_t       wait_ms;  /* 写帧后的固定延时（给 7100 消化时间）：
                              *   路径 A 不用本字段 —— 统一走 A7_ACK_WAIT_MS
                              *   路径 B 测听用 80 / 2，其余 0（写完立刻 82） */
    uint8_t        rlen;     /* 路径 A 读几字节：setter = A7_RX_ACK(3)，
                              *   0x03 探测 = A7_RX_PROBE(6)。路径 B 不用 */
} a7_step_t;

static a7_step_t s_steps[A7_MAX_CMDS];
static uint8_t   s_step_cnt;
static uint8_t   s_step_idx;
static bool      s_sess_active;

/* ---- 推进状态（照 dsp_7100_init.c 的 s_rb_st / s_rb_wait_fall / s_rb_timeout）----
 * 相位与门控：
 *   CMD_ST_SEND 发命令 —— 有前序 82 时等它的**下降沿**（7100 吃下了才发下一条）
 *   CMD_ST_READ 读应答 —— 路径 A 直接读（发帧时已阻塞等满 A7_ACK_WAIT_MS）；
 *                         上次读失败（s_cmd_retry_lock）时只等 tick 再重读
 * 200ms tick 只置 s_cmd_timeout 兜底，真正步进在主循环的 dsp_7100_cmd_poll()。
 *
 * ⚠ 命令层**只认 82 的下降沿**，不再有「等上升沿」的门 ——
 *   A7 写帧没有上升沿（实测），原来那套 s_cmd_read_gate / s_cmd_wait_rise 已删。 */
#define CMD_ST_SEND   0
#define CMD_ST_READ   1

static uint8_t  s_cmd_st;
static uint8_t  s_cmd_send_gate;    /* 1 = SEND 前等上一条 82 的下降沿 */
static uint8_t  s_cmd_retry_lock;  /* 1 = 上次读没拿到 46：本相位不再认边沿，只等 tick 重读 */
static uint8_t  s_cmd_retry_cnt;   /* 本步已重读次数；达 A7_ACK_RETRY_MAX 判本步失败 */
static uint8_t  s_cmd_timeout;      /* 200ms tick 兜底标志 */
static uint32_t s_cmd_wait_fall;    /* 发本条 82 前记的下降沿数 */

static uint8_t   s_cmd_buf[A7_CMD_BUF_SZ];
static uint16_t  s_cmd_used;

/* 会话元信息（成功后回写缓存用） */
static uint8_t   s_sess_prog;       /* 1-4 */
static uint8_t   s_sess_kind;       /* A7_KIND_* */
static uint8_t   s_sess_val;        /* 降噪档位 / DFBC 开关 / EQ 段 / WDRC 参数选择 */
static int8_t    s_sess_db;         /* EQ 段本次下发的绝对值（±dB） */
static int16_t   s_eq_delta;        /* EQ 本次相对上次的差值 = 本次绝对值 − 上次保存值 */
/* WDRC 会话的待写项。**必须拷一份**：会话是异步跑的，
 * 调用方的数组（BLE 负载缓冲）到收尾回写缓存时可能已经失效。 */
static dsp_7100_wdrc_item_t s_wdrc_items[DSP7100_WDRC_CH];
static uint8_t              s_wdrc_n;

/* 纯音会话的频点与电平（异步跑，拷一份） */
static uint16_t s_tone_freq;
static uint8_t  s_tone_db;

static bool s_build_fail;           /* 命令表/缓冲放不下时置位 */

/* 会话族：决定这条命令读不读应答（2026-10-08，见开发文档 §24）。
 * 由 a7_build_session() 按 kind 置位，dsp_7100_cmd_step() 据此选执行函数。
 *   配置族（降噪 / DFBC / EQ / WDRC）→ 路径 A：读 3B、校验 46、失败重读
 *   交互族（切程序 / 调音量 / 测听 / 纯音 / 静音）→ 路径 B：不读
 *
 * ⚠ 两族的帧形状**完全一样**（都是「写帧 + 82」），区别只在中间有没有那一次读，
 *   所以必须显式记族 —— §23 那版想从 rlen / wait_rise 推语义，被实测推翻了。
 * ⚠ 为什么交互族不读：A2 的应答在本工程从未读到可信内容 —— 实测 77ms 时 7100
 *   回 `65 01 00`（未就绪），固定长度盲读还会把后面的事务读错位。见开发文档 §22.2。 */
#define A7_FAM_NOREAD    0
#define A7_FAM_READ      1
static uint8_t s_sess_family;

/* 从命令缓冲切一段并挂到步骤表 */
static uint8_t *a7_put(uint16_t len)
{
    uint8_t *p;

    if (s_step_cnt >= A7_MAX_CMDS) { s_build_fail = true; return NULL; }
    if ((uint32_t)s_cmd_used + len > A7_CMD_BUF_SZ) { s_build_fail = true; return NULL; }

    p = s_cmd_buf + s_cmd_used;
    s_cmd_used = (uint16_t)(s_cmd_used + len);
    s_steps[s_step_cnt].data    = p;
    s_steps[s_step_cnt].wlen    = len;
    s_steps[s_step_cnt].wait_ms = 0;   /* 路径 B 由调用方改写；路径 A 不用本字段 */
    s_steps[s_step_cnt].rlen    = A7_RX_ACK;  /* 默认 setter 长度；0x03 探测由 a7_add_probe 改写 */
    s_step_cnt++;
    return p;
}

/* A2 写帧 `A2 00 <reg> <val>` + 写后固定延时。
 * wait_ms 给 7100 消化时间：切程序 / 调音量传 0（立刻发 82），
 * 测听第 ① 句传 A7_PROG_LOAD_MS(80ms，程序加载窗口)、第 ② 句传 A7_A2_WAIT_MS(2ms)。
 * A2 没有 DIO13 回铃（实测），所以只能等固定时间 —— 但**只有路径 B 用得到本函数**。 */
static void a7_add_a2(uint8_t reg, uint8_t val, uint16_t wait_ms)
{
    uint8_t *p = a7_put(4);

    if (p == NULL) return;
    p[0] = DSP7100_CMD_PARAM_WRITE; p[1] = 0x00; p[2] = reg; p[3] = val;
    s_steps[s_step_cnt - 1].wait_ms = wait_ms;
}

/* A7 01 00 00 00 <opt>（静音 25 / 解除静音 26 / 提交 0C）。
 * 读不读由会话族决定：配置族会话里这几句照样读（抓包 p0dfbc0-1 的
 * TX003/TX027/TX036 后面都跟着一次读，回 46 00 00）。 */
static void a7_add_simple(uint8_t opt)
{
    uint8_t *p = a7_put(6);

    if (p == NULL) return;
    p[0] = 0xA7; p[1] = 0x01; p[2] = 0x00;
    p[3] = 0x00; p[4] = 0x00; p[5] = opt;
}

/* 0x03 状态探测：`A7 01 00 03 00 02` —— 写 1 字节 0x02 到地址 0x0003，读回 6B。
 * 配置族会话里插 6 条，位置照抓包（见开发文档 §25）：
 *   预检 → [静音 选程序] → 等空闲 → [写块] → 等空闲 ×2 → [解除静音 选回] → 收尾前 → [commit] → 收尾
 * 参考：p0dfbc0-1 的 TX000/009/021/024/033/039；降噪抓包同构；WDRC 设置文档 §4。
 *
 * 应答 `46 03 00 <x> <busy> 1A`。忙标志是哪个字节**两份文档说法不一**
 * （WDRC §4 记第 5 字节 = <busy>；DFBC设置 §2 记作第 4 字节），未验证 ——
 * 本版**只发不判**，6 字节原样进日志，等上板看哪个字节在主写块期间跳 01。 */
static void a7_add_probe(void)
{
    uint8_t *p = a7_put(6);

    if (p == NULL) return;
    p[0] = 0xA7; p[1] = 0x01; p[2] = 0x00;
    p[3] = (uint8_t)(A7_PROBE_ADDR & 0xFF); p[4] = (uint8_t)(A7_PROBE_ADDR >> 8);
    p[5] = A7_PROBE_VAL;
    s_steps[s_step_cnt - 1].rlen = A7_RX_PROBE;
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
 * 抓包：tone*hz*db.txt（15 条），波形是 写帧 → 读 3B → 82。
 *
 * ⚠ 2026-09-30 起**不读应答**：写帧完直接 82，与切程序/调音量同形（属交互族）。
 *   代价同 §22 —— 命令不再被校验，ok=1 只代表 I2C 写成功。
 *   （原注释写「A7 写本来有 DIO13 回铃，可等」—— **2026-10-08 实测推翻**：
 *     A7 写帧零边沿，见开发文档 §24.1。） */
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

/* 取该段上次下发的 EQ 绝对值（band 0=低音 1=中音 2=高音） */
static int8_t a7_eq_prev(const dsp_7100_prog_t *p, uint8_t band)
{
    if (band == 0) return p->eq_low;
    if (band == 1) return p->eq_mid;
    return p->eq_high;
}

/* A2 写帧后的固定延时 —— 前提是**实测出来的协议事实**：
 *   A2 命令 7100 不产生 DIO13 回铃（实测「A1/A2 写和纯读命令一个都不跳」，
 *   切程序那次 32.924 写 → 33.004 之间也是零边沿）→ 只能等固定时间，不能等上升沿。
 *   80ms = 程序加载窗口（实测该窗口内 DIO13 零边沿，纯等边沿会卡住）—— 测听第 ① 句用；
 *   2ms  = 原 set_volume / set_tone_mode 的帧间间隔 —— 测听第 ② 句（0x2E）用。
 * （切程序 / 调音量 / 纯音不走这两个值，它们是「写完立刻 82」。） */
#define A7_A2_WAIT_MS     2
#define A7_PROG_LOAD_MS   80

/* 会话骨架（2026-10-08 起按抓包补齐 6 条 0x03 探测，见开发文档 §25）：
 *   探测 → 静音 → 选程序 → 探测 → [写块] → confirm → 探测 → 探测
 *   → 解除静音 → 选回程序0 → 探测 → commit → 探测
 * 写块由 kind 决定。位置逐条对照 p0dfbc0-1 与 7100_A7协议_noise0-1（两抓包同构）。 */
static bool a7_build_session(uint8_t kind, uint8_t prog, uint8_t val)
{
    s_step_cnt = 0;
    s_step_idx = 0;
    s_cmd_used = 0;
    s_build_fail = false;
    /* 会话族 → 走哪条路径（见文件头与开发文档 §24）。
     * 配置族要读应答（路径 A）；切程序 / 调音量 / 测听 / 纯音 / 静音不读（路径 B）。 */
    s_sess_family = (kind == A7_KIND_DENOISE || kind == A7_KIND_DFBC ||
                     kind == A7_KIND_WDRC    || kind == A7_KIND_EQ)
                    ? A7_FAM_READ : A7_FAM_NOREAD;

    if (kind == A7_KIND_MUTE) {             /* 静音 / 解除静音：同样只发这一条 */
        a7_add_simple(val ? A7_OPT_MUTE : A7_OPT_UNMUTE);
        return !s_build_fail;
    }

    if (kind == A7_KIND_TONE) {             /* 纯音：只发这一条（写帧 + 82，不读应答） */
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

    if (kind == A7_KIND_VOL) {              /* 调音量：只发 A2 写帧 + 立刻 82（路径 B） */
        a7_add_a2(DSP7100_REG_VOLUME, s_volume_value[val - 1], 0);
        return !s_build_fail;
    }

    /* 切程序：一条 A2 写帧 + 立刻 82（路径 B） */
    if (kind == A7_KIND_PROG) {
        a7_add_a2(DSP7100_REG_PROGRAM, prog, 0);
        return !s_build_fail;
    }

    /* 测听进入 / 退出：**三条命令编在一个会话里**，照 tonestar / tonestop 抓包的顺序：
     *   ① 切程序   `A2 00 16 <prog>` → 等 80ms → 82
     *        enter → DSP7100_AUDIOMETRY_PROG(3)，由 dsp_7100_audiometry() 算好传进来
     *        exit  → 进测听前那个程序
     *   ② 测听位   `A2 00 2E <00|58>` → 等 2ms  → 82   （00 = 进，58 = 退）
     *   ③ 解除静音 `A7 01 00 00 00 26`        → 立刻  → 82
     * ⚠ 2026-09-30 起三句都**只发命令、不读应答**（原波形里每句后面都跟着一次读），
     *   但**延时照留** —— 80ms 是 switch_program 的程序加载窗口，2ms 是 set_tone_mode
     *   的帧间间隔，都取自 HEAD 原值。第 ③ 句原来靠等 A7 回铃，按「只砍读、不另加等待」
     *   处理成写完立刻 82（与切程序 / 调音量 / 纯音一致）。 */
    if (kind == A7_KIND_AUDIO_ENTER || kind == A7_KIND_AUDIO_EXIT) {
        a7_add_a2(DSP7100_REG_PROGRAM, prog, A7_PROG_LOAD_MS);
        a7_add_a2(DSP7100_REG_TONE_MODE,
                  (kind == A7_KIND_AUDIO_ENTER) ? DSP7100_TONE_MODE_ON
                                                : DSP7100_TONE_MODE_OFF,
                  A7_A2_WAIT_MS);
        a7_add_simple(A7_OPT_UNMUTE);
        return !s_build_fail;
    }

    a7_add_probe();                       /* ① 预检（抓包恒为第一条） */
    a7_add_simple(A7_OPT_MUTE);
    a7_add_prog(A7_OPT_SELECT, prog);
    a7_add_probe();                       /* ② 等空闲：选程序后 */

    if (kind == A7_KIND_DENOISE) {          /* 降噪：块 09 + A7 27(300B) */
        a7_add_prep(A7_BLK_DENOISE, prog, 0x00, 0x01);
        a7_add_noise_block(val);
    } else if (kind == A7_KIND_DFBC) {      /* DFBC：块 0A + A7 04(08 00 00 X)
                                             * 本会话 14 步全走路径 A（读应答） */
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

        /* 缓存里的 LL/HL 就是设备当前值，本次只加"与上次 EQ 的差值" */
        s_eq_delta = (int16_t)s_sess_db - (int16_t)a7_eq_prev(bp, val);

        n = s_eq_ch_n[val];
        for (i = 0; i < n; i++) {
            uint8_t  ch  = s_eq_ch[val][i];             /* 0-based */
            int32_t  ll  = (int32_t)bp->wdrc_ll[ch] + s_eq_delta;
            int32_t  hl  = (int32_t)bp->wdrc_hl[ch] + s_eq_delta;

            if (ll < A7_WDRC_LL_MIN) ll = A7_WDRC_LL_MIN;
            if (ll > A7_WDRC_LL_MAX) ll = A7_WDRC_LL_MAX;
            if (hl < A7_WDRC_HL_MIN) hl = A7_WDRC_HL_MIN;
            if (hl > A7_WDRC_HL_MAX) hl = A7_WDRC_HL_MAX;

            a7_add_wdrc_param(prog, A7_WDRC_LL_ADDR(ch + 1), (uint8_t)ll);
            a7_add_wdrc_param(prog, A7_WDRC_HL_ADDR(ch + 1), (uint8_t)(int8_t)hl);
        }
    }

    a7_add_prog(A7_OPT_CONFIRM, prog);
    a7_add_probe();                       /* ③ 等空闲：提交后 ×2（抓包连续两条） */
    a7_add_probe();                       /* ④ */
    a7_add_simple(A7_OPT_UNMUTE);
    a7_add_prog(A7_OPT_SELECT, 1);        /* 指针复位回程序0（照抓包恒为 01） */
    a7_add_probe();                       /* ⑤ 选回程序后、commit 前 */
    a7_add_simple(A7_OPT_COMMIT);
    a7_add_probe();                       /* ⑥ 收尾 */

    return !s_build_fail;                 /* 无溢出即构建成功 */
}

/* 会话成功后把新值同步进 RAM 缓存（不写 flash）。
 * 调用方已排除 TONE / MUTE / PROG / VOL / 测听 —— 只剩会改参数的四种。 */
static void a7_session_sync_cache(void)
{
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
    } else {                   /* EQ：把本次写入的 LL/HL 与 EQ 绝对值一起落到缓存 */
        dsp_7100_prog_t *p = &dsp_7100_rb_bufs()->prog[s_sess_prog - 1];
        uint8_t n = s_eq_ch_n[s_sess_val];
        uint8_t i;

        for (i = 0; i < n; i++) {
            uint8_t  ch = s_eq_ch[s_sess_val][i];
            int32_t  ll = (int32_t)p->wdrc_ll[ch] + s_eq_delta;
            int32_t  hl = (int32_t)p->wdrc_hl[ch] + s_eq_delta;

            if (ll < A7_WDRC_LL_MIN) ll = A7_WDRC_LL_MIN;
            if (ll > A7_WDRC_LL_MAX) ll = A7_WDRC_LL_MAX;
            if (hl < A7_WDRC_HL_MIN) hl = A7_WDRC_HL_MIN;
            if (hl > A7_WDRC_HL_MAX) hl = A7_WDRC_HL_MAX;

            p->wdrc_ll[ch] = (int8_t)ll;
            p->wdrc_hl[ch] = (int8_t)hl;
        }

        if (s_sess_val == 0)      p->eq_low  = s_sess_db;
        else if (s_sess_val == 1) p->eq_mid  = s_sess_db;
        else                      p->eq_high = s_sess_db;
    }
}

/* 会话收尾：成功后同步 RAM 参数并请求落盘（否则下次开机缓存显示旧值） */
static void a7_session_finish(bool ok)
{
    s_sess_active = false;
    PRINTF("[7100] --- session done ok=%u ---\r\n", ok);

    if (!ok) return;

    /* 切程序 / 测听：只更新当前程序号，不碰读回缓存里的任何参数 */
    if (s_sess_kind == A7_KIND_PROG || s_sess_kind == A7_KIND_AUDIO_ENTER ||
        s_sess_kind == A7_KIND_AUDIO_EXIT) {
        s_cur_prog = s_sess_prog;
        return;
    }
    if (s_sess_kind == A7_KIND_VOL) {
        s_cur_vol_level = s_sess_val;
        return;
    }

    /* 纯音 / 静音**不改缓存里的任何参数** → 直接返回。
     * 否则会走 cache_save_request()，把整份读回缓存（4 个程序）重写一遍 flash ——
     * 测听时每播一个频点就擦写一次，纯属浪费 + 磨损 flash。 */
    if (s_sess_kind == A7_KIND_TONE || s_sess_kind == A7_KIND_MUTE) return;

    a7_session_sync_cache();
    dsp_7100_cache_save_request();
}

/* 本步收尾：记下降沿 → 发 82 → 交给下一条。
 * 返回 true = 会话还有下一条；false = 本步已终结（I2C 失败 / 整会话跑完），
 * 两种情况都已经调过 a7_session_finish()，调用方直接 return 即可。 */
static bool a7_step_advance(void)
{
    /* 先记下降沿数再发 82 —— 这条 82 的下降沿此刻还没到，等它到了就能立刻发下一条 */
    s_cmd_wait_fall = dsp_7100_dio13_fall_cnt();
    if (!dsp_7100_send_end()) { a7_session_finish(false); return false; }

    s_cmd_st         = CMD_ST_SEND;
    s_cmd_send_gate  = 1;      /* 下一条要等这条 82 的下降沿 */
    s_cmd_retry_lock = 0;
    s_cmd_retry_cnt  = 0;
    s_cmd_timeout    = 0;
    s_step_idx++;
    if (s_step_idx >= s_step_cnt) { a7_session_finish(true); return false; }
    return true;
}

/* ---- 路径 A：配置族（降噪 / DFBC / EQ / WDRC）—— 写帧 → 等 → 读 rlen B → 校验 → 82 ----
 * 与读回 dsp_7100_rb_seq_tick 同构，只少一次 payload 读（应答本身就是读到的全部）。
 * ⚠ 读之前只能等固定时间：A7 写帧**没有** DIO13 回铃（实测，见 §24.1）。
 * ⚠ 读失败时**不重发写帧**，只重读 —— 与读回一致（读回失败后停在 RB_READ 重读）。 */
static void a7_step_read(void)
{
    const a7_step_t *s = &s_steps[s_step_idx];
    uint8_t rx[A7_RX_MAX];       /* 缓冲按最长算，实际读 s->rlen 字节 */
    bool    ok;

    if (s_cmd_st == CMD_ST_SEND) {
        if (s->wlen > 0) {
            ok = i2c_7100_write(I2C_7100_ADDR, s->data, s->wlen);
            print_w(s->data, s->wlen, ok);
            if (!ok) { a7_session_finish(false); return; }
            /* ⚠ 阻塞忙等、且不喂狗（见开发文档 §22.8）。50ms 与测听那条 80ms 同量级。 */
            i2c_7100_delay_ms(A7_ACK_WAIT_MS);
        }
        s_cmd_st = CMD_ST_READ;
        return;
    }

    ok = i2c_7100_read(I2C_7100_ADDR, rx, s->rlen);   /* 必须读满本步长度，否则 7100 里留尾巴 */
    print_r(rx, s->rlen, ok);
    if (ok && rx[0] == DSP7100_RSP_OK) {        /* 46 = 7100 接下了这条 */
        (void)a7_step_advance();
        return;
    }

    /* 没拿到 46：本步补发 82 收尾（照读回 —— 失败那步的 82 也照发），再等 tick 重读 */
    PRINTF("[7100] step%u/%u ACK FAIL hdr=%02X %02X %02X (retry %u/%u)\r\n",
           s_step_idx, s_step_cnt, rx[0], rx[1], rx[2],
           s_cmd_retry_cnt, A7_ACK_RETRY_MAX);
    s_cmd_wait_fall = dsp_7100_dio13_fall_cnt();
    if (!dsp_7100_send_end()) { a7_session_finish(false); return; }

    if (s_cmd_retry_cnt >= A7_ACK_RETRY_MAX) {
        PRINTF("[7100] step%u/%u 重读 %u 次仍无 46，会话判失败\r\n",
               s_step_idx, s_step_cnt, (unsigned)s_cmd_retry_cnt);
        a7_session_finish(false);
        return;
    }
    s_cmd_retry_cnt++;
    s_cmd_retry_lock = 1;       /* 停在读相位重读；本相位只等 200ms tick（同读回 s_rb_retry） */
    s_cmd_timeout    = 0;
    s_cmd_st         = CMD_ST_READ;
}

/* ---- 路径 B：交互族（切程序 / 调音量 / 测听 / 纯音 / 静音）—— 写帧 → 等 wait_ms → 82 ----
 * **不读应答**：A2 的应答读不出可信内容（见 s_sess_family 的注释与开发文档 §22.2）。
 * 代价明确：命令不被校验，ok=1 只代表 I2C 写成功。 */
static void a7_step_noread(void)
{
    const a7_step_t *s = &s_steps[s_step_idx];

    if (s_cmd_st == CMD_ST_SEND) {
        if (s->wlen > 0) {
            bool ok = i2c_7100_write(I2C_7100_ADDR, s->data, s->wlen);
            print_w(s->data, s->wlen, ok);
            if (!ok) { a7_session_finish(false); return; }
        }
        /* 测听第 ① 句 80ms（程序加载窗口）、第 ② 句 2ms；切程序 / 调音量 / 纯音传 0 */
        if (s->wait_ms > 0) i2c_7100_delay_ms(s->wait_ms);
        s_cmd_st = CMD_ST_READ;
        return;
    }

    (void)a7_step_advance();
}

/* 推进一步相位（只由主循环 dsp_7100_cmd_poll 调）：按会话族选路径 */
static void dsp_7100_cmd_step(void)
{
    if (s_sess_family == A7_FAM_READ) a7_step_read();
    else                              a7_step_noread();
}

/* 200ms tick（app_process.c）调：只置超时标志，**不推进** —— 推进统一由
 * dsp_7100_cmd_poll 做（与读回 dsp_7100_rb_tick 的分工完全相同）。 */
void dsp_7100_cmd_tick(void)
{
    s_cmd_timeout = 1;
}

/* 主循环（app.c）：命令会话**唯一**的推进点。
 *   SEND  等上一条 82 的**下降沿**（7100 吃下了才发下一条）—— 实测 0~2ms
 *   READ  路径 A 读失败重试时只等 tick（不认边沿，同读回 s_rb_retry）；
 *         其余情况直接进（写帧时已阻塞等满 A7_ACK_WAIT_MS）
 * 边沿/tick 都等不到时退回 200ms tick 兜底 —— 与 dsp_7100_rb_poll 同一套写法。
 *
 * ⚠ 命令层**不再有「等上升沿」的门**：A7 写帧零边沿（实测，见 §24.1），
 *   原来那套 s_cmd_read_gate / s_cmd_wait_rise 已删。 */
void dsp_7100_cmd_poll(void)
{
    if (!s_sess_active) { s_cmd_timeout = 0; return; }
    /* 步骤表空了（构建失败时不会置 active，这里是兜底）：别拿越界下标去发 I2C */
    if (s_step_idx >= s_step_cnt) { a7_session_finish(true); return; }

    if (s_cmd_st == CMD_ST_SEND) {
        if (s_cmd_send_gate && !s_cmd_timeout &&
            dsp_7100_dio13_fall_cnt() == s_cmd_wait_fall) return;
    } else if (s_cmd_retry_lock && !s_cmd_timeout) {
        return;     /* 重读：不认边沿，只等 200ms tick —— 照读回，否则一步能自激成风暴 */
    }

    s_cmd_timeout = 0;
    dsp_7100_cmd_step();
}

bool dsp_7100_cmd_busy(void)
{
    return s_sess_active;
}

static const char *a7_kind_name(uint8_t kind)
{
    if (kind == A7_KIND_DENOISE)     return "Denoise";
    if (kind == A7_KIND_DFBC)        return "DFBC";
    if (kind == A7_KIND_WDRC)        return "WDRC";
    if (kind == A7_KIND_TONE)        return "Tone";
    if (kind == A7_KIND_MUTE)        return "Mute";
    if (kind == A7_KIND_PROG)        return "SwitchProgram";
    if (kind == A7_KIND_VOL)         return "SetVolume";
    if (kind == A7_KIND_AUDIO_ENTER) return "AudiometryOn";
    if (kind == A7_KIND_AUDIO_EXIT)  return "AudiometryOff";
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
    /* 推进状态复位：第一条命令没有前序 82 → 不等下降沿，直接发 */
    s_cmd_st         = CMD_ST_SEND;
    s_cmd_send_gate  = 0;
    s_cmd_retry_lock = 0;
    s_cmd_retry_cnt  = 0;
    s_cmd_timeout    = 0;
    PRINTF("[7100] --- session start: %s prog=%u val=%u db=%d cmds=%u ---\r\n",
           a7_kind_name(kind), prog, val, s_sess_db, s_step_cnt);
    return true;
}

/* 切程序（异步）。序列：`A2 00 16 <prog>` → `82`（2 帧，不读应答）。见 §22。 */
bool dsp_7100_switch_program(uint8_t prog)
{
    if (prog < 1 || prog > 4) return false;
    PRINTF("[7100] --- SwitchProgram prog=%u ---\r\n", prog);
    s_sess_db = 0;
    return a7_session_start(A7_KIND_PROG, prog, 0);
}

/* 调音量（异步）。序列：`A2 00 12 <0x11…0x64>` → `82`（2 帧，不读应答）。见 §22。 */
bool dsp_7100_set_volume(uint8_t level)
{
    if (level < 1 || level > 6) return false;
    PRINTF("[7100] --- SetVolume level=%u ---\r\n", level);
    s_sess_db = 0;
    return a7_session_start(A7_KIND_VOL, dsp_7100_get_program(), level);
}

/* 测听进入 / 退出（异步）。三条命令一套会话，见 a7_build_session 的 AUDIO 分支：
 *   enter: 切到 DSP7100_AUDIOMETRY_PROG(3) → 0x2E=00 → 解除静音
 *   exit : 切回 prev_prog（进测听前那个程序）→ 0x2E=58 → 解除静音
 * 三句都只发命令不读应答，延时照留（80ms / 2ms / 0）。 */
bool dsp_7100_audiometry(bool enter, uint8_t prev_prog)
{
    uint8_t prog = enter ? DSP7100_AUDIOMETRY_PROG : prev_prog;

    if (prog < 1 || prog > 4) return false;
    PRINTF("[7100] --- Audiometry %s prog=%u ---\r\n", enter ? "on" : "off", prog);
    s_sess_db = 0;
    return a7_session_start(enter ? A7_KIND_AUDIO_ENTER : A7_KIND_AUDIO_EXIT,
                            prog, 0);
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

/* 三段均衡器：band 0=低音 1=中音 2=高音，db = App 下发的**绝对值**（±dB）。
 * 实际写入 = 设备当前值 + (本次绝对值 − 上次保存的绝对值)，LL 与 HL 同时平移。
 * 缓存里的 LL/HL 与 EQ 绝对值都是设备实际值，掉电保存在 flash。 */
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

/* 测听模式寄存器 `0x2E`（`A2 00 2E 00` 进 / `A2 00 2E 58` 退）与解除静音 `A7 …26`
 * 都是 dsp_7100_audiometry() 那个会话里的第 ② / ③ 条，**没有独立的公共入口** ——
 * 外层不要再单独调 dsp_7100_set_mute()，会撞上「同一时刻只允许一个会话」。
 * 来源：tonestar.txt / tonestop.txt 抓包，见 docs/7100协议/纯音测听.md */

/* 静音 / 解除静音 —— 单命令会话（`A7 01 00 00 00 25` / `26`），无公共帧。
 * 对应 BLE 的 SetMuteData (21)：mute=true → 静音。
 * 参考设备也这么单发：tonestar/tonestop 抓包里 `A7 01 00 00 00 26` 就是独立一条（写→读→82）。 */
bool dsp_7100_set_mute(bool mute)
{
    PRINTF("[7100] --- %s ---\r\n", mute ? "mute" : "unmute");
    s_sess_db = 0;
    return a7_session_start(A7_KIND_MUTE, dsp_7100_get_program(), mute ? 1u : 0u);
}
