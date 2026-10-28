/**
 * 7100 I2C HAL — 硬件 I2C (I2C0) master，中断驱动。
 * 引脚: SCL=DIO0, SDA=DIO1；速度 ~400kHz。
 * 参考 RSL10 remote_mic_tx_raw 样例 + CMSIS 驱动 I2C_RSLxx。
 * 纯轮询在地址 ACK 后卡住（外设每字节需 CPU 经中断响应），改用 I2C 中断推进。
 * len 用 uint16_t：支持 >255（7100 512B 页写 / 612B 大块读须单事务一次完成）。
 * 超时显式标失败 + Reset（不吞错）。
 */

#include "i2c_7100_hal.h"
#include <rsl10.h>
#include <printf.h>

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

#define I2C_7100_TIMEOUT_MAX    2000000U
#define I2C_7100_READ_TRIES     3   /* 一次读最多分几段（超时续读），见 i2c_7100_read() */

/* 传输状态（I2C_IRQHandler 使用） */
static const uint8_t *s_tx_buf;
static uint16_t       s_tx_len;
static uint16_t       s_tx_idx;
static volatile bool  s_tx_done;
static volatile bool  s_tx_ok;
static volatile bool  s_tx_active;

/* 读状态（I2C_IRQHandler 使用，master read） */
static uint8_t       *s_rx_buf;
static uint16_t       s_rx_len;
static volatile uint16_t s_rx_idx;  /* ISR 递增、主上下文当返回值读 → 必须 volatile（-O2 会折叠成 0） */
static volatile bool  s_rx_done;
static volatile bool  s_rx_ok;
static volatile bool  s_rx_active;

static bool           s_hw_init_done;

/* I2C 硬件配置（引脚 + 控制器 + 速度 + 中断）。除首次初始化外，
 * DIO0 分时复用采样结束后也要整体重配一遍（见 i2c_7100_pin_restore_after_adc）。 */
static void hw_apply_config(void)
{
    /* 引脚: SCL=DIO0, SDA=DIO1。强上拉 + 6x 驱动 + 使能滤波 */
    Sys_I2C_DIOConfig(DIO_6X_DRIVE | DIO_LPF_ENABLE | DIO_STRONG_PULL_UP,
                      I2C_7100_SCL_DIO, I2C_7100_SDA_DIO);
    Sys_I2C_Reset();

    /* CM3 控制器 + 采样时钟 + 关自动 ACK + STOP 中断 */
    I2C->CTRL0 = (I2C_CONTROLLER_CM3 | I2C_SAMPLE_CLK_ENABLE |
                  I2C_AUTO_ACK_DISABLE | I2C_STOP_INT_ENABLE);

    /* 速度：prescale = SystemCoreClock/(3*400kHz)-1 = 12 @16MHz → SCL≈410kHz */
    I2C->CTRL0 = ((I2C->CTRL0 & ~I2C_CTRL0_SPEED_Mask) |
                  (12U << I2C_CTRL0_SPEED_Pos));

    /* 使能 I2C 中断（低优先级，避免抢占 BLE/音频） */
    NVIC_SetPriority(I2C_IRQn, 4);
    NVIC_ClearPendingIRQ(I2C_IRQn);
    NVIC_EnableIRQ(I2C_IRQn);
}

static void hw_ensure_init(void)
{
    if (s_hw_init_done) return;
    s_hw_init_done = true;

    hw_apply_config();
}

/* I2C 中断：master 写/读，每字节事件推进（参考样例 I2C_IRQHandler + CMSIS 驱动） */
void I2C_IRQHandler(void)
{
    uint32_t st = Sys_I2C_Get_Status();

    if (st & I2C_BUS_ERROR) {
        Sys_I2C_Reset();
        s_tx_ok = false;
        s_tx_done = true;
        s_rx_ok = false;
        s_rx_done = true;
        return;
    }
    if (st & I2C_STOP_DETECTED) {
        /* 读方向：未收满 len 字节就 STOP → 地址 NACK 或传输失败 */
        if (s_rx_active && s_rx_idx != s_rx_len) s_rx_ok = false;
        s_tx_done = true;
        s_rx_done = true;
        return;
    }

    if (st & I2C_IS_READ) {
        /* ---- master read：每字节 ACK，最后一字节 NACKAndStop ---- */
        if (!s_rx_active) return;
        if (st & I2C_BUFFER_FULL) {
            /* 人为拉长"8 时钟→ACK"间隔（~3µs），便于对齐参考机时序 */
            Sys_Delay_ProgramROM(3 * (SystemCoreClock / 1000000UL));
            if (s_rx_idx < s_rx_len - 1) {
                Sys_I2C_ACK();
            } else {
                Sys_I2C_NACKAndStop();
            }
            s_rx_buf[s_rx_idx++] = (uint8_t)I2C->DATA;
        } else if (st & I2C_DATA_EVENT) {
            Sys_Delay_ProgramROM(3 * (SystemCoreClock / 1000000UL)); /* ~3µs */
            Sys_I2C_ACK();   /* 数据事件 → 允许开始接收 */
        }
        return;
    }

    /* ---- master write ---- */
    if (!s_tx_active) return;

    if ((st >> I2C_STATUS_ACK_STATUS_Pos) & 1) {
        /* ACK_STATUS=1 → NACK（从机不应答） */
        s_tx_ok = false;
        s_tx_done = true;
        return;
    }
    /* ACK_STATUS=0 → ACK（I2C_HAS_ACK=0），发下一字节 */
    if (s_tx_idx < s_tx_len) {
        I2C->DATA = s_tx_buf[s_tx_idx++];
    } else {
        I2C_CTRL1->LAST_DATA_ALIAS = I2C_LAST_DATA_BITBAND;
        s_tx_done = true;
    }
}

bool i2c_7100_hal_init(void)
{
    hw_ensure_init();
    return true;
}

/* ---- DIO0 分时复用：电池 AD 采样 ----
 * 1664 板上电池分压与 I2C SCL 同在 DIO0。采样前把该脚从 I2C 释放：
 * ① Reset 让 I2C 状态机停手；② DIO_NO_PULL 关掉内部强上拉 —— 否则上拉会把
 * 1M+360k 的弱分压拉满，ADC 只会读到 ≈VDD。
 * 调用方须保证当前没有 I2C 传输在跑（本 HAL 的读写都是阻塞式，同一任务上下文
 * 里调用即天然满足）。 */
void i2c_7100_pin_release_for_adc(void)
{
    Sys_I2C_Reset();

    /* 清掉可能残留的传输标志，避免下次读写空等超时 */
    s_tx_active = false;
    s_rx_active = false;
    s_tx_done   = true;
    s_rx_done   = true;

    Sys_DIO_Config(I2C_7100_SCL_DIO, DIO_MODE_GPIO_IN_0 | DIO_NO_PULL |
                   DIO_LPF_DISABLE);
}

/* 采样结束：DIO0/DIO1 交还 I2C（引脚 + 控制器配置整体重配）。 */
void i2c_7100_pin_restore_after_adc(void)
{
    hw_apply_config();
}

bool i2c_7100_write(uint8_t addr, const uint8_t *data, uint16_t len)
{
    uint32_t t;
    if (!data || !len) return false;
    hw_ensure_init();

    /* 等上次传输结束 */
    t = 0;
    while ((s_tx_active || s_rx_active) && t < I2C_7100_TIMEOUT_MAX) {
        t++;
        Sys_Watchdog_Refresh();
    }

    s_tx_buf    = data;
    s_tx_len    = len;
    s_tx_idx    = 0;
    s_tx_done   = false;
    s_tx_ok     = true;
    s_tx_active = true;

    Sys_I2C_StartWrite(addr);

    t = 0;
    while (!s_tx_done && t < I2C_7100_TIMEOUT_MAX) {
        t++;
        Sys_Watchdog_Refresh();
    }
    if (!s_tx_done) {
        /* 超时（从机时钟拉伸/无响应）→ 显式标失败，不吞错 */
        s_tx_ok = false;
        Sys_I2C_Reset();
    }
    s_tx_active = false;
    return s_tx_ok;
}

/* 单次 master read：不重试，返回**实际进缓冲的字节数**（超时 / 提前 STOP 时 < len）。
 * 7100 的读指针是纯字节流：master 已经时钟出去的字节它不会再给，所以「已收 M 字节」
 * 就是它前进的距离 —— 续读必须从 data+M 接着要，不能从头重读（2026-10-28 实测：
 * 读超时之后下一轮读到的第一个字节仍是漏掉的那个）。 */
static uint16_t i2c_rx_once(uint8_t addr, uint8_t *data, uint16_t len)
{
    uint32_t t;

    /* 等上次传输结束 */
    t = 0;
    while ((s_tx_active || s_rx_active) && t < I2C_7100_TIMEOUT_MAX) {
        t++;
        Sys_Watchdog_Refresh();
    }

    s_rx_buf    = data;
    s_rx_len    = len;
    s_rx_idx    = 0;
    s_rx_done   = false;
    s_rx_ok     = true;
    s_rx_active = true;

    Sys_I2C_StartRead(addr);

    t = 0;
    while (!s_rx_done && t < I2C_7100_TIMEOUT_MAX) {
        t++;
        Sys_Watchdog_Refresh();
    }
    if (!s_rx_done) {
        /* 超时（从机时钟拉伸/无响应）→ 显式标失败，不吞错 */
        s_rx_ok = false;
        Sys_I2C_Reset();
    }
    s_rx_active = false;
    return s_rx_idx;
}

/* 读 len 字节：超时就带着「已收 M 字节」**续读**剩下的，最多分 I2C_7100_READ_TRIES 段。
 * 为什么必须续读而不是重读整帧：读超时那一刻 7100 已经吐出去 M 个字节、读指针前进了 M，
 * 从头重读会把流错位（读回第 26 步 306B 就是这么卡死的，见开发文档 §38）。
 * 分 3 段是要给「7100 还在备数据（时钟拉伸）」留余量；真实失败（从机不应答）每段代价
 * 一次 TIMEOUT（≈37ms），3 段约 110ms，只在故障时发生。 */
bool i2c_7100_read(uint8_t addr, uint8_t *data, uint16_t len)
{
    uint16_t got = 0;
    uint16_t n;
    uint8_t  tries = 0;

    if (!data || !len) return false;
    hw_ensure_init();

    while (got < len && tries < I2C_7100_READ_TRIES) {
        n   = i2c_rx_once(addr, data + got, (uint16_t)(len - got));
        got = (uint16_t)(got + n);
        if (got < len) {
            tries++;
            PRINTF("[I2C] 读 %uB 断在 %u（第 %u 段）→ 续读 %u\r\n",
                   (unsigned)len, (unsigned)got, (unsigned)tries,
                   (unsigned)(len - got));
            i2c_7100_delay_ms(1);
        }
    }
    if (got < len) {
        PRINTF("[I2C] 读 %uB 失败：只到 %u\r\n", (unsigned)len, (unsigned)got);
        return false;
    }
    return true;
}

/* 硬件 I2C 速度由 prescale 固定（410kHz），此接口保留以兼容调用方 */
void i2c_7100_set_speed(uint32_t delay) { (void)delay; }

/* 忙等毫秒延时（ROM 循环，保 CPU 唤醒）——I2C 包间延时用 */
void i2c_7100_delay_ms(uint32_t ms)
{
    Sys_Delay_ProgramROM(ms * SystemCoreClock / 1000);
}
