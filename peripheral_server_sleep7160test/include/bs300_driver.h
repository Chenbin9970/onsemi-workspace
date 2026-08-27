#ifndef BS300_DRIVER_H
#define BS300_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "bs300_program_read.h"
#include "bs300_param_encode.h"
#include "bs300_calib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One-shot init: I2C init + load programs from Main Flash or read from BS300.
 * Blocks during I2C communication (~2-3s first boot, ~0 on cached boot).
 * Returns true on success. */
bool bs300_driver_init(void);

/* Get parsed program struct (loaded from Main Flash on each call).
 * Returns NULL if prog_idx > 3 or driver not initialized.
 * The returned pointer is valid only until the next call to this function. */
const bs300_program_data_t *bs300_driver_get_program(uint8_t prog_idx);

/* Get raw calibration data (144 bytes, 3 packets).
 * Only valid after init + chip read. Returns NULL on NVR-cached boot. */
const uint8_t *bs300_driver_get_calibration(void);

/* Force re-read all 4 programs from BS300 + update Main Flash cache.
 * Returns true on success. */
bool bs300_driver_refresh(void);

/* Get structured program data from cache (Main Flash-backed).
 * Returns NULL if not cached or prog_idx > 3. */
const bs300_prog_struct_t *bs300_driver_get_struct(uint8_t prog_idx);

/* Get parsed calibration data from cache.
 * Returns NULL if not loaded. */
const bs300_calib_t *bs300_driver_get_calib(void);

/* Returns true if boot cache is populated. */
bool bs300_driver_is_cached(void);

#ifdef __cplusplus
}
#endif

#endif /* BS300_DRIVER_H */
