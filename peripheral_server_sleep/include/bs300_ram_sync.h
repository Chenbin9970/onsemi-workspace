#ifndef BS300_RAM_SYNC_H
#define BS300_RAM_SYNC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sync one program's parameters from NVR3 cache to BS300 RAM.
 * Encodes all 31 Param I2C commands and sends via bs300_advanced_write().
 * prog_idx: 0-3. Returns true if all commands sent successfully.
 *
 * Prerequisites: bs300_driver_init() must have been called first.
 * The flow internally handles: startup (MUTE→KEY_LOCK→VERIFY_COMM)
 * if chip was not already unlocked, then sends all commands, then ACTIVE. */
bool bs300_ram_sync(uint8_t prog_idx);

#ifdef __cplusplus
}
#endif

#endif /* BS300_RAM_SYNC_H */
