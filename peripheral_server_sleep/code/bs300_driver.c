/**
 * BS300 Driver — init flow, on-demand program loading, calibration read
 *
 * bs300_driver_init() orchestrates the full startup sequence:
 *   1. I2C hardware init + 800ms DSP power stabilization
 *   2. Check NVR3 cache → validate
 *   3. Else: bs300_startup() → read all 4 programs → save to NVR3 incrementally
 *
 * bs300_driver_get_program(idx) reads raw data from NVR3, parses it,
 * and returns a pointer to a single static buffer. The pointer is valid
 * only until the NEXT call to this function.
 */

#include "bs300_driver.h"
#include "bs300_hal.h"
#include "bs300_startup.h"
#include "bs300_storage.h"
#include "bs300_ram_sync.h"
#include "app.h"
#include <string.h>

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

#define CALIB_SIZE  144

static uint8_t             s_raw_buf[BS300_TOTAL_DATA];   /* 480B */
static bs300_program_data_t s_prog_buf;                    /* per-call result */
static uint8_t             s_calibration[CALIB_SIZE];      /* 144B */
static bool                s_initialized;
static bool                s_calib_loaded;

/* ============================================================
 * Internal: read all data from BS300 chip, save to NVR3
 * ============================================================ */

static bool read_and_save_all(void)
{
    /* Phase 1: Unlock chip */
    if (!bs300_startup()) {
        PRINTF("BS300: startup FAIL\r\n");
        return false;
    }
    PRINTF("BS300: startup OK\r\n");

    /* Phase 2: Erase NVR3 once, then read+save each program */
    if (!bs300_storage_erase()) {
        PRINTF("BS300: NVR3 erase FAIL\r\n");
        return false;
    }

    for (uint8_t i = 0; i < 4; i++) {
        if (!bs300_program_read(i, s_raw_buf)) {
            PRINTF("BS300: program %u read FAIL\r\n", i);
            return false;
        }
        if (!bs300_storage_write_program(i, s_raw_buf)) {
            PRINTF("BS300: program %u NVR write FAIL\r\n", i);
            return false;
        }
        bs300_delay_ms(5);
    }
    PRINTF("BS300: 4 programs read + saved to NVR3\r\n");

    /* Phase 3: Finalize NVR3 (CRC + header) */
    if (!bs300_storage_finalize()) {
        PRINTF("BS300: NVR3 finalize FAIL\r\n");
        return false;
    }

    /* Phase 4: Read calibration */
    if (!bs300_read_calibration(s_calibration)) {
        PRINTF("BS300: calibration read FAIL\r\n");
        return false;
    }
    s_calib_loaded = true;
    PRINTF("BS300: calibration read OK\r\n");

    return true;
}

/* ============================================================
 * Public API
 * ============================================================ */

bool bs300_driver_init(void)
{
    if (s_initialized) return true;

    /* Step 1: I2C init + DSP power stabilization */
    if (!bs300_hal_init()) {
        PRINTF("BS300: I2C init FAIL\r\n");
        return false;
    }
    bs300_delay_ms(800);

    /* Step 2: Try NVR cache first */
    if (bs300_storage_is_valid()) {
        PRINTF("BS300: NVR cache valid\r\n");
        s_initialized = true;
        return true;
    }

    /* Step 3: Read everything from BS300 + save to NVR3 */
    PRINTF("BS300: NVR empty, reading from chip...\r\n");
    if (!read_and_save_all()) return false;

    s_initialized = true;
    return true;
}

const bs300_program_data_t *bs300_driver_get_program(uint8_t prog_idx)
{
    if (!s_initialized || prog_idx > 3) return NULL;

    /* Read raw 480B from NVR3, parse into single static buffer */
    bs300_storage_load_program(prog_idx, s_raw_buf);
    if (!bs300_program_parse(s_raw_buf, &s_prog_buf))
        return NULL;
    return &s_prog_buf;
}

const uint8_t *bs300_driver_get_calibration(void)
{
    if (!s_initialized || !s_calib_loaded) return NULL;
    return s_calibration;
}

bool bs300_driver_refresh(void)
{
    s_initialized = false;
    s_calib_loaded = false;

    if (!read_and_save_all()) return false;

    s_initialized = true;
    return true;
}

bool bs300_driver_sync_ram(uint8_t prog_idx)
{
    if (!s_initialized) {
        PRINTF("BS300: driver not initialized\r\n");
        return false;
    }
    return bs300_ram_sync(prog_idx);
}
