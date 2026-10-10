/* 7100 运行时命令层（切程序 / 调音量 / 降噪 / DFBC / WDRC / EQ / 纯音 / 静音 / 测听）。
 *
 * 帧序列见 include/dsp_7100_cmd.h。**全部异步**：所有设置都是往步骤表里塞命令，
 * 由主循环的 dsp_7100_cmd_poll() 在 DIO13 边沿上推进（200ms tick 兜底），
 * 与读回（dsp_7100_init.c 的 dsp_7100_rb_poll）同一套模型。
 * 由 Rempro 命令处理（主循环上下文）调用启动；同一时刻只允许一个会话。 */

#include "dsp_7100_cmd.h"
#include "dsp_7100_init.h"
#include "dsp_7100_sniff.h"   /* dsp_7100_sniff_rebase()：会话收尾时追平 sniff 的触发基准 */
#include "ble_rempro_cmd.h"   /* rempro_push_7100_notify()：路径 B 读到的通知顺手上报 */
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

#define DSP7100_AUDIOMETRY_PROG  3      /* 测听程序号（原 ble_rempro_cmd.c 的 AUDIOMETRY_PROG） */

/* 音量 7 档（App 档位 0-6 = 7100 档位 0-6）→ 0-100 值：round(档位 * 100 / 6)
 *   = 0/17/33/50/67/83/100，**下标即档位**。档位 0 = 值 0 是最低那档（App 也能到）。 */
static const uint8_t s_volume_value[7] = {0x00, 0x11, 0x21, 0x32, 0x43, 0x53, 0x64};

static uint8_t s_cur_prog = 1;
static uint8_t s_cur_vol_level = 6;   /* 音量档位 0-6（与 App 同号）；初值 = 最大档 */

/* ---- I2C 收发打印（切程序/音量/降噪/DFBC 四条链路共用）----
 * 只打数据字节；I2C 地址字节由 HAL 在 START 后发出，不在字节流内
 * （逻辑分析仪上对应的就是日志里没显示的 0x04/0x05）。
 *
 * ⚠ 必须分块：pack printf.c 用 vsprintf 写入 200B 静态缓冲（TX_BUFFER_SIZE），
 *   无边界检查。降噪写块 300B → 900+ 字符会冲爆缓冲导致死机。
 *   I2C_DUMP_CHUNK=32 → 每行 ≤ 98 字符，加前缀 < 200。 */
#define I2C_DUMP_CHUNK 32

/* 把 len 字节写成 " AA BB CC"，返回写入的字符数（**不**补 '\0'，调用方补） */
static uint16_t hex_into(char *dst, const uint8_t *d, uint16_t len)
{
    static const char hexd[] = "0123456789ABCDEF";
    uint16_t w = 0;
    uint16_t k;

    for (k = 0; k < len; k++) {
        dst[w++] = ' ';
        dst[w++] = hexd[d[k] >> 4];
        dst[w++] = hexd[d[k] & 0x0Fu];
    }
    return w;
}

static void print_i2c(char dir, const uint8_t *d, uint16_t len, bool ok)
{
    char line[I2C_DUMP_CHUNK * 3 + 2];
    uint16_t i = 0;
    uint8_t first = 1;

    while (i < len) {
        uint16_t n = len - i;

        if (n > I2C_DUMP_CHUNK) n = (uint16_t)I2C_DUMP_CHUNK;
        line[hex_into(line, d + i, n)] = '\0';

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

/* 发一帧结束 82，并打印。82 = 「这一帧我读完了，缓冲区还你」（两条路径都调）。
 * 2026-10-09 起**非 static**：开机读回（dsp_7100_init.c）也走这里，声明见 dsp_7100_cmd.h。 */
bool dsp_7100_send_end(void)
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

/* 值 → 音量档位 0-6：与 s_volume_value 逐个比，取最接近的一档（**下标即档位**）。
 * 实测值只可能是那 7 个（`00 12 11`），取最近是为了容忍 7100 侧四舍五入的 ±1。 */
static uint8_t vol_value_to_level(uint8_t val)
{
    uint8_t best = 0;
    uint8_t best_d = 0xFFu;
    uint8_t i;

    for (i = 0; i < 7; i++) {
        uint8_t d = (val > s_volume_value[i]) ? (uint8_t)(val - s_volume_value[i])
                                              : (uint8_t)(s_volume_value[i] - val);
        if (d < best_d) {
            best_d = d;
            best   = i;
        }
    }
    return best;
}

/* DIO13 推来的通知 payload：`<addr_hi> <addr_lo> <值>`（16 位寄存器地址 + 值）。
 * 地址与写命令 `A2 00 <reg> <val>` 同一张表 → 0x0016 程序号 / 0x0012 音量。
 * 顺手更新跟踪值（本地按键改的，s_cur_prog / s_cur_vol_level 本来只在
 * RSL10 自己发起会话时才变）。语义与返回码见 dsp_7100_cmd.h。 */
uint8_t dsp_7100_notify_apply(const uint8_t *payload, uint8_t len, uint8_t *out_val)
{
    uint16_t addr;

    if (payload == NULL || out_val == NULL || len < 3) {
        return DSP7100_NOTIFY_NONE;
    }

    /* 首字节恒 0x00、寄存器在 payload[1] —— 与写帧 `A2 00 <reg> <val>` 的字节位一致
     * （a7_add_a2()：p[1]=0x00、p[2]=reg）。即地址是**大端** `00 12` = 0x0012。
     * ⚠ 别写成 `payload[0] | payload[1]<<8`（小端）：那样 `00 12` 会算成 0x1200，永不匹配。 */
    addr = ((uint16_t)payload[0] << 8) | (uint16_t)payload[1];

    if (addr == DSP7100_REG_PROGRAM) {
        if (payload[2] < 1 || payload[2] > 4) {   /* 程序号 1-4，越界当认不出 */
            return DSP7100_NOTIFY_NONE;
        }
        s_cur_prog = payload[2];
        *out_val   = s_cur_prog;
        return DSP7100_NOTIFY_PROG;
    }

    if (addr == DSP7100_REG_VOLUME) {
        s_cur_vol_level = vol_value_to_level(payload[2]);
        *out_val        = s_cur_vol_level;
        return DSP7100_NOTIFY_VOL;
    }

    return DSP7100_NOTIFY_NONE;   /* 不认识的属性（如常态帧）：不更新、不上报 */
}


/* ============================================================================
 * 参数设置会话（降噪 / DFBC / EQ / WDRC / 切程序 / 调音量 / 测听 / 纯音 / 静音）
 *
 * 会话序列（2026-10-08 起**按抓包补齐 0x03 探测**，见开发文档 §25）：
 *   探测 → 静音 → 选程序 → 探测 → 写块准备 → 写块 → confirm → 探测 → 探测
 *   → 解除静音 → 选回程序0 → 探测 → commit → 探测      （DFBC = 14 条）
 *
 * ⚠ 2026-09-30 起**全部异步**：命令表 + 主循环单点推进，200ms tick 只兜底。
 * ⚠ 2026-10-08 起分两条路径，**2026-10-09 起两族的时序已统一**（依据见 §24 / §26 / §26.9）：
 *     两族都是：写帧 → 等 7100 抬线的**上升沿** → 读头 3B + 按 len16 补读 → 82 → 等下降沿
 *     差别**只剩「认不认内容」和「等不到沿怎么收场」**：
 *       路径 A 配置族（降噪 / DFBC / EQ / WDRC）—— 校验首字节 46，不是就补发 82 重读（≤3 次）；
 *              等不到沿（连等 DSP7100_WAIT_RISE_MAX_TICKS 拍线仍低）时**盲读一次**降级收场
 *       路径 B 交互族（切程序 / 调音量 / 测听 / 纯音 / 静音）—— 只当门用，读到什么照发 82；
 *              等不到沿就判本步失败
 *   ⚠ 配置族原先走「固定 50ms 盲读」，依据是「A7 写帧没有 DIO13 回铃」（§24.1 实测）——
 *     该依据**已被推翻**：§26.9（+47ms）与 §26.11 两轮上板都看到 A7 写帧 ~10~58ms 抬线，
 *     那 50ms 只是与沿**碰巧同相**，还让每步白背一次不喂狗的忙等（WDRC n=16 累计 2.3s）。
 * ========================================================================== */

/* 最长会话 = WDRC 全通道单参数：6 条 0x03 探测 + 静音+选程序+提交+解除+选回+commit
 *            + 16 通道×2 命令 = 44 条
 * （EQ 每段 2 通道 = 20 条；命令缓冲 44×10B = 440B，取 640 留余量） */
#define A7_MAX_CMDS      56
#define A7_CMD_BUF_SZ    640

/* 读缓冲上限（头 3B + payload 3B）。
 * 读长度**不再按步区分**（2026-10-09）：头里的 len16 就是长度，payload 由它定。
 * 旧版按步硬编码的 A7_RX_ACK(3) / A7_RX_PROBE(6) 已删 —— 那两个固定值就是在这两个应答
 * 形状上凑出来的，与 len16 逐条相符（setter `46 00 00` → len16=0；0x03 探测
 * `46 03 00 <x> <busy> 1A` → len16=3），见开发文档 §25 / §26.9。 */
#define A7_RX_MAX        6

/* 一次读的**两截**（头 3B + payload）合成**一行**打（2026-10-09，调用点 a7_read_frame，两族共用）。
 * 为什么必须合：串口工具按自己的轮询周期收行，分两行打会凭空多出一个时间戳 ——
 * 实测同一段代码给出过「同一毫秒」和「差 12ms」两种结果，拿去量协议时序会看错；
 * 而且每行 @115200 ≈2.5ms 的传输时间也会把后面的 82 推后。
 * 头里的 len16 为 0（pay == 0）时退化成原来的单行头，日志形状与改造前一致。
 * ⚠ 不套 print_i2c 的分块：两截合计 ≤ A7_RX_MAX(6)B → 一行 ≤ 60 字符，离 200B 静态缓冲很远。 */
static void print_r2(const uint8_t *h, uint16_t hlen, bool okh,
                     const uint8_t *p, uint16_t plen, bool okp)
{
    char hx[A7_RX_MAX * 3 + 1];
    char px[A7_RX_MAX * 3 + 1];

    if (plen == 0) {
        print_r(h, hlen, okh);
        return;
    }
    hx[hex_into(hx, h, hlen)] = '\0';
    px[hex_into(px, p, plen)] = '\0';
    PRINTF("[7100] R (%uB ok=%u):%s | (%uB ok=%u):%s\r\n",
           hlen, okh, hx, plen, okp, px);
}

/* 路径 A 读应答的参数（2026-10-08，依据见开发文档 §24.1、§25.2）
 *   A7_ACK_RETRY_MAX 一条命令最多重读几次（照读回：补发 82 后锁住，只等 200ms tick）。
 *                    读回那边无上限（开机一次性），命令会话跑在运行期，必须封顶 ——
 *                    否则一条命令会永久占着 I2C，读回和后续设置全被挡住。
 * ⚠ 原来的 A7_ACK_WAIT_MS(50) 已删除（2026-10-09）：写帧→读应答的间隔不再靠固定延时定位，
 *   改成等 7100 抬线的上升沿（同路径 B）。那 50ms 的由来留档：参考抓包 p0dfbc0-1.csv 里
 *   setter 8 条中 6 条落在 44~50ms（另有 71 / 98ms 各一条）、0x03 探测 94~98ms、
 *   首条 250ms（冷启动）—— 它量的**只是参考 RSL10 自己的读节奏**（读→读基本 50ms 台阶），
 *   不等于 7100 的应答就绪时间。 */
#define A7_ACK_RETRY_MAX   3

/* 等 7100 抬线的上限（拍数 ×200ms）—— **值在 dsp_7100_cmd.h 的 DSP7100_WAIT_RISE_MAX_TICKS**，
 * 2026-10-09 起命令会话与开机读回共用（读回原先没有这道门）。下面是它的来历：
 * tick 兜底放行时若线**还是低的**，说明 7100 还没把回执放出来 —— 不立刻读（实测盲读会读到
 * `00 00 00` 还照发 82，之后真正的回执抬了线却没人读，那帧就滞留在 7100 里）。吞掉这一拍
 * 继续等，连等这么多拍仍线低时：**路径 B 判本步失败**（没读就不欠 credit，不发 82）；
 * **路径 A 盲读一次**降级 —— 它有 46 校验兜底，读到空帧只会走「补发 82 + 重读」，
 * 不会像路径 B 那样把空帧当成功。5 拍 = 1s：正常回执最慢也就 80~90ms（程序加载窗口）。 */

/* SEND 门的兜底读上限（开发者定值 10）—— **值在 dsp_7100_cmd.h 的 DSP7100_RESCUE_MAX**，
 * 2026-10-09 起命令会话与开机读回共用。下面是它的来历：
 *
 * 门的规则：上一条 82 的**下降沿**到了，还要求 DIO13 是**低的**才发下一条。线高说明这条
 * 82 之后 7100 又抬了线 —— 它压着一帧（日志 `rise +1/47 fall +1/46`，而人家要的是那帧被
 * 读走），那就先读掉、还 credit，再回来发下一条。见 a7_rescue_read()。
 *
 * ⚠ 上限是**必须**的（防 7100 持续有帧时把会话拖死）。语义已定案（11:05，见 §26.6）：
 *   线高 = 7100 手上真有帧，帧抽干后那条 82 只产生**下降沿**、线落回低 → 循环自然停。
 *   实测切程序后是 2 帧（0x2A/0x2E），所以正常只需兜 1~2 次，10 基本用不到。
 * ⚠ **两族共用**（2026-10-09 起，SEND 门不再分族）；第一条命令前面没有 82，不套这个门。
 *   会话**收尾**也用同一个上限，而且要循环到线低（见 a7_session_finish）。
 *   读回的 SEND 门与收尾兜底读用同两个上限（见 dsp_7100_init.c 的 dsp_7100_rb_poll）。 */

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
#define A7_KIND_AUDIO_ENTER  8   /* 进测听：切程序3 + 0x2E=00 + 解除静音（三条，路径 B 串行） */
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

/* 一条命令 = 一个步骤，两步相位（写帧 → 读回 / 收尾，按族不同），与读回
 * dsp_7100_init.c 的 RB_SEND / RB_READ 完全同构。
 *
 * 2026-10-08 起分两条路径（见开发文档 §24），**2026-10-09 起时序统一**：
 * 步表只描述「发什么」，两族都走「写帧 → 等 7100 抬线的**上升沿** → 读头 3B + 按 len16
 * 补读 → 82 → 等下降沿」。读不读由**会话族**（s_sess_family）决定，每族一个执行函数。
 *
 *   路径 A（配置族：降噪 / DFBC / EQ / WDRC）—— 要读应答、**认内容**
 *     读到的首字节不是 46 就重读（照读回：补发 82 后锁住，只等 200ms tick，≤3 次）；
 *     沿等不到（连等 DSP7100_WAIT_RISE_MAX_TICKS 拍线仍低）时**盲读一次**降级收场。
 *
 *   路径 B（交互族：切程序 / 调音量 / 测听 / 纯音 / 静音）—— 也读、也发 82，只当门用
 *     不校验内容；沿等不到就判本步失败（没读就不欠 credit，不发 82）。
 *     写后**没有固定延时**（2026-10-09 去掉）：沿一到就读，7100 的抬线本身就是「回执就绪」。
 *
 *   ⚠ 为什么路径 B 也必须读 + 发 82：82 = 「这一帧我读完了，缓冲区还你」。不读就是不还
 *     credit，7100 会把那帧一直压着（以前是 sniff 替它还的，sniff 一关就轮到后面第一个
 *     要读的踩雷 —— 这曾表现为「配置族命令紧跟交互族命令时读不到 46」）。见 §26。
 *   ⚠ 路径 A 原先「固定 50ms 盲读」的依据（「A7 写帧产生零边沿」，2026-10-08 见 §24.1）
 *     **已被推翻**：2026-10-08 测听 ③ 与 2026-10-09 两轮上板都看到 A7 写帧 +47~58ms 抬线
 *     （§26.9 / §26.11），那 50ms 只是与沿碰巧同相。此处即 2026-10-09 那次统一改动。
 */
typedef struct
{
    const uint8_t *data;     /* 写帧字节；NULL = 本条只读 */
    uint16_t       wlen;     /* 写帧长度；0 = 不写 */
} a7_step_t;

static a7_step_t s_steps[A7_MAX_CMDS];
static uint8_t   s_step_cnt;
static uint8_t   s_step_idx;
static bool      s_sess_active;

/* ---- 推进状态（照 dsp_7100_init.c 的 s_rb_st / s_rb_wait_rise / s_rb_timeout）----
 * 相位与门控：
 *   CMD_ST_SEND      发命令 —— 有前序 82 时等它的**下降沿**（7100 吃下了才发下一条）
 *   CMD_ST_WAIT_RISE 发完等 7100 抬线的**上升沿**（回执到了才去读）—— **两族都走**。
 *                    200ms tick 兜底时**还要求线高**；线低就继续等（最多
 *                    DSP7100_WAIT_RISE_MAX_TICKS 拍），到点后路径 B 判失败、路径 A 盲读一次。
 *   CMD_ST_READ      读头 3B + 按 len16 补读（两族同）；上次读失败（s_cmd_retry_lock）
 *                    只等 tick 重读 —— 只有路径 A 会置它（它才校验 46，才有失败可言）
 * 200ms tick 只置 s_cmd_timeout 兜底，真正步进在主循环的 dsp_7100_cmd_poll()。
 *
 * ⚠ 等上升沿**必须在本步发 82 之前** —— 82 自己也可能带沿（§23.6），
 *   混在一起就分不清是 7100 抬的还是我们自己发出来的。 */
#define CMD_ST_SEND       0
#define CMD_ST_WAIT_RISE  1
#define CMD_ST_READ       2

static uint8_t  s_cmd_st;
static uint8_t  s_cmd_send_gate;    /* 1 = SEND 前等上一条 82 的下降沿（**两条路径都置**） */
static uint8_t  s_cmd_retry_lock;  /* 1 = 上次读没拿到 46：本相位不再认边沿，只等 tick 重读 */
static uint8_t  s_cmd_retry_cnt;   /* 本步已重读次数；达 A7_ACK_RETRY_MAX 判本步失败 */
static uint8_t  s_cmd_timeout;      /* 200ms tick 兜底标志 */
static uint8_t  s_cmd_low_ticks;    /* WAIT_RISE 里吞掉的「tick 到了但线还是低」拍数 */
static uint8_t  s_cmd_rescue_cnt;   /* 本步已做的兜底读次数（DSP7100_RESCUE_MAX 封顶） */
static uint32_t s_cmd_wait_fall;    /* 发本条 82 前记的下降沿数 */
static uint32_t s_cmd_wait_rise;    /* 写完本条后记的上升沿数（**两族的进门基准**） */

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

/* 会话族：决定「**认不认**读到的内容」和「等不到沿怎么收场」（2026-10-08 晚，见 §26）。
 * 由 a7_build_session() 按 kind 置位，dsp_7100_cmd_step() 据此选执行函数。
 *   配置族（降噪 / DFBC / EQ / WDRC）  → A7_FAM_ACK ：校验首字节 46，不是就补发 82 重读；
 *                                        等不到沿则盲读一次降级
 *   交互族（切程序 / 调音量 / 测听 / 纯音 / 静音）→ A7_FAM_EDGE：只当门，读到什么照发 82；
 *                                        等不到沿判本步失败
 *
 * ⚠ 两族**都读、都发 82**（收尾的 82 不再分族，见 a7_step_advance）、等沿/等下降沿的时序
 *   完全一致（2026-10-09 统一）—— 差别只剩上面那两条。§23 那版想从 rlen / wait_rise 推语义，
 *   被实测推翻了，所以显式记族。
 * ⚠ 配置族原来的「固定 50ms 盲读」依据「A7 写帧零边沿」**已被推翻**（实测 +47~58ms 抬线，
 *   §26.9 / §26.11），那 50ms 只是与沿碰巧同相。 */
#define A7_FAM_EDGE      0
#define A7_FAM_ACK       1
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
    s_step_cnt++;
    return p;
}

/* A2 写帧 `A2 00 <reg> <val>`。只有路径 B 用得到本函数 —— 读完由 7100 的回执沿定
 * （a7_step_edge），写完不等任何固定时间。 */
static void a7_add_a2(uint8_t reg, uint8_t val)
{
    uint8_t *p = a7_put(4);

    if (p == NULL) return;
    p[0] = DSP7100_CMD_PARAM_WRITE; p[1] = 0x00; p[2] = reg; p[3] = val;
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
    uint8_t *p;

    /* ⚠ 顺序不能反：a7_put 按**调用顺序**把帧排进队列，must 先 prep（选地址）
     *   再写值。写反了每个值都会落到**上一条 prep 选的地址**上，7100 照样 ack
     *   （会话报 ok=1，日志看不出问题）—— 2026-10-09 上板实测的就是这个：
     *   EQ +5 后读回，P1 两条通道的 LL/HL 整体错位一格。 */
    a7_add_prep(A7_BLK_WDRC, prog, (uint8_t)(addr >> 8), (uint8_t)(addr & 0xFF));
    p = a7_put(9);
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
 * ⚠ 2026-09-30 一度砍掉那次读、2026-10-08 白天连 82 也去掉；**当天晚上都退回来了**
 *   （交互族整族改成走完整握手，见 a7_step_edge 与开发文档 §26）—— 现在与抓包同形。
 *   仍**不校验内容**（只当门用），只是把 7100 的那帧读走、把 credit 还回去。
 *   ⚠ 本条是 A7 帧，曾担心「§24.1 说 A7 写帧零边沿 ⇒ 等不到沿、1s 后判本步失败」——
 *     2026-10-09 上板**已排除**（见 §26.11）：纯音帧写后 ~10ms 抬线并回 `46 00 00`，
 *     与 A2 帧一样走「写 → 等抬线 → 读 → 82」，两轮都没出现「等抬线超时」。
 *     （规则仍是：读之前要求线高，线一直低则 5 拍 = 1s 判本步失败、会话报 ok=0。） */
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
    /* 会话族 → 走哪条路径（见文件头与开发文档 §24 / §26）。
     * 两族都读、都发 82、都等 7100 抬线再读（2026-10-09 统一）；差别是配置族（路径 A）
     * **校验 46**（失败补 82 重读）、等不到沿时盲读一次降级，交互族（路径 B）读到的内容只进日志。 */
    s_sess_family = (kind == A7_KIND_DENOISE || kind == A7_KIND_DFBC ||
                     kind == A7_KIND_WDRC    || kind == A7_KIND_EQ)
                    ? A7_FAM_ACK : A7_FAM_EDGE;

    if (kind == A7_KIND_MUTE) {             /* 静音 / 解除静音：同样只发这一条 */
        a7_add_simple(val ? A7_OPT_MUTE : A7_OPT_UNMUTE);
        return !s_build_fail;
    }

    if (kind == A7_KIND_TONE) {             /* 纯音：只发这一条（路径 B：写帧 + 读回执 + 82） */
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

    if (kind == A7_KIND_VOL) {              /* 调音量：A2 写帧 + 读回执 + 82（路径 B） */
        a7_add_a2(DSP7100_REG_VOLUME, s_volume_value[val]);
        return !s_build_fail;
    }

    /* 切程序：一条 A2 写帧 + 读回执 + 82（路径 B） */
    if (kind == A7_KIND_PROG) {
        a7_add_a2(DSP7100_REG_PROGRAM, prog);
        return !s_build_fail;
    }

    /* 测听进入 / 退出：**三条命令编在一个会话里**，照 tonestar / tonestop 抓包的顺序：
     *   ① 切程序   `A2 00 16 <prog>`
     *        enter → DSP7100_AUDIOMETRY_PROG(3)，由 dsp_7100_audiometry() 算好传进来
     *        exit  → 进测听前那个程序
     *   ② 测听位   `A2 00 2E <00|58>`（00 = 进，58 = 退）
     *   ③ 解除静音 `A7 01 00 00 00 26`
     * 三句都走完整握手：写 → 等 7100 抬线 → 读回执 → 82 → 等下降沿 → 下一条
     * （2026-09-30 砍掉的那次读，2026-10-08 晚又回来了 —— 见 a7_step_edge 与 §26）。
     * ⚠ ① 的程序加载窗口（原先写死 80ms）已由 7100 自己的回执沿顶替 —— 见 a7_step_edge。 */
    if (kind == A7_KIND_AUDIO_ENTER || kind == A7_KIND_AUDIO_EXIT) {
        a7_add_a2(DSP7100_REG_PROGRAM, prog);
        a7_add_a2(DSP7100_REG_TONE_MODE,
                  (kind == A7_KIND_AUDIO_ENTER) ? DSP7100_TONE_MODE_ON
                                                : DSP7100_TONE_MODE_OFF);
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

/* ---- 兜底读一帧（2026-10-09）----
 * 两个调用点共用：SEND 门（线高就先去读，别发下一条）与会话收尾（收尾前读掉最后压着的帧）。
 * 触发条件由调用方判：**上一条 82 的下降沿到了、线却还是高的** —— 说明这条 82 之后 7100
 * 又抬了线（日志 `rise +1/47 fall +1/46`），它压着一帧。
 *
 * 读法与路径 B 完全相同（读头 3B → 按 len16 补读 payload → 一行打印）、**同样不校验内容**；
 * 读完发 82 还 credit（`82` = 「这一帧我读完了，缓冲区还你」），顺手做一次通知解码上报
 * （BLE 未连接时 rempro_push_7100_notify 内部跳过）。
 *
 * ⚠ 没读到（头那 3B 传输层就失败）**不发 82**：没读到就不欠 credit —— 与路径 B 的
 *   READ 分支一致；也不判会话失败（这是旁路动作，调用方自己决定怎么继续）。
 * ⚠ 调用方负责封顶（DSP7100_RESCUE_MAX / 收尾只做一次），本函数不管循环。 */
static void a7_rescue_read(void)
{
    uint8_t  rx[A7_RX_MAX];
    uint16_t pay = 0;
    bool     okh;
    bool     okp = true;

    /* 编号直接用 s_cmd_rescue_cnt：两个调用点都在进来**之前**已经 ++ 过，再加 1 会全体偏 1
     * （2026-10-09 11:05 日志里第一次兜底读打成 `#2` 就是这个 bug）。 */
    PRINTF("[7100] step%u/%u 线高（7100 压着一帧）→ 兜底读 #%u\r\n",
           s_step_idx, s_step_cnt, (unsigned)s_cmd_rescue_cnt);

    okh = i2c_7100_read(I2C_7100_ADDR, rx, 3);      /* 头 3B：status + len16(小端) */
    if (!okh) {
        PRINTF("[7100] step%u/%u 兜底读失败（头）—— 不发 82（没读到就不欠 credit）\r\n",
               s_step_idx, s_step_cnt);
        return;
    }
    pay = (uint16_t)rx[1] | (uint16_t)((uint16_t)rx[2] << 8);
    if (pay > (A7_RX_MAX - 3)) pay = A7_RX_MAX - 3;
    if (pay > 0) {
        okp = i2c_7100_read(I2C_7100_ADDR, rx + 3, (uint8_t)pay);
    }
    print_r2(rx, 3, okh, rx + 3, (uint16_t)pay, okp);   /* 两截合成一行，同路径 B */

    if (okp && pay > 0) {
        rempro_push_7100_notify(rx + 3, (uint8_t)((pay > 3u) ? 3u : pay));
    }

    /* 先记沿再发（同 a7_step_advance）：这条 82 的下降沿还没到，等它到了门就能往下走 */
    s_cmd_wait_fall = dsp_7100_dio13_fall_cnt();
    (void)dsp_7100_send_end();
}

/* 会话收尾：成功后同步 RAM 参数并请求落盘（否则下次开机缓存显示旧值） */
static void a7_session_finish(bool ok)
{
    s_sess_active = false;

    /* 收尾兜底（2026-10-09，开发者定）：跑完时若线还是高的，说明最后那条 82 之后 7100
     * 又抬了线 —— 它压着一帧。**收尾前先读掉**，否则下面那句 rebase 会把那记上升追平抹掉，
     * 那一帧就永远没人读（实测 2026-10-09 10:30：`session done ok=1 D13=1`，那帧没人读）。
     *
     * **要读到线落回低或到上限为止，不能只兜一次**：10:51 那轮第一次兜底读读到的是真帧
     * （`43 03 00 | 00 2A 44`），而那条 82 之后线又被抬起来（`rise +1 / fall +1`）——
     * 那不是回声，是 7100 **手上还有下一帧**（11:05 定案，见开发文档 §26.6）。只兜一次，
     * 后面那一帧照样留在 7100 里。
     *
     * 只在成功收尾时做：失败路径 I2C 已经出过问题，不再插读。看族别不限 —— 路径 A
     * 的会话同样会丢帧。
     * ⚠ 这里是**同步循环**（一次主循环 pass 内跑完）。实测切程序后是 2 帧（0x2A/0x2E），
     *   约 30ms；上限 10 是防 7100 一直有帧那种异常。若嫌长，可以让会话多留一拍、
     *   每次 pass 兜一次（要加状态，先别做）。 */
    if (ok && DIO_DATA->ALIAS[13] == 1) {
        s_cmd_rescue_cnt = 0;       /* 收尾这一轮单独计数，上限同 DSP7100_RESCUE_MAX（每步的上限） */
        while (DIO_DATA->ALIAS[13] == 1 && s_cmd_rescue_cnt < DSP7100_RESCUE_MAX) {
            s_cmd_rescue_cnt++;
            a7_rescue_read();
        }
        if (DIO_DATA->ALIAS[13] == 1) {
            PRINTF("[7100] 收尾兜底读已满 %u 次线仍高 → 到此为止（随后 rebase，归 sniff）\r\n",
                   (unsigned)DSP7100_RESCUE_MAX);
        }
    }

    /* 收尾瞬间的线电平 + 沿计数（2026-10-09 加，排查「发完 82 线又被抬起来却没人读」）：
     * 命令引擎只在「本步发过写帧」的答复窗口里读 —— 会话一收尾表就空了，之后抬的线
     * 归 sniff 管。而下面那句 rebase 会把触发基准追平到**当前值**，会话期抬的沿全被抹掉。
     * ⚠ 本行打在**上面那句兜底读之后**：D13 停在 1 = 兜底读自己那条 82 之后线又被抬起来
     *   （要么还有一帧、要么就是 82 的回声）；落回 0 = 收干净了，rebase 也是对的。
     *   （2026-10-09 之前没有兜底读，那时 D13=1 直接意味着「压着一帧、被 rebase 丢了」。）
     * rise/fall 一起打，便于按差值反推末态（升 > 降 = 高）。 */
    PRINTF("[7100] --- session done ok=%u D13=%u rise=%lu fall=%lu ---\r\n",
           ok, (unsigned)DIO_DATA->ALIAS[13],
           (unsigned long)dsp_7100_dio13_rise_cnt(),
           (unsigned long)dsp_7100_dio13_fall_cnt());

    /* 会话期 dsp_7100_sniff_poll() 被 cmd_busy 挡着不跑，可会话自己的读与 82 已经把
     * DIO13 沿计数推高了 —— 不追平 sniff 的触发基准，它恢复的第一拍就会凭空读一帧
     * （HDR 00 00 00）+ 白发一条 04 82。放这里而不是 app.c：会话只有这一个出口。 */
#if (DSP7100_DIO13_SNIFF_ENABLE)
    dsp_7100_sniff_rebase();
#endif

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

/* 本步收尾 → 交给下一条。
 * **两条路径都在这里发 82**（2026-10-08 晚改回统一）：82 = 「这一帧我读完了，缓冲区还你」，
 * 两族现在都要读满、都要还，所以不再分族。
 * 返回 true = 会话还有下一条；false = 本步已终结（I2C 失败 / 整会话跑完），
 * 两种情况都已经调过 a7_session_finish()，调用方直接 return 即可。 */
static bool a7_step_advance(void)
{
    /* 先记沿再发：这条 82 的下降沿此刻还没到，等它到了就能立刻发下一条。 */
    s_cmd_wait_fall = dsp_7100_dio13_fall_cnt();
    if (!dsp_7100_send_end()) { a7_session_finish(false); return false; }
    s_cmd_send_gate = 1;       /* 下一条要等这条 82 的下降沿 */

    s_cmd_st         = CMD_ST_SEND;
    s_cmd_retry_lock = 0;
    s_cmd_retry_cnt  = 0;
    s_cmd_timeout    = 0;
    s_step_idx++;
    if (s_step_idx >= s_step_cnt) { a7_session_finish(true); return false; }
    return true;
}

/* ---- 读一帧回执（**命令会话与开机读回共用**，声明见 dsp_7100_cmd.h）----
 * 头 3B = status + len16(小端)，再按 len16 补读 payload（上限 cap − 3）。
 * 只读不打印：命令族要**合成一行**（下面的 a7_read_frame），读回要分块 dump（375B 一行
 * 会冲爆 pack printf 的 200B 静态缓冲）—— 打印形状是调用方的事。 */
uint8_t dsp_7100_read_frame(uint8_t *rx, uint16_t cap, uint16_t *pay_out)
{
    uint16_t room = (cap > 3u) ? (uint16_t)(cap - 3u) : 0u;
    uint16_t pay  = 0;
    bool     okh, okp = true;

    okh = i2c_7100_read(I2C_7100_ADDR, rx, 3);
    if (okh) {
        pay = (uint16_t)rx[1] | (uint16_t)((uint16_t)rx[2] << 8);
        /* setter `46 00 00` → len16 = 0（零数据字节）；0x03 探测 `46 03 00 <x> <busy> 1A`
         * → len16 = 3；`43 03 00` 那类也是 3（开发文档 §25.2 的旧固定长度即由此而来）。
         * 读回那三条是 375 / 306 / 174B（read 命令里的 16 位块地址就是字节数）。 */
        if (pay > room) pay = room;
        if (pay > 0) okp = i2c_7100_read(I2C_7100_ADDR, rx + 3, pay);
    }

    *pay_out = pay;
    if (!okh) return DSP7100_RDF_ERR_HDR;
    if (!okp) return DSP7100_RDF_ERR_PAY;
    return DSP7100_RDF_OK;
}

/* 命令族这一侧的薄封装：传本族的读缓冲上限 A7_RX_MAX，并把**两截合成一行**打。
 * 为什么必须合一行：串口工具按自己的轮询周期收行，分两行打会凭空多出一个时间戳 ——
 * 实测同一段代码给出过「同一毫秒」和「差 12ms」两种结果，拿去量协议时序会看错；
 * 而且每行 @115200 ≈2.5ms 的传输时间也会把后面的 82 推后。
 * 头里的 len16 为 0（pay == 0）时退化成原来的单行头，日志形状与改造前一致。
 * ⚠ 读回不套这层（它的 payload 有 375B，合一行要 1150 字符 > 200B 缓冲）。 */
static uint8_t a7_read_frame(uint8_t *rx, uint16_t *pay_out)
{
    uint8_t rd = dsp_7100_read_frame(rx, A7_RX_MAX, pay_out);

    print_r2(rx, 3, rd != DSP7100_RDF_ERR_HDR, rx + 3, *pay_out,
             rd != DSP7100_RDF_ERR_PAY);
    return rd;
}

/* ---- 路径 A：配置族（降噪 / DFBC / EQ / WDRC）—— 写帧 → 等抬线 → 读 → 校验 46 → 82 ----
 * 与路径 B（a7_step_edge）**只差两处**：读完要校验首字节是 46（不是就补发 82 重读，≤3 次），
 * 以及等不到抬线时盲读一次降级（见 dsp_7100_cmd_poll）。
 * ⚠ 2026-10-09 之前这里是「写帧 → 阻塞等 A7_ACK_WAIT_MS(50) → 按步读固定长度」；依据
 *   「A7 写帧没有 DIO13 回铃」（§24.1）**已被推翻**（实测 +47~58ms 抬线，§26.9 / §26.11），
 *   那 50ms 只是与沿碰巧同相，还让每步白背一次不喂狗的忙等（WDRC n=16 累计 2.3s）。
 * ⚠ 读失败时**不重发写帧**，只重读 —— 与读回一致（读回失败后停在 RB_READ 重读）。 */
static void a7_step_read(void)
{
    const a7_step_t *s = &s_steps[s_step_idx];
    uint8_t  rx[A7_RX_MAX] = {0};   /* 读失败时别把栈上的垃圾当数据打出来 */
    uint16_t pay;
    uint8_t  rd;
    bool     ok;

    if (s_cmd_st == CMD_ST_SEND) {
        /* 基准必须在**写帧之前**记死（同 a7_step_edge）：回执沿可能就在写完之后几毫秒里。
         * 此刻上一条 82 的下降沿已经在 SEND 门那里等到了，窗口里的沿只能是本步 7100 的。 */
        s_cmd_wait_rise  = dsp_7100_dio13_rise_cnt();
        s_cmd_low_ticks  = 0;    /* 本步等待计数清零（tick 兜底连等多少拍，见 cmd_poll） */
        s_cmd_rescue_cnt = 0;    /* 本步兜底读次数清零（DSP7100_RESCUE_MAX 是**每步**的上限） */
        if (s->wlen > 0) {
            ok = i2c_7100_write(I2C_7100_ADDR, s->data, s->wlen);
            print_w(s->data, s->wlen, ok);
            if (!ok) { a7_session_finish(false); return; }
        }
        s_cmd_st = CMD_ST_WAIT_RISE;
        return;
    }

    if (s_cmd_st == CMD_ST_WAIT_RISE) {     /* 走到这里 = 沿到了，或 tick 兜底（盲读降级） */
        s_cmd_st = CMD_ST_READ;
        return;
    }

    /* 读相位。重读走 s_cmd_retry_lock 分支：那时不等沿、只等 tick，读到的是我们补发 82
     * 之后 7100 重新放出来的同一条。 */
    rd = a7_read_frame(rx, &pay);
    if (rd == DSP7100_RDF_OK && rx[0] == DSP7100_RSP_OK) {    /* 46 = 7100 接下了这条 */
        (void)a7_step_advance();
        return;
    }

    /* 没拿到 46（或压根没读成）：本步补发 82 收尾（照读回 —— 失败那步的 82 也照发），
     * 再等 tick 重读 */
    PRINTF("[7100] step%u/%u ACK FAIL hdr=%02X %02X %02X pay=%u (retry %u/%u)%s\r\n",
           s_step_idx, s_step_cnt, rx[0], rx[1], rx[2], (unsigned)pay,
           s_cmd_retry_cnt, A7_ACK_RETRY_MAX,
           (rd == DSP7100_RDF_ERR_HDR) ? " [读头失败]" :
           (rd == DSP7100_RDF_ERR_PAY) ? " [读payload失败]" : "");
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

/* ---- 路径 B：交互族（切程序 / 调音量 / 测听 / 纯音 / 静音）----
 * 2026-10-08 晚起**也走完整握手**（此前只写、不读、不发 82）：
 *   写帧 → 等 7100 抬线的**上升沿** → 读头 3B + 按 len16 补读
 *   → 82 → 等下降沿 → 下一条
 * 2026-10-09：写后那个 wait_ms 固定延时去掉了 —— 读时机**纯由沿定**（见 a7_step_edge）。
 *
 * ⚠ 为什么必须读：82 = 「这一帧我读完了，缓冲区还你」。不读就是不还 credit，7100 会把
 *   那帧一直压着 —— 此前是 sniff 替它还（日志里 #52/#53/#54 就是 sniff 在替交互族读），
 *   sniff 一关就轮到后面第一个要读的踩雷。见开发文档 §26。
 * ⚠ **只当门用、不校验内容**：读到什么（`43 03 00 …` / `46 00 00`）都照发 82 放行。
 *   沿等不到时 200ms tick 兜底**但仍要求线高**（2026-10-09 起）：线低说明 7100 还没把回执
 *   放出来，那就继续等，连等 DSP7100_WAIT_RISE_MAX_TICKS 拍才判本步失败 —— **不盲读**。
 * 代价仍在：命令**不被校验**，日志里能看出的只有「读到了/没读到」。 */
static void a7_step_edge(void)
{
    const a7_step_t *s = &s_steps[s_step_idx];
    uint8_t  rx[A7_RX_MAX] = {0};   /* 读失败时别把栈上的垃圾当数据打出来 */
    uint16_t pay;
    uint8_t  rd;
    bool     ok;

    if (s_cmd_st == CMD_ST_SEND) {
        /* 写完**不等任何固定时间**，直接去等 7100 抬线 —— 那个沿就是「回执已就绪」，
         * 7100 说好了才读。（2026-10-09 去掉此前的 wait_ms：它只会把关早了。
         * 实测 0x2E 那条写后 ≤1ms 就抬线，而测听第 ① 句要等 80ms —— 有 delay 时
         * 沿落在窗口里、延迟一满才读，等于白等；沿若比 delay 慢，delay 又只会把它挡在
         * 窗外，只能等下一个沿（那要等下一条命令）→ 退化成 200ms tick（实测白等 178ms）。）
         *
         * 基准必须在**写帧之前**记死：这么快的回铃，写完之后再记就漏了。
         * 此刻上一条 82 的下降沿已经在 SEND 门那里等到了，窗口里的沿只能是本步 7100 的。 */
        s_cmd_wait_rise = dsp_7100_dio13_rise_cnt();
        s_cmd_low_ticks = 0;    /* 本步等待计数清零（tick 兜底连等多少拍，见 cmd_poll） */
        s_cmd_rescue_cnt = 0;   /* 本步兜底读次数清零（DSP7100_RESCUE_MAX 是**每步**的上限） */
        if (s->wlen > 0) {
            ok = i2c_7100_write(I2C_7100_ADDR, s->data, s->wlen);
            print_w(s->data, s->wlen, ok);
            if (!ok) { a7_session_finish(false); return; }
        }
        s_cmd_st = CMD_ST_WAIT_RISE;
        return;
    }

    if (s_cmd_st == CMD_ST_WAIT_RISE) {     /* 走到这里 = 沿到了，或 tick 兜底放行 */
        s_cmd_st = CMD_ST_READ;
        return;
    }

    rd = a7_read_frame(rx, &pay);                   /* 头 3B + 按 len16 补读，打印合成一行 */

    if (rd != DSP7100_RDF_OK) {                           /* 传输层失败 → 显式判失败，不装看不见 */
        if (rd == DSP7100_RDF_ERR_HDR) {
            PRINTF("[7100] step%u/%u 读回执失败（头）\r\n", s_step_idx, s_step_cnt);
        } else {
            PRINTF("[7100] step%u/%u 读回执失败（payload %uB）\r\n",
                   s_step_idx, s_step_cnt, (unsigned)pay);
        }
        a7_session_finish(false);
        return;
    }

    if (pay > 0) {
        /* 会话期 7100 本地按键改档位也走这条通道 —— 顺手解一次，别丢（BLE 未连接内部跳过） */
        rempro_push_7100_notify(rx + 3, (uint8_t)((pay > 3u) ? 3u : pay));
    } else {
        PRINTF("[7100] step%u/%u 回执 pay=0（%02X %02X %02X）\r\n",
               s_step_idx, s_step_cnt, rx[0], rx[1], rx[2]);
    }

    (void)a7_step_advance();
}

/* 推进一步相位（只由主循环 dsp_7100_cmd_poll 调）：按会话族选路径 */
static void dsp_7100_cmd_step(void)
{
    if (s_sess_family == A7_FAM_ACK) a7_step_read();
    else                             a7_step_edge();
}

/* 200ms tick（app_process.c）调：只置超时标志，**不推进** —— 推进统一由
 * dsp_7100_cmd_poll 做（与读回 dsp_7100_rb_tick 的分工完全相同）。 */
void dsp_7100_cmd_tick(void)
{
    s_cmd_timeout = 1;
}

/* 主循环（app.c）：命令会话**唯一**的推进点。
 *   SEND      等上一条 82 的**下降沿**（7100 吃下了才发下一条）—— 实测 0~2ms，两族共用。
 *             **再加一条**：还要 DIO13 是低的。线高 = 那条 82 之后 7100 又抬了线，
 *             它压着一帧 → 不发起下一条，先兜底读掉（a7_rescue_read，每步上限 DSP7100_RESCUE_MAX）
 *   WAIT_RISE 等 7100 抬线的**上升沿**（回执到了才去读）—— **两族共用**（2026-10-09 起）。
 *             200ms tick 只用来兜底，且**必须线高才放行**（2026-10-09，见下）；
 *             连等 DSP7100_WAIT_RISE_MAX_TICKS 拍仍线低时：路径 B 判本步失败（不盲读），
 *             路径 A 盲读一次降级（它认 46，读到空帧会走「补 82 + 重读」而不是当成功）。
 *   READ      路径 A 读失败重试时只等 tick（不认边沿，同读回 s_rb_retry）；
 *             其余情况直接进。
 *
 * ⚠ **不再无条件盲读**（2026-10-09 上板实测）：以前 tick 到点路径 B 就无条件读，读到的是
 *   `00 00 00`（7100 还没把回执放出来），却照样 `82` 出去 —— 之后真正的回执才抬线，可会话
 *   已经收尾，没人读它 → 线停在高、那帧滞留在 7100（日志 `session done ok=1 D13=1`）。
 *   加了电平门后读永远发生在 7100 抬线之后，也就不会再有「读空还报 ok=1」（路径 A 那次
 *   降级盲读例外，但它有 46 校验兜着）。 */
void dsp_7100_cmd_poll(void)
{
    if (!s_sess_active) { s_cmd_timeout = 0; return; }
    /* 步骤表空了（构建失败时不会置 active，这里是兜底）：别拿越界下标去发 I2C */
    if (s_step_idx >= s_step_cnt) { a7_session_finish(true); return; }

    if (s_cmd_st == CMD_ST_SEND) {
        if (s_cmd_send_gate && !s_cmd_timeout &&
            dsp_7100_dio13_fall_cnt() == s_cmd_wait_fall) return;
        /* 门开（上一条 82 的下降沿到了，或 tick 兜底）——**还要线是低的**才发下一条。
         * 线高 = 那条 82 之后 7100 又抬了线，它压着一帧（2026-10-09 日志 `rise +1/47
         * fall +1/46` 那次就是）：不该继续往下发，先把那帧读掉。
         * s_cmd_send_gate 只在本步**有前序 82** 时为 1 —— 会话第一条命令前面没有 82，
         * 不套这个门（否则会话开头就会先白读一轮）。 */
        if (s_cmd_send_gate && DIO_DATA->ALIAS[13] == 1) {
            if (s_cmd_rescue_cnt < DSP7100_RESCUE_MAX) {
                s_cmd_rescue_cnt++;
                s_cmd_timeout = 0;          /* 这一拍已经做了事（兜底读）——tick 标志清掉，
                                             * 免得后面几拍都不等沿、10 次一口气烧完 */
                a7_rescue_read();
                return;                     /* 回到本相位：等这条 82 的下降沿 + 线低 */
            }
            /* 到上限还是高（正常见不到；线高=有帧，帧抽干会落回低）：不再兜，照常发下一条 */
            PRINTF("[7100] step%u/%u 兜底读已满 %u 次线仍高 → 照发下一条\r\n",
                   s_step_idx, s_step_cnt, (unsigned)DSP7100_RESCUE_MAX);
        }
    } else if (s_cmd_st == CMD_ST_WAIT_RISE) {
        if (!s_cmd_timeout &&
            dsp_7100_dio13_rise_cnt() == s_cmd_wait_rise) {
            return;                                     /* 还没抬线，继续等 */
        }
        /* 到这里 = 沿到了，或 tick 兜底放行。两种来源**都要求线是高的**才读：
         *   沿放行但线低 —— 抬过又落回，那不是本步要读的帧（比如上一条 82 自己的回声沿）；
         *   tick 放行但线低 —— 7100 还没把回执放出来，**不读**（实测盲读读到 00 00 00，
         *                       还照发了 82；真正的回执稍后才抬线，没人读 → 滞留）。
         * 吞掉这一拍继续等，连等 DSP7100_WAIT_RISE_MAX_TICKS 拍仍线低的收场见下。 */
        if (DIO_DATA->ALIAS[13] == 0) {
            if (!s_cmd_timeout) return;                 /* 沿放了又落回：等下一个 */
            if (++s_cmd_low_ticks >= DSP7100_WAIT_RISE_MAX_TICKS) {
                if (s_sess_family != A7_FAM_ACK) {
                    /* 路径 B：没读到就不欠 credit → 不发 82，直接判本步失败 */
                    PRINTF("[7100] step%u/%u 等抬线超时（%u 拍线仍低，判本步失败）\r\n",
                           s_step_idx, s_step_cnt, (unsigned)s_cmd_low_ticks);
                    a7_session_finish(false);
                    return;
                }
                /* 路径 A 降级：等不到沿也读一次。它有 46 校验兜底 —— 读到空帧只是走
                 * 「补发 82 + 重读」，不会像路径 B 那样把空帧当成功，所以这里敢读。
                 * 不 return：落到下面的 s_cmd_timeout = 0 + cmd_step() 就进读相位，
                 * 主循环转得很快，不会为此再等一拍 200ms。 */
                PRINTF("[7100] step%u/%u 等抬线超时（%u 拍线仍低）→ 配置族盲读一次\r\n",
                       s_step_idx, s_step_cnt, (unsigned)s_cmd_low_ticks);
            } else {
                s_cmd_timeout = 0;                      /* 吞掉这一拍，等下一拍/下一个沿 */
                return;
            }
        }
        s_cmd_low_ticks = 0;
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
    s_cmd_rescue_cnt = 0;
    PRINTF("[7100] --- session start: %s prog=%u val=%u db=%d cmds=%u ---\r\n",
           a7_kind_name(kind), prog, val, s_sess_db, s_step_cnt);
    return true;
}

/* 切程序（异步）。序列：`A2 00 16 <prog>` → 读回执 → `82`。见 §26。 */
bool dsp_7100_switch_program(uint8_t prog)
{
    if (prog < 1 || prog > 4) return false;
    PRINTF("[7100] --- SwitchProgram prog=%u ---\r\n", prog);
    s_sess_db = 0;
    return a7_session_start(A7_KIND_PROG, prog, 0);
}

/* 调音量（异步）。序列：`A2 00 12 <0x00…0x64>` → 读回执 → `82`。见 §26。 */
bool dsp_7100_set_volume(uint8_t level)
{
    if (level > 6) return false;   /* 档位 0-6；uint8_t 不用判负 */
    PRINTF("[7100] --- SetVolume level=%u ---\r\n", level);
    s_sess_db = 0;
    return a7_session_start(A7_KIND_VOL, dsp_7100_get_program(), level);
}

/* 测听进入 / 退出（异步）。三条命令一套会话，见 a7_build_session 的 AUDIO 分支：
 *   enter: 切到 DSP7100_AUDIOMETRY_PROG(3) → 0x2E=00 → 解除静音
 *   exit : 切回 prev_prog（进测听前那个程序）→ 0x2E=58 → 解除静音
 * 三句都走完整握手（写 → 等抬线 → 读回执 → 82 → 等下降沿），80ms / 2ms 是最短间隔。 */
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
