#ifndef BS300_STORAGE_H
#define BS300_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "bs300_program_read.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Write one program's raw 480B data to its dedicated Main Flash sector.
 * Independent per-sector erase — changing one program doesn't affect others. */
bool bs300_storage_write_program(uint8_t idx, const uint8_t *data);

/* Load one program's raw 480B data from its Main Flash sector. */
void bs300_storage_load_program(uint8_t idx, uint8_t *data_out);

/* Check if a single program's sector contains valid data. */
bool bs300_storage_is_valid(uint8_t idx);

/* Invalidate one program's sector (force re-read from BS300 on next init). */
void bs300_storage_invalidate(uint8_t idx);

/* ---- Settings (active program + volume per program) ---- */

/* Save active program, volume, EQ, and denoise levels to Settings sector. */
bool bs300_settings_save(uint8_t active_prog, const uint8_t *volume,
                          const int8_t *eq_low, const int8_t *eq_mid,
                          const int8_t *eq_high, const uint8_t *denoise);

/* Load active program, volume, EQ, and denoise levels from Settings sector. */
bool bs300_settings_load(uint8_t *active_prog, uint8_t *volume,
                          int8_t *eq_low, int8_t *eq_mid, int8_t *eq_high,
                          uint8_t *denoise);

/* Invalidate settings sector. */
void bs300_settings_invalidate(void);

#ifdef __cplusplus
}
#endif

#endif /* BS300_STORAGE_H */
