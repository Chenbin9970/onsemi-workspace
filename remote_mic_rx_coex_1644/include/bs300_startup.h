#ifndef BS300_STARTUP_H
#define BS300_STARTUP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Protocol Building Blocks
 * ============================================================ */

/* Send a Simple Command and poll FURPROC until ready.
 * Frame: 5 bytes (Addr + Len(0x00) + Cmd[3] + Chk)
 * After send: wait 60ms, Read Request(0x80), poll bit23=0 */
bool bs300_send_simple_cmd(uint32_t cmd_word);

/* Send an Advanced Write command with 48-byte data payload.
 * Frame: 53 bytes (Addr + Len(0x10) + Cmd[3] + Data[48] + Chk)
 * After send: wait 60ms, poll FURPROC */
bool bs300_advanced_write(uint32_t cmd_word, const uint8_t *data);

/* Generic read_packet: Prepare → poll → Read Request(0x90) → read 52B → extract 48B.
 * 1. Send prepare_cmd as Simple Command
 * 2. Wait 60ms, poll FURPROC until ready
 * 3. Wait 60ms after ready
 * 4. Read Request(0x90) → I2C Read 52B → extract Cmd[3] + Data[48] + Chk
 * Returns 48 bytes of payload in data_out. */
bool bs300_read_packet(uint32_t prepare_cmd, uint8_t *data_out);

/* ============================================================
 * High-level Sequences
 * ============================================================ */

/* Phase 1: Unlock chip communication.
 *   MUTE(0x800000) → delay 2ms →
 *   KEY_LOCK(0x801020) → delay 2ms →
 *   VERIFY_COMM(0x800030) with security_code */
bool bs300_startup(void);

/* Phase 2: Read calibration data (3 packets, 144 bytes).
 *   Pkt0: 0x800051, Pkt1: 0x801051, Pkt2: 0x802051.
 *   Returns true if all 3 packets read successfully. */
bool bs300_read_calibration(uint8_t *calib_out);

/* Phase 2: Read Global Profile (1 packet, 48 bytes).
 *   Command: 0x800071. */
bool bs300_read_global_profile(uint8_t *profile_out);

#ifdef __cplusplus
}
#endif

#endif /* BS300_STARTUP_H */
