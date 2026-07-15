#ifndef BS300_RAM_SYNC_H
#define BS300_RAM_SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include "bs300_param_encode.h"
#include "bs300_calib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Timer message ID — must be added to app.h message enum. */
#ifndef BS300_SYNC_TIMER
#define BS300_SYNC_TIMER  0x10
#endif

/* ================================================================
 *  I2C command words
 * ================================================================ */
#define BS300_CMD_MUTE              0x800000
#define BS300_CMD_ACTIVE            0x800010
#define BS300_CMD_UNLOCK            0x800020
#define BS300_CMD_KEY_LOCK          0x801020
#define BS300_CMD_IS_CONNECT        0x800050

#define BS300_CMD_DDM2              0x800022
#define BS300_CMD_MM_PLUS           0x800062
#define BS300_CMD_DFBC              0x800052
#define BS300_CMD_ENR_GENERAL       0x8000C2
#define BS300_CMD_NOISE_GEN2        0x800172
#define BS300_CMD_TC_DAI            0x804272
#define BS300_CMD_ISS               0x8001B2
#define BS300_CMD_WNR_SETUP         0x8001C2
#define BS300_CMD_WNR_BAND_0_15     0x8011C2
#define BS300_CMD_WNR_BAND_16_31    0x8411C2
#define BS300_CMD_WNR_SINGLE_MIC    0x8021C2
#define BS300_CMD_AGCO              0x800382
#define BS300_CMD_VOL_BEEP          0x800081
#define BS300_CMD_WDRC_BASE         0x8000B2
#define BS300_CMD_ITG               0x8001E2

/* ================================================================
 *  Sync session (non-blocking tick-driven state machine)
 * ================================================================ */
#define BS300_SYNC_MAX_CMDS         32

typedef enum {
    BS300_SYNC_IDLE = 0,
    BS300_SYNC_SEND,
    BS300_SYNC_POLL,
    BS300_SYNC_DONE,
    BS300_SYNC_ERROR
} bs300_sync_state_t;

typedef struct {
    uint32_t cmds[BS300_SYNC_MAX_CMDS];
    uint8_t  datas[BS300_SYNC_MAX_CMDS][48];
    uint8_t  cmd_count;
    uint8_t  cmd_index;
    bs300_sync_state_t state;
    uint8_t  retry_count;
    uint8_t  fail_count;
    bool     abort_requested;           /* external abort signal */
    bs300_prog_struct_t *dsp_state;     /* → s_dsp_state, updated per-command */
    bs300_prog_struct_t *target;        /* → s_target, source for apply */
} bs300_sync_session_t;

/* ================================================================
 *  Session management
 * ================================================================ */
void bs300_sync_session_init(bs300_sync_session_t *s);
int  bs300_session_append(bs300_sync_session_t *s, uint32_t cmd, const uint8_t *data);
int  bs300_sync_tick(bs300_sync_session_t *s);

/* ================================================================
 *  Fill-mode functions (build command queue, caller ticks)
 * ================================================================ */
int bs300_sync_program_start(bs300_sync_session_t *s, bs300_prog_struct_t *prog);
int bs300_switch_program_start(bs300_sync_session_t *s, uint8_t new_prog_idx);
int bs300_resync_diff_start(bs300_sync_session_t *s, bs300_prog_struct_t *_new);
int bs300_param_modify_start(bs300_sync_session_t *s, uint8_t prog_idx,
                              uint16_t offset, const uint8_t *val, uint8_t len);

/* ================================================================
 *  Blocking API (simple wrappers)
 * ================================================================ */
int bs300_sync_program(bs300_prog_struct_t *prog);
int bs300_switch_program(uint8_t new_prog_idx);
int bs300_resync_diff(bs300_prog_struct_t *_new);
int bs300_param_modify(uint8_t prog_idx, uint16_t offset,
                       const uint8_t *val, uint8_t len);

/* Timer callback — call from APP_Timer when msg_id == BS300_SYNC_TIMER */
void bs300_sync_timer_handler(void);

/* Process deferred switch/volume after session completes.
 * Call from Main_Loop (NOT from timer handler). */
void bs300_process_deferred(void);

/* Persist current program + volume to flash.
 * Safe only when BLE is idle or disconnected. */
void bs300_settings_persist(void);

/* ================================================================
 *  Async API (non-blocking via ke_timer)
 * ================================================================ */
int  bs300_sync_is_busy(void);
int  bs300_switch_program_async(uint8_t new_prog_idx, void (*on_done)(void));
int  bs300_resync_diff_async(bs300_prog_struct_t *_new, void (*on_done)(void));
int  bs300_param_modify_async(uint8_t prog_idx, uint16_t offset,
                               const uint8_t *val, uint8_t len);

/* ================================================================
 *  Volume / EQ control
 * ================================================================ */
int bs300_set_volume_async(uint8_t level, void (*on_done)(void));
int bs300_set_volume_notone_async(uint8_t level, void (*on_done)(void));
void bs300_async_done_callback(void);
int bs300_set_eq_async(int8_t low, int8_t mid, int8_t high,
                        void (*on_done)(void));
void bs300_sync_dirty(void);

/* ================================================================
 *  Voice prompt input switch (uses s_dsp_state directly)
 * ================================================================ */
uint8_t bs300_voice_prompt_input_switch(uint8_t target_input);
int  bs300_voice_prompt_input_restore(uint8_t original_input);

/* ================================================================
 *  Input Tone Generator (0x8001E2)
 * ================================================================ */
int bs300_itg_write(uint8_t level_db, uint16_t freq_hz,
                    const bs300_calib_t *calib);
int bs300_itg_clear(void);

/* ================================================================
 *  Pure-tone Audiometry
 * ================================================================ */
int bs300_audiometry_enter(void);
int bs300_audiometry_exit(void);

/* ================================================================
 *  Basic commands
 * ================================================================ */
int bs300_mute(void);
int bs300_active(void);
int bs300_is_connected(void);

/* ================================================================
 *  Prompt Tone
 * ================================================================ */
void bs300_play_prompt_tone(uint8_t program, uint8_t volume);

/* ================================================================
 *  DSP state query (single source of truth)
 * ================================================================ */
uint8_t bs300_get_active_prog(void);
uint8_t bs300_get_module_volume(uint8_t prog_idx);
void bs300_set_prog_volume(uint8_t prog_idx, uint8_t level);
void bs300_set_prog_denoise(uint8_t prog_idx, uint8_t level);
uint8_t bs300_get_prog_denoise(uint8_t prog_idx);
void bs300_print_settings(void);
void bs300_persist_active_prog(uint8_t prog);
bool    bs300_is_boot_cached(void);
const bs300_calib_t *bs300_get_cached_calib(void);

/* ================================================================
 *  Boot-time init — load active program into s_dsp_state
 * ================================================================ */
void bs300_cache_boot_state(void);
void bs300_restore_settings(uint8_t active_prog, const uint8_t *volume,
                            const int8_t *eq_low, const int8_t *eq_mid,
                            const int8_t *eq_high, const uint8_t *denoise);
void bs300_reset_user_params(uint8_t prog_idx);

/* Direct access to current DSP state (490B .bss) */
bs300_prog_struct_t *bs300_get_dsp_state(void);

/* ================================================================
 *  Shared work buffer (480B, used by driver and sync for Flash I/O)
 * ================================================================ */
extern uint8_t bs300_work_buf[480];

#ifdef __cplusplus
}
#endif

#endif /* BS300_RAM_SYNC_H */
