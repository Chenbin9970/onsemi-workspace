#ifndef BLE_REMPRO_CMD_H
#define BLE_REMPRO_CMD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HDLC protocol constants */
#define HDLC_SEPARATOR          0x7E
#define HDLC_SYS_ID             0x00

/* Command IDs — App → Device */
#define CMD_SETVOLUME           2
#define CMD_SETDEVICEONOFF      3
#define CMD_GETBATTERYINFO      4
#define CMD_SETFEEDBACKONOFF    5
#define CMD_SETGAIN             6
#define CMD_SETMPO              7
#define CMD_SETCOMPRESSRATIO    8
#define CMD_SETDENOISE          9
#define CMD_SETEQUALIZER        10
#define CMD_GETCURRENTSCENE     15
#define CMD_SETCURRENTSCENE     16
#define CMD_GETDEVICECONFIG     26
#define CMD_GETFEEDBACKONOFF    34

/* Command IDs — Device → App (active push, SYS_ID=1) */
#define CMD_PUSH_VOLUME         4
#define CMD_PUSH_SCENE          5

/* HDLC SYS_ID for device-initiated push */
#define HDLC_SYS_ID_DEVICE      1

void rempro_cmd_process(void);

/* Called from GATT callback — appends raw BLE chunk directly to
 * reassembly buffer.  Avoids the single-slot role_value race condition
 * when the App sends multi-chunk frames back-to-back. */
void rempro_reasm_append(const uint8_t *data, uint8_t len);
void rempro_reasm_reset(void);

/* Active push: notify app of state changes triggered by button */
void rempro_push_scene_change(uint8_t scene_id);
void rempro_push_volume_change(uint8_t prog, uint8_t volume);

#ifdef __cplusplus
}
#endif

#endif
