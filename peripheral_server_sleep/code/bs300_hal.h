#ifndef BS300_HAL_H
#define BS300_HAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BS300 I2C slave address (7-bit), bus byte = (0x01<<1)|R/W → 0x02/0x03 */
#define BS300_I2C_ADDR  0x01

/* I2C bus speed: 10 kHz */
#define BS300_I2C_SPEED_HZ  10000

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

/* Delay milliseconds (polling, not sleep). */
void bs300_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* BS300_HAL_H */
