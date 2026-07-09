#ifndef BS300_DRIVER_H
#define BS300_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "bs300_program_read.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One-shot init: I2C init + load programs from NVR or read from BS300.
 * Blocks during I2C communication (~2-3s first boot, ~0 on cached boot).
 * Returns true on success. */
bool bs300_driver_init(void);

/* Get parsed program struct (loaded from NVR3 on each call).
 * Returns NULL if prog_idx > 3 or driver not initialized.
 * The returned pointer is valid only until the next call to this function. */
const bs300_program_data_t *bs300_driver_get_program(uint8_t prog_idx);

/* Get raw calibration data (144 bytes, 3 packets).
 * Only valid after init + chip read. Returns NULL on NVR-cached boot. */
const uint8_t *bs300_driver_get_calibration(void);

/* Force re-read all 4 programs from BS300 + update NVR cache.
 * Returns true on success. */
bool bs300_driver_refresh(void);

/* Sync one program's parameters from NVR3 cache to BS300 RAM.
 * Encodes all 31 Param I2C commands and writes to BS300 via I2C.
 * Blocks during I2C communication (~3-4s).
 * Prerequisite: bs300_driver_init() must have succeeded.
 * Calibration must be loaded (first-boot or after refresh). */
bool bs300_driver_sync_ram(uint8_t prog_idx);

#ifdef __cplusplus
}
#endif

#endif /* BS300_DRIVER_H */
