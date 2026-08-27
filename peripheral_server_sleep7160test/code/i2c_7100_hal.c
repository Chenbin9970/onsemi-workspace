/**
 * 7100 I2C HAL — 硬件 I2C (I2C0) master，中断驱动。
 * 引脚: SCL=DIO0, SDA=DIO1；速度 ~400kHz。
 * 参考 RSL10 remote_mic_tx_raw 样例 + CMSIS 驱动 I2C_RSLxx。
 * 纯轮询在地址 ACK 后卡住（外设每字节需 CPU 经中断响应），改用 I2C 中断推进。
 */

#include "i2c_7100_hal.h"
#include <rsl10.h>

#define I2C_7100_TIMEOUT_MAX    2000000U

/* 传输状态（I2C_IRQHandler 使用） */
static const uint8_t *s_tx_buf;
static uint8_t        s_tx_len;
static uint8_t        s_tx_idx;
static volatile bool  s_tx_done;
static volatile bool  s_tx_ok;
static volatile bool  s_tx_active;

/* 读状态（I2C_IRQHandler 使用，master read） */
static uint8_t       *s_rx_buf;
static uint8_t        s_rx_len;
static uint8_t        s_rx_idx;
static volatile bool  s_rx_done;
static volatile bool  s_rx_ok;
static volatile bool  s_rx_active;

static bool           s_hw_init_done;

static void hw_ensure_init(void)
{
    if (s_hw_init_done) return;
    s_hw_init_done = true;

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
            if (s_rx_idx < s_rx_len - 1) {
                Sys_I2C_ACK();
            } else {
                Sys_I2C_NACKAndStop();
            }
            s_rx_buf[s_rx_idx++] = (uint8_t)I2C->DATA;
        } else if (st & I2C_DATA_EVENT) {
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

bool i2c_7100_write(uint8_t addr, const uint8_t *data, uint8_t len)
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
    s_tx_active = false;
    return s_tx_ok;
}

bool i2c_7100_read(uint8_t addr, uint8_t *data, uint8_t len)
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
    s_rx_active = false;
    return s_rx_ok;
}

/* 硬件 I2C 速度由 prescale 固定（410kHz），此接口保留以兼容调用方 */
void i2c_7100_set_speed(uint32_t delay) { (void)delay; }

/* 忙等毫秒延时（ROM 循环，保 CPU 唤醒）——I2C 包间延时用 */
void i2c_7100_delay_ms(uint32_t ms)
{
    Sys_Delay_ProgramROM(ms * SystemCoreClock / 1000);
}
