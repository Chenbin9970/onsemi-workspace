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

#define A7_MAX_CMDS      8
#define A7_CMD_BUF_SZ    400    /* 最大单命令 300B（降噪块） */
#define A7_RX_ACK        3      /* 应答固定 3B（46 00 00） */

#define A7_OPT_MUTE      0x25
#define A7_OPT_UNMUTE    0x26
#define A7_OPT_COMMIT    0x0C
#define A7_OPT_SELECT    0x12
#define A7_OPT_CONFIRM   0x10
#define A7_BLK_DENOISE   0x09
#define A7_BLK_DFBC      0x0A

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
static uint8_t   s_sess_kind;       /* 0=降噪 1=DFBC */
static uint8_t   s_sess_val;        /* 降噪档位 / DFBC 开关 */

/* 从命令缓冲切一段并挂到步骤表 */
static uint8_t *a7_put(uint16_t len)
{
    uint8_t *p;

    if (s_step_cnt >= A7_MAX_CMDS) return NULL;
    if ((uint32_t)s_cmd_used + len > A7_CMD_BUF_SZ) return NULL;

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

/* 会话骨架：静音 → 选程序 → [写块准备+写块] → confirm → 解除静音 →
 * 选回程序0 → commit。写块由 kind 决定。 */
static bool a7_build_session(uint8_t kind, uint8_t prog, uint8_t val)
{
    s_step_cnt = 0;
    s_step_idx = 0;
    s_step_read = false;
    s_cmd_used = 0;

    a7_add_simple(A7_OPT_MUTE);
    a7_add_prog(A7_OPT_SELECT, prog);

    if (kind == 0) {                      /* 降噪：块 09 + A7 27(300B) */
        a7_add_prep(A7_BLK_DENOISE, prog, 0x00, 0x01);
        if (a7_add_noise_block(val) == NULL) return false;
    } else {                              /* DFBC：块 0A + A7 04(08 00 00 X) */
        uint8_t *p;
        a7_add_prep(A7_BLK_DFBC, prog, 0x00, 0x00);
        p = a7_put(9);
        if (p == NULL) return false;
        p[0] = 0xA7; p[1] = 0x04; p[2] = 0x00; p[3] = 0x00; p[4] = 0x00;
        p[5] = 0x08; p[6] = 0x00; p[7] = 0x00; p[8] = val ? 0x01 : 0x00;
    }

    a7_add_prog(A7_OPT_CONFIRM, prog);
    a7_add_simple(A7_OPT_UNMUTE);
    a7_add_prog(A7_OPT_SELECT, 1);        /* 指针复位回程序0（照抓包恒为 01） */
    a7_add_simple(A7_OPT_COMMIT);

    return (s_step_cnt == A7_MAX_CMDS);   /* 步骤齐了才算构建成功 */
}

/* 会话收尾：成功后同步 RAM 参数并请求落盘（否则下次开机缓存显示旧值） */
static void a7_session_finish(bool ok)
{
    s_sess_active = false;
    PRINTF("[7100] --- session done ok=%u ---\r\n", ok);

    if (!ok) return;

    if (s_sess_kind == 0) {
        dsp_7100_prog_t *p = &dsp_7100_rb_bufs()->prog[s_sess_prog - 1];
        p->denoise_en  = 1;    /* 设档位即为开启（读回各档使能位均为 1） */
        p->denoise_lvl = s_sess_val;
        dsp_7100_cache_save_request();
    } else {
        dsp_7100_prog_t *p = &dsp_7100_rb_bufs()->prog[s_sess_prog - 1];
        p->dfbc_en = s_sess_val ? 1 : 0;
        dsp_7100_cache_save_request();
    }
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

/* 启动会话；返回 true = 已受理（异步，完成情况看日志） */
static bool a7_session_start(uint8_t kind, uint8_t prog, uint8_t val)
{
    if (s_sess_active) {
        PRINTF("[7100] 上一会话未完成，忽略本次设置\r\n");
        return false;
    }
    if (!a7_build_session(kind, prog, val)) {
        PRINTF("[7100] 会话构建失败（命令缓冲不足）\r\n");
        return false;
    }

    s_sess_prog = prog;
    s_sess_kind = kind;
    s_sess_val  = val;
    s_sess_active = true;
    PRINTF("[7100] --- session start: %s prog=%u val=%u cmds=%u ---\r\n",
           kind == 0 ? "Denoise" : "DFBC", prog, val, s_step_cnt);
    return true;
}

bool dsp_7100_set_denoise(uint8_t prog, uint8_t level)
{
    if (prog < 1 || prog > 4 || level > 4) return false;
    return a7_session_start(0, prog, level);
}

bool dsp_7100_set_dfbc(uint8_t prog, uint8_t onoff)
{
    if (prog < 1 || prog > 4) return false;
    return a7_session_start(1, prog, onoff ? 1 : 0);
}
