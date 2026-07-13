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

/* Command IDs */
#define CMD_SETVOLUME           2
#define CMD_SETDEVICEONOFF      3
#define CMD_GETBATTERYINFO      4
#define CMD_GETCURRENTSCENE     15
#define CMD_SETCURRENTSCENE     16
#define CMD_GETDEVICECONFIG     26

void rempro_cmd_process(void);

#ifdef __cplusplus
}
#endif

#endif
