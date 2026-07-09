#ifndef BS300_APP_COMMON_DEVICE_BS300_BS300_DRIVER_H
#define BS300_APP_COMMON_DEVICE_BS300_BS300_DRIVER_H

#include "asm/iic_soft.h"
#include "bs300_param.h"

/* Debug control */
#define BS300_DEBUG_ENABLE  1

#if BS300_DEBUG_ENABLE
#define bs300_debug(format, ...)    printf("[BS300] " format, ## __VA_ARGS__)
#define bs300_put_buf               put_buf
#define bs300_i2c_log(dir, buf, len) do { \
    printf("[BS300-I2C] %s(%d): ", dir, len); \
    put_buf(buf, len); \
} while(0)
#else
#define bs300_debug(...)
#define bs300_put_buf(...)
#define bs300_i2c_log(dir, buf, len)
#endif

/* I2C slave address (7-bit: 0b0000001, write: 0x02, read: 0x03) */
#define BS300_I2C_ADDR              0x02

/* Packet sizes */
#define BS300_DATA_SIZE             48
#define BS300_PROGRAM_PKTS          10
#define BS300_PROGRAM_SIZE          (BS300_PROGRAM_PKTS * BS300_DATA_SIZE)

/* VM segment storage: 2 segments × 240B per program, 4 programs = 8 IDs */
#define BS300_VM_ID_BASE            20
#define BS300_VM_SEG_SIZE           250
#define BS300_VM_SEG_COUNT          2
#define BS300_VM_DATA_SIZE          (BS300_VM_SEG_SIZE * BS300_VM_SEG_COUNT)

/* Timing (ms) */
#define BS300_CMD_DELAY_MS          60
#define BS300_POLL_TIMEOUT_MS       1000
#define BS300_POLL_RETRY_MAX        (BS300_POLL_TIMEOUT_MS / BS300_CMD_DELAY_MS)

/* Frame length flag */
#define BS300_LEN_NO_DATA           0x00
#define BS300_LEN_HAS_DATA          0x10
#define BS300_LEN_RD_REQ_FLAG       0x80

/* Program count */
#define BS300_PROG_COUNT            4

/* Basic control commands */
#define BS300_CMD_MUTE              0x800000
#define BS300_CMD_ACTIVE            0x800010
#define BS300_CMD_UNLOCK            0x800020
#define BS300_CMD_KEY_LOCK          0x801020
#define BS300_CMD_IS_CONNECT        0x800050

/* Initialization commands */
#define BS300_CMD_VERIFY_COMM       0x800030
#define BS300_CMD_GLOBAL_PROFILE    0x800071
#define BS300_DEFAULT_SECURITY_CODE 0x012958

/* Calibration */
#define BS300_CMD_CALIB_READ_BASE   0x800051
#define BS300_CALIB_PKT_COUNT       3
#define BS300_CALIB_SIZE            (BS300_CALIB_PKT_COUNT * BS300_DATA_SIZE)
#define BS300_VM_ID_CALIB           28
#define BS300_VM_ID_CALIB_CHK       29
#define BS300_CALIB_CHECKSUM        130180UL
#define BS300_VM_ID_PROG_CHK_BASE   30

/* Program Burn base commands (Y=0, prog_idx added via Y << 12) */
#define BS300_CMD_READ_START_BASE   0x800031
#define BS300_CMD_BURN_END_BASE     0x800021
#define BS300_CMD_PROG_READ_BASE    0x800011
#define BS300_CMD_PROG_WRITE_BASE   0x800001

/* Command section bit masks */
#define BS300_CMD_FURPROC_BIT       23
#define BS300_CMD_PKTNUM_SHIFT      12
#define BS300_CMD_PKTNUM_MASK       0xF

/**
 * @brief One-time BS300 startup sequence.
 * Must be called once at boot before any bs300_init() calls.
 * Sends: Mute → Unlock → Verify Comm Code → reads calibration → saves to VM.
 *
 * @param iic            Software I2C device index
 * @param security_code  24-bit security code (0 = default)
 * @return 0 on success, negative on error
 */
int bs300_startup(soft_iic_dev iic, u32 security_code);

/**
 * @brief Initialize BS300 driver for a program.
 * Sends Global Profile, loads program from VM (or reads from BS300),
 * then syncs all params to BS300 RAM.
 * bs300_startup() must be called first.
 *
 * @param iic       Software I2C device index
 * @param prog_idx  Program index (0-3)
 * @return 0 on success, negative on error
 */
int bs300_init(soft_iic_dev iic, u8 prog_idx);

/**
 * @brief Sync all parameters from buffer to BS300 RAM.
 *
 * @param iic  Software I2C device index
 * @param buf  672-byte program buffer
 * @return 0 on success, negative on error
 */
int bs300_sync_all(soft_iic_dev iic, const u8 *buf);

/**
 * @brief Read program data from BS300 Flash into buffer.
 *
 * @param iic       Software I2C device index
 * @param prog_idx  Program index (0-3)
 * @param buf       Output buffer (BS300_PROGRAM_SIZE bytes)
 * @return 0 on success, negative on error
 */
int bs300_program_read(soft_iic_dev iic, u8 prog_idx, u8 *buf);

/**
 * @brief Write buffer to BS300 Flash via Program Burn.
 *
 * @param iic       Software I2C device index
 * @param prog_idx  Program index (0-3)
 * @param buf       672-byte program buffer
 * @return 0 on success, negative on error
 */
int bs300_program_write(soft_iic_dev iic, u8 prog_idx, const u8 *buf);

/**
 * @brief Modify bytes in a program, sync affected module to BS300, save VM.
 *
 * @param iic       Software I2C device index
 * @param prog_idx  Program index (0-3)
 * @param offset    Byte offset in program buffer
 * @param val       New value(s)
 * @param len       Number of bytes to write
 * @return 0 on success, negative on error
 */
int bs300_param_modify(soft_iic_dev iic, u8 prog_idx, u16 offset,
                       const u8 *val, u8 len);

/**
 * @brief Write a single 48-byte data packet via Param I2C Advanced Write.
 *
 * @param iic   Software I2C device index
 * @param cmd   24-bit command word
 * @param data  48-byte data buffer
 * @return 0 on success, negative on error
 */
int bs300_param_write_packet(soft_iic_dev iic, u32 cmd, const u8 *data);

/**
 * @brief Set volume level (0-5), re-encode Bin Gain, send 1 I2C command.
 *
 * @param iic    Software I2C device index
 * @param level  Volume level (0-5), each step +5dB
 * @return 0 on success, negative on error
 */
int bs300_set_volume(soft_iic_dev iic, u8 level);

/**
 * @brief Reload active program from VM and resync all I2C RAM commands.
 * Used after VM modifications to the active program (volume/EQ/ENR/etc).
 */
int bs300_resync_active(soft_iic_dev iic);

/**
 * @brief Set 3-band EQ, re-encode Bin Gain, send 1 I2C command.
 *
 * @param iic        Software I2C device index
 * @param low_gain   Low freq gain (<500Hz), [-12, 12] dB
 * @param mid_gain   Mid freq gain (500-2000Hz), [-12, 12] dB
 * @param high_gain  High freq gain (>2000Hz), [-12, 12] dB
 * @return 0 on success, negative on error
 */
int bs300_set_eq(soft_iic_dev iic, s8 low_gain, s8 mid_gain, s8 high_gain);

/* Async variants — VM ops sync, I2C op async via session tick */
int bs300_reencode_bin_gain_async(void (*on_done)(void));
int bs300_set_volume_async(u8 level, void (*on_done)(void));
int bs300_set_eq_async(s8 low, s8 mid, s8 high, void (*on_done)(void));

/**
 * @brief Switch input for voice prompt playback (does NOT trigger igd chain).
 *        Encodes Volume/Beep with target_input, sends via I2C.
 *
 * @param iic           Software I2C device index
 * @param target_input  Target input selection (0=FrontMic, 2=Telecoil, etc.)
 * @return original input_selection on success, 0xFF on error
 */
u8 bs300_voice_prompt_input_switch(soft_iic_dev iic, u8 target_input);

/**
 * @brief Restore input after voice prompt playback.
 *
 * @param iic             Software I2C device index
 * @param original_input  Original input_selection to restore
 * @return 0 on success, negative on error
 */
int bs300_voice_prompt_input_restore(soft_iic_dev iic, u8 original_input);

/* ================================================================
 *  VM segment storage (implemented in bs300_vm.c)
 * ================================================================ */

/**
 * @brief Load one VM segment for a program.
 *
 * @param prog_idx  Program index (0-3)
 * @param seg_idx   Segment index (0-1)
 * @param buf       Output buffer (BS300_VM_SEG_SIZE bytes)
 * @param len       Buffer length (must equal BS300_VM_SEG_SIZE)
 * @return 0 on success, negative on error
 */
int bs300_vm_load_seg(u8 prog_idx, u8 seg_idx, u8 *buf, u16 len);

/**
 * @brief Save one VM segment for a program.
 *
 * @param prog_idx  Program index (0-3)
 * @param seg_idx   Segment index (0-1)
 * @param buf       Data buffer (BS300_VM_SEG_SIZE bytes)
 * @param len       Data length (must equal BS300_VM_SEG_SIZE)
 * @return 0 on success, negative on error
 */
int bs300_vm_save_seg(u8 prog_idx, u8 seg_idx, const u8 *buf, u16 len);

/**
 * @brief Load full program from VM segments into buffer.
 * Zero-fills the unstored padding area (bytes 540-671).
 *
 * @param prog_idx  Program index (0-3)
 * @param buf       Output buffer (BS300_PROGRAM_SIZE bytes)
 * @return 0 on success, negative on error
 */
int bs300_program_load(u8 prog_idx, u8 *buf);

/**
 * @brief Save full program buffer to VM segments.
 * Only stores bytes 0-539; padding is not persisted.
 *
 * @param prog_idx  Program index (0-3)
 * @param buf       672-byte program buffer
 * @return 0 on success, negative on error
 */
int bs300_program_save(u8 prog_idx, const u8 *buf);

/* ================================================================
 *  Calibration storage (implemented in bs300_vm.c)
 * ================================================================ */

/**
 * @brief Load calibration data from VM.
 *
 * @param buf  Output buffer (BS300_CALIB_SIZE = 144 bytes)
 * @return 0 on success, negative on error
 */
int bs300_calib_vm_load(u8 *buf);

/**
 * @brief Save calibration data to VM.
 *
 * @param buf  Calibration data (BS300_CALIB_SIZE = 144 bytes)
 * @return 0 on success, negative on error
 */
int bs300_calib_vm_save(const u8 *buf);

/**
 * @brief Compute 32-bit checksum of buffer (24-bit word sum, little-endian).
 *
 * @param buf  Data buffer
 * @param len  Buffer length in bytes
 * @return 32-bit checksum
 */
u32 bs300_checksum32(const u8 *buf, u16 len);

/**
 * @brief Check if BS300 is connected (responds to IsConnect command).
 *
 * @param iic  Software I2C device index
 * @return 0 if connected, negative if not responding
 */
int bs300_is_connected(soft_iic_dev iic);

/* ================================================================
 *  Non-blocking sync session (state machine per command)
 * ================================================================ */

#define BS300_SYNC_MAX_CMDS  64

typedef enum {
    BS300_SYNC_IDLE = 0,
    BS300_SYNC_SEND,
    BS300_SYNC_POLL,
    BS300_SYNC_DONE,
    BS300_SYNC_ERROR
} bs300_sync_state_t;

typedef struct {
    u32 cmds[BS300_SYNC_MAX_CMDS];
    u8  datas[BS300_SYNC_MAX_CMDS][48];
    u8  cmd_count;
    u8  cmd_index;
    bs300_sync_state_t state;
    u8  retry_count;
    u8  fail_count;
    u32 last_action_ms;
    u8   iic;   /* stored as u8 because soft_iic_dev is const-qualified */
} bs300_sync_session_t;

/**
 * @brief Initialize a sync session.
 * @param s     Session struct
 * @param iic   Software I2C device index
 */
void bs300_sync_session_init(bs300_sync_session_t *s, soft_iic_dev iic);

/**
 * @brief Append a command to the session's send queue.
 *        Called by fill-mode functions to build the command list.
 * @param s     Session struct
 * @param cmd   24-bit command word
 * @param data  48-byte data payload
 * @return 0 on success, -1 if queue is full
 */
int bs300_session_append(bs300_sync_session_t *s, u32 cmd, const u8 *data);

/**
 * @brief Non-blocking tick: process one step of the sync state machine.
 *        Call repeatedly (e.g. from sys_timeout_add callback) until
 *        it returns 0 (BS300_SYNC_DONE or BS300_SYNC_ERROR).
 * @param s  Session struct
 * @return 1 = still busy (call again later), 0 = done/error
 */
int bs300_sync_tick(bs300_sync_session_t *s);

/**
 * @brief Fill session with all Param I2C commands for a full sync.
 *        After calling, use bs300_sync_tick() repeatedly to send them.
 * @param s     Session struct (must be initialized)
 * @param prog  Program struct to sync
 * @return 0 on success, negative on error
 */
int bs300_sync_program_start(bs300_sync_session_t *s, bs300_prog_struct_t *prog);

/**
 * @brief Fill session with only the changed commands for a program switch.
 *        After calling, use bs300_sync_tick() repeatedly to send them.
 * @param s             Session struct (must be initialized)
 * @param new_prog_idx  Target program index (0-3)
 * @return 0 on success, negative on error
 */
int bs300_switch_program_start(bs300_sync_session_t *s, u8 new_prog_idx);

/**
 * @brief Diff-sync modified active program to RAM (only changed I2C commands).
 * @param iic  soft_iic index
 * @param _new Caller's modified program buffer (compared against VM copy)
 * @return 0 on success, negative on error
 */
int bs300_resync_diff(soft_iic_dev iic, bs300_prog_struct_t *_new);

/**
 * @brief Non-blocking version of bs300_resync_diff: fill session with diff
 *        commands and set up async tick.  @p _new must remain valid until
 *        tick finishes (BS300_SYNC_DONE / BS300_SYNC_ERROR).
 * @param s     Session struct (must be initialized)
 * @param _new  Caller's modified program buffer (compared against VM copy)
 * @return 0 on success, negative on error
 */
int bs300_resync_diff_start(bs300_sync_session_t *s, bs300_prog_struct_t *_new);

/**
 * @brief Fully async diff-sync of modified active program.  Same semantics
 *        as bs300_resync_diff(0, _new) but returns immediately; I2C commands
 *        are sent via the tick callback.  The caller's buffer must stay
 *        valid until the sync completes.
 * @param _new     Caller's modified program buffer
 * @param on_done  Optional callback invoked when tick finishes (may be NULL)
 * @return 0 on success, -1 if sync is busy
 */
int bs300_resync_diff_async(bs300_prog_struct_t *_new, void (*on_done)(void));

/**
 * @brief Fill session with commands affected by a single parameter change.
 * @param s         Session struct (must be initialized)
 * @param prog_idx  Program index (0-3)
 * @param offset    Byte offset in program struct
 * @param val       New value(s)
 * @param len       Number of bytes to write
 * @return 0 on success, negative on error
 */
int bs300_param_modify_start(bs300_sync_session_t *s, u8 prog_idx,
                              u16 offset, const u8 *val, u8 len);

/** @brief Get cached input_selection for a program (populated at boot). */
u8  bs300_get_prog_input(u8 prog_idx);
/** @brief Cache all 4 programs' data from VM. Call once at boot. */
void bs300_cache_prog_inputs(void);
/** @brief Called after program switch to update active-prog cache. */
void bs300_on_active_prog_changed(u8 new_prog_idx);
/** @brief Returns 1 if an async sync/switch is in progress. */
int  bs300_sync_is_busy(void);
/** @brief Mark VM dirty — triggers async diff-sync to DSP if idle. */
void bs300_sync_dirty(void);
/** @brief Volume change: save VM → sync_dirty. Safe for any caller. */
int  bs300_vol_commit(u8 level);

/**
 * @brief Async (non-blocking) program switch.
 *        Starts a background sync via sys_timeout_add, returns immediately.
 * @param new_prog_idx  Target program index (0-3)
 * @return 0 on success, -1 if busy or error
 */
int bs300_switch_program_async(u8 new_prog_idx);

/**
 * @brief Async (non-blocking) single parameter modify.
 * @param prog_idx  Program index (0-3)
 * @param offset    Byte offset in program struct
 * @param val       New value(s)
 * @param len       Number of bytes to write
 * @return 0 on success, -1 if busy or error
 */
int bs300_param_modify_async(u8 prog_idx, u16 offset, const u8 *val, u8 len);

/**
 * @brief Send MUTE command to stop DSP audio processing.
 *        Uses delay=200 (4x slow) for first communication stability.
 * @param iic  Software I2C device index
 * @return 0 on success, negative on error
 */
int bs300_mute(soft_iic_dev iic);

int bs300_key_lock(soft_iic_dev iic);
int bs300_verify_comm(soft_iic_dev iic, u32 security_code);
int bs300_unlock(soft_iic_dev iic);

/**
 * @brief Send ACTIVE command to start DSP audio processing.
 *        Must be called after all programs are loaded and synced.
 * @param iic  Software I2C device index
 * @return 0 on success, negative on error
 */
int bs300_active(soft_iic_dev iic);

#endif /* BS300_APP_COMMON_DEVICE_BS300_BS300_DRIVER_H */
