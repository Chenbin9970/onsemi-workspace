/* DIO13 被动监听实验：机制与静默清单见 include/dsp_7100_sniff.h。
 *
 * 相位机（主循环每次 poll 推进一步）：
 *   SN_WAIT_RISE  只认上升沿（无 tick 兜底）→ 有新的沿就进 SN_READ
 *   SN_READ       读 3B 头 + 按头里 len16 读满 payload（线上 `05 …`），分两行打印；
 *                 认出的属性（0x0016 程序号 / 0x0012 音量）上报给手机
 *                 （rempro_push_7100_notify，BLE 未连接则内部跳过）
 *   SN_ACK        记 rise/fall 基准 → 发 `04 82`（打 ok）→ 进 SN_ECHO
 *   SN_ECHO       只吃 82 那记「高→低」→ 取基准回 SN_WAIT_RISE；
 *                 窗口里若出现**新的上升沿**（7100 又抬线）→ 不当回声，去读它
 *
 * ⚠ 82 的自效应只有 fall（7100 高→低）。实测九轮（15:05 #1~#4、15:06 #1~#4）里
 *   82 之后**只有 fall、从无 rise** ⇒ 窗口里出现 rise 就只能是 7100 自己抬的线
 *   （也就是「还有一帧」）。⚠ 这与读回里的注释「82 自己就产生一个上升沿」
 *   **冲突**（dsp_7100_init.c 的失败分支）—— 那条待重核，别照抄。 */

#include "dsp_7100_sniff.h"
#include "dsp_7100_init.h"
#include "i2c_7100_hal.h"
#include "app.h"
#include "ble_rempro_cmd.h"   /* rempro_push_7100_notify()：认出来的通知上报给手机 */
#include <printf.h>
#include <string.h>

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

#define SNIFF_HDR_LEN    3    /* 应答头固定 3B：<status> <len16-LE>（照抓包 pkt 17 的 `05 44 00 00`） */
#define SNIFF_DUMP_MAX  32    /* 单行最多 dump 的 payload 字节（同 dsp_7100_init.c 的 DSP_DUMP_MAX） */
#define SNIFF_ECHO_WAIT  3    /* 等 82 那记「高→低」落定的上限（ms）；实测 <1ms 就到 */

static const uint8_t s_end82[] = { 0x82 };

typedef enum { SN_WAIT_RISE, SN_READ, SN_ACK, SN_ECHO } sniff_st_t;

static uint8_t  s_st = SN_WAIT_RISE;
static uint8_t  s_init;        /* 基准是否已取（首次 poll 取，避开引导期那堆边沿） */
static uint32_t s_base_rise;   /* 触发基准：rise_cnt 不等于它才算「新的上升沿」 */
static uint32_t s_pre_rise;    /* 发 82 之前记的上升沿数（**82 之后 rise 前进 = 7100 又抬线**） */
static uint32_t s_pre_fall;    /* 发 82 之前记的下降沿数（用来认 82 那记「高→低」） */
static uint8_t  s_echo_ms;     /* SN_ECHO 已等了几 ms */
static uint32_t s_n;           /* 已完成的轮次 */

/* 基准必须在引导 + DIO11 脉冲**之后**取：那两段的边沿不属于「7100 自己发的」。 */
static void sniff_start(void)
{
    s_base_rise = dsp_7100_dio13_rise_cnt();
    s_pre_rise  = s_base_rise;
    s_pre_fall  = dsp_7100_dio13_fall_cnt();
    s_init      = 1;
    PRINTF("[SNIFF] start: base rise=%lu fall=%lu（此后只等上升沿，不发任何命令帧）\r\n",
           (unsigned long)s_base_rise, (unsigned long)s_pre_fall);
}

/* 一行 dump（超 SNIFF_DUMP_MAX 截断打 ..），一次 PRINTF 输出。
 * 照 dsp_7100_init.c 的 dump_hex —— 那个是 static 拿不到，故自备一份同形的。
 * ⚠ pack printf.c 的 vsprintf 写 200B 静态缓冲且无边界检查：
 *   SNIFF_DUMP_MAX×3+4 = 100 字符；调用点行首前缀（`[SNIFF] #<n> data ok=<u>:`）最坏 30，
 *   整行最坏 ≈131B < 200（头那行 ≈84B）。加字段或调大 SNIFF_DUMP_MAX 须重新核算。 */
static void sniff_dump(const uint8_t *p, uint16_t len)
{
    static const char hexd[] = "0123456789ABCDEF";
    char line[SNIFF_DUMP_MAX * 3 + 4];
    uint16_t n = (len > SNIFF_DUMP_MAX) ? SNIFF_DUMP_MAX : len;
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

/* 上升沿触发后的读：先读 3B 头，再按头里的 len16 读满 payload（照读回的两步读法）。
 * ⚠ 必须读满 len16，不能只读头：只读头会把 payload 留在 7100 里（本次改动的起因）。
 *   抓包实证 `<status> <len16-LE> <payload>`：pkt 24 `21 01 00 00`（len16=1 → 1B payload）、
 *   pkt 26 `01 10 00 …`（16B）、pkt 28 `02 0A 00 …`（10B 串号）。
 * 分两行打印（头一行、payload 一行）：pack printf 200B 缓冲无边界检查，挤一行会顶到 200B。 */
static void sniff_read(void)
{
    uint8_t  hdr[SNIFF_HDR_LEN];
    uint8_t  pay[DSP_INIT_RX_BUF];
    uint16_t hlen, rd;
    bool     okh, okp = true;
    uint32_t rise = dsp_7100_dio13_rise_cnt();

    memset(hdr, 0, sizeof(hdr));
    okh  = i2c_7100_read(I2C_7100_ADDR, hdr, sizeof(hdr));
    hlen = (uint16_t)hdr[1] + ((uint16_t)hdr[2] << 8);
    rd   = 0;
    if (okh && hlen > 0) {
        rd  = (hlen > DSP_INIT_RX_BUF) ? (uint16_t)DSP_INIT_RX_BUF : hlen;
        okp = i2c_7100_read(I2C_7100_ADDR, pay, rd);
    }

    /* lvl 打在触发之后：上升沿刚过，正常应读回 1 —— 这是 §25.9.4 第 3 条要的极性数据。 */
    PRINTF("[SNIFF] #%lu rise=%lu lvl=%u ok=%u HDR %02X %02X %02X len16=%u DATA%uB\r\n",
           (unsigned long)(s_n + 1), (unsigned long)rise,
           (unsigned)DIO_DATA->ALIAS[13], okh,
           hdr[0], hdr[1], hdr[2], hlen, rd);

    if (rd > 0) {
        PRINTF("[SNIFF] #%lu data ok=%u:", (unsigned long)(s_n + 1), okp);
        sniff_dump(pay, rd);
        PRINTF("\r\n");

        /* 认出来的通知（程序号 0x0016 / 音量 0x0012）上报给手机；BLE 未连接时内部跳过。
         * 只解前 3B（`<addr_hi> <addr_lo> <值>`），故按 3 截断后传入。 */
        if (okp) {
            rempro_push_7100_notify(pay, (uint8_t)((rd > 3u) ? 3u : rd));
        }
    }
}

static void sniff_echo(void)
{
    uint32_t rise = dsp_7100_dio13_rise_cnt();
    uint32_t fall = dsp_7100_dio13_fall_cnt();

    /* ① 先认新的上升沿：82 不会抬线（实测九轮 82 之后从无 rise），所以窗口里的 rise
     *    只能是 7100 自己又抬的 —— 它在说「还有一帧，再来读」。**不能当回声吃掉**：
     *    留旧基准（s_pre_rise），s_base_rise 还没前进 ⇒ SN_WAIT_RISE 立刻放行去读。
     *    若 7100 连着抬，就一帧一帧读到它不抬为止（每帧一行日志，真自激也一目了然）。 */
    if (rise != s_pre_rise) {
        PRINTF("[SNIFF] 回声期出现新上升沿 %lu→%lu，7100 又抬线 → 去读\r\n",
               (unsigned long)s_pre_rise, (unsigned long)rise);
        s_base_rise = s_pre_rise;
        s_n++;
        s_st = SN_WAIT_RISE;
        return;
    }

    /* ② 只有「高→低」：这才是我们那条 82 生效（7100 事务结束）→ 吃进基准后静止。 */
    if (fall != s_pre_fall) {
        s_base_rise = rise;
        s_n++;
        s_st = SN_WAIT_RISE;
        return;
    }

    /* ③ 到上限还没有任何沿：照取当前为基准，别把后面卡死。 */
    if (s_echo_ms >= SNIFF_ECHO_WAIT) {
        PRINTF("[SNIFF] #%lu 等 82 回声超时 %ums，直接取基准\r\n",
               (unsigned long)(s_n + 1), (unsigned)SNIFF_ECHO_WAIT);
        s_base_rise = rise;
        s_n++;
        s_st = SN_WAIT_RISE;
        return;
    }
    s_echo_ms++;
    Sys_Watchdog_Refresh();
    Sys_Delay_ProgramROM(SystemCoreClock / 1000UL);   /* 1ms */
}

/* 会话结束、sniff 重新接管时调（dsp_7100_cmd.c 的 a7_session_finish）：
 * 把触发基准拉到当前值。会话期 dsp_7100_sniff_poll() 被 cmd_busy 挡着不跑，
 * 但会话自己的读与 82 已经把 DIO13 边沿计数推高了 —— 基准不动的话，恢复的第一拍
 * 必然把那个差值判成「7100 抬线了」：凭空读一帧（HDR 00 00 00）+ 白发一条 04 82。
 * ⚠ 不能用「线是不是高的」代替这个校正：实测那一拍 [IO] 打的是 D13=0。 */
void dsp_7100_sniff_rebase(void)
{
    s_st        = SN_WAIT_RISE;
    s_base_rise = dsp_7100_dio13_rise_cnt();
}

void dsp_7100_sniff_poll(void)
{
    if (!s_init) {
        sniff_start();
    }

    switch (s_st) {
    case SN_WAIT_RISE:
        if (dsp_7100_dio13_rise_cnt() == s_base_rise) {
            return;                    /* 只等沿，不等时间 —— 没沿就静止 */
        }
        s_st = SN_READ;
        return;
    case SN_READ:
        sniff_read();
        s_st = SN_ACK;
        return;
    case SN_ACK:
    {
        /* 两个基准都记在发 82 **之前**：之后任何 rise 前进都只能来自 7100。 */
        s_pre_rise = dsp_7100_dio13_rise_cnt();
        s_pre_fall = dsp_7100_dio13_fall_cnt();
        bool ok82 = i2c_7100_write(I2C_7100_ADDR, s_end82, sizeof(s_end82));   /* 04 82 */
        /* 这一行必须打：不打的话「发没发 04 82」在日志里无从判断
         * （读回那边每条都 dump 82，见 dsp_7100_init.c 的 RB_DUMP）。 */
        PRINTF("[SNIFF] #%lu W 04 82 ok=%u\r\n", (unsigned long)(s_n + 1), ok82);
        s_echo_ms = 0;
        s_st = SN_ECHO;
        return;
    }
    case SN_ECHO:
        sniff_echo();
        return;
    default:
        s_st = SN_WAIT_RISE;
        return;
    }
}
