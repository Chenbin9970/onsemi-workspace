/**
 * BS300 I2C HAL — verified I2C primitives from bt895x driver.
 *
 * Differences from previous broken version:
 *   - SDA input uses DIO_WEAK_PULL_UP (not DIO_NO_PULL)
 *   - ACK delay 5x longer than data-bit delay
 *   - init() does SDA toggle + 10ms settle (matches bsp_i2c_init)
 *   - ms delay uses Sys_Delay_ProgramROM(1000) loop
 */

#include "bs300_hal.h"
#include <rsl10.h>

/* ============================================================
 * Pin / timing
 * ============================================================ */

#define SCL_PIN   BS300_I2C_SCL_DIO   /* DIO8 */
#define SDA_PIN   BS300_I2C_SDA_DIO   /* DIO7 */

static const uint32_t bit_delay     = 250;   /* data-bit delay loops (10x slower) */
static const uint32_t ack_delay_val = 1250;  /* ACK delay loops (5x, 10x slower) */

/* ============================================================
 * GPIO helpers — same as verified driver
 * ============================================================ */

static void scl_out(void) { Sys_DIO_Config(SCL_PIN, DIO_MODE_GPIO_OUT_0); }
static void scl_h(void)   { Sys_GPIO_Set_High(SCL_PIN); }
static void scl_l(void)   { Sys_GPIO_Set_Low(SCL_PIN); }

static void sda_out(void) { Sys_DIO_Config(SDA_PIN, DIO_MODE_GPIO_OUT_0); }
static void sda_in(void)  { Sys_DIO_Config(SDA_PIN, DIO_MODE_GPIO_IN_0 | DIO_WEAK_PULL_UP | DIO_LPF_DISABLE); }
static void sda_h(void)   { Sys_GPIO_Set_High(SDA_PIN); }
static void sda_l(void)   { Sys_GPIO_Set_Low(SDA_PIN); }
static uint8_t sda_r(void) { return (uint8_t)DIO_DATA->ALIAS[SDA_PIN]; }

/* ============================================================
 * Delay — match verified driver
 * ============================================================ */

static void delay_loop(uint32_t n) { for (uint32_t i = 0; i < n; i++) Sys_Watchdog_Refresh(); }
static void delay_bit(void)  { delay_loop(bit_delay); }
static void delay_ack(void)  { delay_loop(ack_delay_val); }

static void delay_ms(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++)
        for (uint32_t j = 0; j < 17; j++) { Sys_Watchdog_Refresh(); Sys_Delay_ProgramROM(1000); }
}

/* ============================================================
 * I2C primitives — match verified driver exactly
 * ============================================================ */

static void i2c_start(void)  { scl_out(); sda_out(); scl_h(); sda_h(); delay_bit(); sda_l(); delay_bit(); scl_l(); }
static void i2c_stop(void)   { sda_out(); sda_l(); delay_bit(); scl_h(); delay_bit(); sda_h(); }

static void i2c_tx_byte(uint8_t dat)
{
    sda_out();
    for (uint8_t i = 0; i < 8; i++) {
        if (dat & 0x80) sda_h(); else sda_l();
        delay_bit(); scl_h(); delay_bit(); scl_l();
        dat <<= 1;
    }
}

static bool i2c_rx_ack(void)
{
    bool ack = false;
    sda_in(); delay_bit();
    scl_h();  delay_ack();           /* 5x longer for ACK */
    if (!sda_r()) ack = true;
    scl_l();
    return ack;
}

static void i2c_tx_ack(void)  { sda_out(); sda_l(); delay_bit(); scl_h(); delay_ack(); scl_l(); }
static void i2c_tx_nack(void) { sda_out(); sda_h(); delay_bit(); scl_h(); delay_ack(); scl_l(); }

static uint8_t i2c_rx_byte(void)
{
    uint8_t dat = 0;
    sda_in();
    for (uint8_t i = 0; i < 8; i++) { delay_bit(); scl_h(); delay_bit(); dat <<= 1; if (sda_r()) dat |= 1; scl_l(); }
    return dat;
}

/* ============================================================
 * Public API
 * ============================================================ */

bool bs300_hal_init(void)
{
    /* Match bsp_i2c_init: configure + toggle + settle */
    scl_out(); sda_out(); sda_h();
    scl_out(); sda_out(); sda_h();
    delay_ms(10);
    return true;
}

bool bs300_i2c_write(uint8_t addr, const uint8_t *data, uint8_t len)
{
    if (!data || !len) return false;

    i2c_start();
    /* Send address byte: (7-bit addr << 1) | W=0 */
    i2c_tx_byte((uint8_t)((addr << 1) | 0));
    if (!i2c_rx_ack()) { i2c_stop(); return false; }

    for (uint8_t i = 0; i < len; i++) {
        Sys_Watchdog_Refresh();
        i2c_tx_byte(data[i]);
        if (!i2c_rx_ack()) { i2c_stop(); return false; }
    }
    i2c_stop();
    return true;
}

bool bs300_i2c_read(uint8_t addr, uint8_t *data, uint8_t len)
{
    if (!data || !len) return false;

    i2c_start();
    /* Send address byte: (7-bit addr << 1) | R=1 */
    i2c_tx_byte((uint8_t)((addr << 1) | 1));
    if (!i2c_rx_ack()) { i2c_stop(); return false; }

    for (uint8_t i = 0; i < len; i++) {
        Sys_Watchdog_Refresh();
        data[i] = i2c_rx_byte();
        if (i < len - 1) i2c_tx_ack(); else i2c_tx_nack();
    }
    i2c_stop();
    return true;
}

void bs300_delay_ms(uint32_t ms) { delay_ms(ms); }
