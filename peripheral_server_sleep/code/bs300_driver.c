/**
 * BS300 Driver — full init flow matching AD697N startup sequence.
 *
 * bs300_driver_init():
 *   1. I2C init + 2s DSP power stabilization
 *   2. Unlock chip (MUTE → KEY_LOCK → VERIFY_COMM)
 *   3. First boot: read 4 programs from BS300 Flash → save to Main Flash
 *      Cached boot: skip flash read, load structs from Main Flash
 *   4. MUTE → sync active program to BS300 RAM (31 I2C commands)
 *   5. ACTIVE → start DSP audio processing
 *   6. Cache all 4 programs' inputs/modules for runtime switch
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

static bool s_initialized;
static uint8_t s_raw[BS300_TOTAL_DATA];  /* 480B — shared work buffer */

/* ============================================================
 *  Internal: read programs from BS300 Flash, save to Main Flash
 * ============================================================ */
static bool read_and_save_all(void)
{
    uint8_t i;
    for (i = 0; i < 4; i++) {
        if (!bs300_program_read(i, s_raw)) {
            PRINTF("[BS300] program %u read FAIL\r\n", i);
            return false;
        }
        if (!bs300_storage_write_program(i, s_raw)) {
            PRINTF("[BS300] program %u save FAIL\r\n", i);
            return false;
        }
        bs300_delay_ms(5);
        Sys_Watchdog_Refresh();
    }
    PRINTF("[BS300] 4 programs read + saved\r\n");
    return true;
}

/* ============================================================
 *  Public API
 * ============================================================ */

bool bs300_driver_init(void)
{
    if (s_initialized) return true;

    /* Step 1: I2C init + DSP power stabilization */
    if (!bs300_hal_init()) {
        PRINTF("[BS300] I2C init FAIL\r\n");
        return false;
    }
    bs300_delay_ms(2000);

    /* Step 2: Unlock chip (MUTE → KEY_LOCK → VERIFY_COMM) */
    if (!bs300_startup()) {
        PRINTF("[BS300] startup FAIL\r\n");
        return false;
    }
    PRINTF("[BS300] startup OK\r\n");

    /* Step 3: Load programs from Main Flash cache or read from BS300 Flash */
    if (bs300_storage_is_valid(0)) {
        PRINTF("[BS300] cache valid, skip flash read\r\n");
    } else {
        PRINTF("[BS300] cache empty, reading from chip...\r\n");
        if (!read_and_save_all()) return false;
    }

    /* Step 4: Cache programs + calibration (single read, shared by all) */
    bs300_cache_prog_inputs();

    /* Step 5: Restore settings (active program + volume) if available */
    {
        uint8_t saved_prog = 0;
        uint8_t saved_vol[4];
        if (bs300_settings_load(&saved_prog, saved_vol)) {
            bs300_restore_settings(saved_prog, saved_vol);
        }
    }

    /* Step 6: MUTE → sync active program → ACTIVE */
    PRINTF("[BS300] syncing active program to RAM...\r\n");
    bs300_mute();

    {
        bs300_prog_struct_t prog;
        bs300_storage_load_program(bs300_get_active_prog(), s_raw);
        if (bs300_flash_to_struct(s_raw, &prog) == 0) {
            prog.modules.volume_level = bs300_get_module_volume(bs300_get_active_prog());
            bs300_sync_program(&prog);
        }
    }

    bs300_active();
    PRINTF("[BS300] init complete, DSP active\r\n");

    s_initialized = true;
    return true;
}

/* ============================================================
 *  Accessors
 * ============================================================ */

const bs300_program_data_t *bs300_driver_get_program(uint8_t prog_idx)
{
    static bs300_program_data_t prog;

    if (!s_initialized || prog_idx > 3) return NULL;

    bs300_storage_load_program(prog_idx, s_raw);
    if (!bs300_program_parse(s_raw, &prog)) return NULL;
    return &prog;
}

const uint8_t *bs300_driver_get_calibration(void)
{
    return NULL;  /* deprecated — use bs300_driver_get_calib() */
}

bool bs300_driver_refresh(void)
{
    s_initialized = false;
    /* Full re-read from chip */
    if (!bs300_startup()) return false;
    if (!read_and_save_all()) return false;
    bs300_cache_prog_inputs();

    {
        uint8_t saved_prog = 0;
        uint8_t saved_vol[4];
        if (bs300_settings_load(&saved_prog, saved_vol)) {
            bs300_restore_settings(saved_prog, saved_vol);
        }
    }

    bs300_mute();
    bs300_prog_struct_t prog;
    bs300_storage_load_program(bs300_get_active_prog(), s_raw);
    if (bs300_flash_to_struct(s_raw, &prog) == 0) {
        prog.modules.volume_level = bs300_get_module_volume(bs300_get_active_prog());
        bs300_sync_program(&prog);
    }
    bs300_active();

    s_initialized = true;
    return true;
}

const bs300_prog_struct_t *bs300_driver_get_struct(uint8_t prog_idx)
{
    static bs300_prog_struct_t buf;

    if (!s_initialized || prog_idx > 3) return NULL;

    bs300_storage_load_program(prog_idx, s_raw);
    if (bs300_flash_to_struct(s_raw, &buf) != 0) return NULL;
    return &buf;
}

const bs300_calib_t *bs300_driver_get_calib(void)
{
    static bs300_calib_t calib;
    static bool loaded = false;

    if (loaded) return &calib;

    if (!bs300_read_calibration(s_raw)) return NULL;
    if (bs300_parse_calibration(s_raw, &calib) != 0) return NULL;
    loaded = true;
    return &calib;
}

bool bs300_driver_is_cached(void)
{
    return bs300_is_boot_cached();
}
