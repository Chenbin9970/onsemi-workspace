#ifndef I2C_7100_HAL_H
#define I2C_7100_HAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 7100 I2C slave address (7-bit) = 0x02。
 * 7100connect 波形总线地址字节 = 0x04（即 0x02<<1|W=0x04）。
 * Sys_I2C_StartWrite 内部左移 1 位，若传 0x04 会发成 0x08（地址错误）。 */
#define I2C_7100_ADDR  0x02

/* I2C speed presets（历史 bit_delay 值）。
 * 注：本 HAL 已是硬件 I2C，速度由 prescale 固定（~410kHz）；
 * 下列常量与 i2c_7100_set_speed() 仅为与参考实现保持一致（当前无调用者）。 */
#define I2C_7100_DELAY_FAST    10   /* DSP stopped, fast I2C */
#define I2C_7100_DELAY_ACTIVE  2    /* 7100 默认 ~316kHz */
#define I2C_7100_DELAY_NORMAL  2    /* 7100 默认 ~316kHz */

/* I2C clock multiplier (RSL10 hardware constant) */
#define I2C_CLK_MUL  3

/* I2C 引脚：SCL=DIO0 / SDA=DIO1
 * （1664 原为 OD_P/OD_N，音频输出关闭后腾出，见开发文档 §3.4/§5） */
#define I2C_7100_SCL_DIO  0
#define I2C_7100_SDA_DIO  1

/* Initialize I2C hardware for 7100 communication.
 * Configures DIO pins and I2C peripheral as master.
 * Returns true on success. */
bool i2c_7100_hal_init(void);

/* Write len bytes to I2C slave. Returns true on success. */
bool i2c_7100_write(uint8_t addr, const uint8_t *data, uint16_t len);

/* Read len bytes from I2C slave. Returns true on success.
 * len 支持 >255（7100 A7 大块读如 0x32=309B / 0x177=378B，须单事务一次读完）。 */
bool i2c_7100_read(uint8_t addr, uint8_t *data, uint16_t len);

/* Set I2C bus speed by bit-delay loop count.
 * Use I2C_7100_DELAY_FAST (25) when DSP is stopped.
 * Use I2C_7100_DELAY_NORMAL (500) before critical commands and when DSP is active.
 * ack_delay is always 5x bit_delay. */
void i2c_7100_set_speed(uint32_t delay);

/* Delay milliseconds (polling, not sleep). */
void i2c_7100_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* I2C_7100_HAL_H */
