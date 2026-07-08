#ifndef BS300_STORAGE_H
#define BS300_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "bs300_program_read.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Erase NVR3 sector. Call once before incremental writes. */
bool bs300_storage_erase(void);

/* Write one program's raw 480B data to NVR3 at the given index (0-3).
 * Call after erase, before finalize. */
bool bs300_storage_write_program(uint8_t idx, const uint8_t *data);

/* Finalize: compute CRC16 over all 4 programs and write header.
 * Returns true if all 4 program slots have been written. */
bool bs300_storage_finalize(void);

/* Load one program's raw 480B data from NVR3. */
void bs300_storage_load_program(uint8_t idx, uint8_t *data_out);

/* Check if NVR3 contains valid BS300 program data. */
bool bs300_storage_is_valid(void);

/* Invalidate stored data (force re-read from BS300 on next init). */
void bs300_storage_invalidate(void);

#ifdef __cplusplus
}
#endif

#endif /* BS300_STORAGE_H */
