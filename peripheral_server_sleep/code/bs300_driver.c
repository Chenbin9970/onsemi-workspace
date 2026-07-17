/**
 * BS300 Driver — full init flow with single DSP state.
 *
 * bs300_driver_init():
 *   1. I2C init + 2s DSP power stabilization
 *   2. Unlock chip (MUTE → KEY_LOCK → VERIFY_COMM)
 *   3. First boot: read 4 programs from BS300 Flash → save to Main Flash
 *      Cached boot: skip flash read
 *   4. Restore settings (active program + volume) if available
 *   5. MUTE → sync active program to BS300 RAM → ACTIVE
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

/* ============================================================
 *  Internal: read programs from BS300 Flash, save to Main Flash
 * ============================================================ */
static bool read_and_save_all(void)
{
    uint8_t i;
    for (i = 0; i < 4; i++) {
        if (!bs300_program_read(i, bs300_work_buf)) {
            PRINTF("[BS300] program %u read FAIL\r\n", i);
            return false;
        }
        if (!bs300_storage_write_program(i, bs300_work_buf)) {
            PRINTF("[BS300] program %u save FAIL\r\n", i);
            return false;
        }
        bs300_delay_ms(5);
        Sys_Watchdog_Refresh();
    }
    PRINTF("[BS300] 4 programs read + saved\r\n");

    /* Init feedback on/off from chip data (only runs when reading from BS300) */
    bs300_init_feedback_from_flash();

    return true;
}

static bool all_programs_cached(void)
{
    uint8_t i;
    for (i = 0; i < 4; i++) {
        if (!bs300_storage_is_valid(i)) return false;
    }
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

    /* Step 2: Unlock chip */
    if (!bs300_startup()) {
        PRINTF("[BS300] startup FAIL\r\n");
        return false;
    }
    PRINTF("[BS300] startup OK\r\n");

    /* Step 3: First boot reads programs from chip; cached boot skips */
    if (all_programs_cached()) {
        PRINTF("[BS300] Main Flash cache valid, skip chip read\r\n");
    } else {
        PRINTF("[BS300] reading all programs from chip...\r\n");
        if (!read_and_save_all()) return false;
    }

    /* Step 4: Restore settings (active program + volume / EQ / denoise / feedback) */
    {
        uint8_t saved_prog = 0;
        uint8_t saved_vol[4] = {9, 9, 9, 9};
        int8_t  saved_eq_low[4] = {0};
        int8_t  saved_eq_mid[4] = {0};
        int8_t  saved_eq_high[4] = {0};
        uint8_t saved_denoise[4] = {0};
        uint8_t saved_fbonoff[4] = {0};
        const uint8_t *fb_ptr = NULL;
        if (bs300_settings_load(&saved_prog, saved_vol,
                                 saved_eq_low, saved_eq_mid, saved_eq_high,
                                 saved_denoise, saved_fbonoff)) {
            /* Only use settings feedback if any program was explicitly set */
            if (saved_fbonoff[0] | saved_fbonoff[1] |
                saved_fbonoff[2] | saved_fbonoff[3])
                fb_ptr = saved_fbonoff;
            PRINTF("[BS300] settings loaded from flash\r\n");
        } else {
            PRINTF("[BS300] settings invalid, using defaults\r\n");
        }
        bs300_restore_settings(saved_prog, saved_vol,
                               saved_eq_low, saved_eq_mid, saved_eq_high,
                               saved_denoise, fb_ptr);

        /* Fallback: if settings didn't have feedback, init from program flash */
        if (fb_ptr == NULL)
            bs300_init_feedback_from_flash();
    }

    /* Step 5: Boot cache — load active program into s_dsp_state + calibration */
    bs300_cache_boot_state();

    /* Step 6: MUTE → sync active program → ACTIVE */
    PRINTF("[BS300] syncing active program to RAM...\r\n");
    bs300_mute();
    bs300_sync_program(bs300_get_dsp_state());
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

    bs300_storage_load_program(prog_idx, bs300_work_buf);
    if (!bs300_program_parse(bs300_work_buf, &prog)) return NULL;
    return &prog;
}

const uint8_t *bs300_driver_get_calibration(void)
{
    return NULL;  /* deprecated — use bs300_driver_get_calib() */
}

bool bs300_driver_refresh(void)
{
    s_initialized = false;
    if (!bs300_startup()) return false;
    if (!read_and_save_all()) return false;

    {
        uint8_t saved_prog = 0;
        uint8_t saved_vol[4] = {0};
        int8_t  saved_eq_low[4] = {0};
        int8_t  saved_eq_mid[4] = {0};
        int8_t  saved_eq_high[4] = {0};
        uint8_t saved_denoise[4] = {0};
        uint8_t saved_fbonoff[4] = {0};
        const uint8_t *fb_ptr = NULL;
        if (bs300_settings_load(&saved_prog, saved_vol,
                                 saved_eq_low, saved_eq_mid, saved_eq_high,
                                 saved_denoise, saved_fbonoff)) {
            if (saved_fbonoff[0] | saved_fbonoff[1] |
                saved_fbonoff[2] | saved_fbonoff[3])
                fb_ptr = saved_fbonoff;
            bs300_restore_settings(saved_prog, saved_vol,
                                   saved_eq_low, saved_eq_mid, saved_eq_high,
                                   saved_denoise, fb_ptr);
        }
        if (fb_ptr == NULL)
            bs300_init_feedback_from_flash();
    }

    bs300_cache_boot_state();

    bs300_mute();
    bs300_sync_program(bs300_get_dsp_state());
    bs300_active();

    s_initialized = true;
    return true;
}

const bs300_prog_struct_t *bs300_driver_get_struct(uint8_t prog_idx)
{
    static bs300_prog_struct_t buf;

    if (!s_initialized || prog_idx > 3) return NULL;

    bs300_storage_load_program(prog_idx, bs300_work_buf);
    if (bs300_flash_to_struct(bs300_work_buf, &buf) != 0) return NULL;
    return &buf;
}

const bs300_calib_t *bs300_driver_get_calib(void)
{
    return bs300_get_cached_calib();
}

bool bs300_driver_is_cached(void)
{
    return bs300_is_boot_cached();
}
