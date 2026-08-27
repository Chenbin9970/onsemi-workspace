#ifndef BS300_HAL_H
#define BS300_HAL_H

/* 兼容头：I2C bit-bang 驱动已改名 i2c_7100_hal（7100 通讯）。
 * BS300 协议层（bs300_driver/startup/ram_sync）沿用旧符号名，这里统一映射到新名。 */

#include "i2c_7100_hal.h"

#define BS300_I2C_ADDR         I2C_7100_ADDR
#define BS300_I2C_DELAY_FAST   I2C_7100_DELAY_FAST
#define BS300_I2C_DELAY_ACTIVE I2C_7100_DELAY_ACTIVE
#define BS300_I2C_DELAY_NORMAL I2C_7100_DELAY_NORMAL
#define BS300_I2C_SCL_DIO      I2C_7100_SCL_DIO
#define BS300_I2C_SDA_DIO      I2C_7100_SDA_DIO

#define bs300_hal_init       i2c_7100_hal_init
#define bs300_i2c_write      i2c_7100_write
#define bs300_i2c_read       i2c_7100_read
#define bs300_i2c_set_speed  i2c_7100_set_speed
#define bs300_delay_ms       i2c_7100_delay_ms

#endif /* BS300_HAL_H */
