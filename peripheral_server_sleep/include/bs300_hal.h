#ifndef BS300_HAL_H
#define BS300_HAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BS300 I2C slave address (7-bit), bus byte = (0x01<<1)|R/W → 0x02/0x03 */
#define BS300_I2C_ADDR  0x01

/* I2C speed presets (bit_delay loop count) */
#define BS300_I2C_DELAY_FAST    10   /* DSP stopped, fast I2C */
#define BS300_I2C_DELAY_NORMAL  500  /* DSP active, slow & reliable */

/* I2C clock multiplier (RSL10 hardware constant) */
#define I2C_CLK_MUL  3

/* I2C pin assignments — must match RTE_Device.h */
#define BS300_I2C_SCL_DIO  8
#define BS300_I2C_SDA_DIO  7

/* Initialize I2C hardware for BS300 communication.
 * Configures DIO pins and I2C peripheral as master.
 * Returns true on success. */
bool bs300_hal_init(void);

/* Write len bytes to I2C slave. Returns true on success. */
bool bs300_i2c_write(uint8_t addr, const uint8_t *data, uint8_t len);

/* Read len bytes from I2C slave. Returns true on success. */
bool bs300_i2c_read(uint8_t addr, uint8_t *data, uint8_t len);

/* Set I2C bus speed by bit-delay loop count.
 * Use BS300_I2C_DELAY_FAST (25) when DSP is stopped.
 * Use BS300_I2C_DELAY_NORMAL (500) before critical commands and when DSP is active.
 * ack_delay is always 5x bit_delay. */
void bs300_i2c_set_speed(uint32_t delay);

/* Delay milliseconds (polling, not sleep). */
void bs300_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* BS300_HAL_H */
