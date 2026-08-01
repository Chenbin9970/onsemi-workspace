/* ----------------------------------------------------------------------------
 * Copyright (c) 2018 Semiconductor Components Industries, LLC (d/b/a
 * ON Semiconductor), All Rights Reserved
 *
 * This code is the property of ON Semiconductor and may not be redistributed
 * in any form without prior written permission from ON Semiconductor.
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 *
 * This is Reusable Code.
 *
 * ----------------------------------------------------------------------------
 * ble_asha.h
 * - Android Audio Streaming Hearing Aid service header file
 * ------------------------------------------------------------------------- */

#ifndef BLE_ASHA_H
#define BLE_ASHA_H

/* ----------------------------------------------------------------------------
 * If building with a C++ compiler, make all of the definitions in this header
 * have a C binding.
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
extern "C"
{
#endif    /* ifdef __cplusplus */

/* ----------------------------------------------------------------------------
 * Include files
 * --------------------------------------------------------------------------*/
#include <rsl10_ke.h>

#include <l2cc_task.h>

/* ----------------------------------------------------------------------------
 * Defines
 * --------------------------------------------------------------------------*/
/** ASHA [service UUIDs][https://source.android.com/devices/bluetooth/asha#asha-gatt-services] */
#define ASHA_SERVICE_UUID                    {0xf0,0xfd}
#define ASHA_READONLYPROPERTIES_CHAR_UUID    {0xbb,0x37,0xad,0x2a,0x90,0x7c,0x69,0x91,0x3e,0x4a,0x81,0xc4,0x1e,0x65,0x33,0x63}
#define ASHA_AUDIOCONTROLPOINT_CHAR_UUID     {0xc0,0x6c,0x99,0xb0,0x37,0x19,0x9f,0x9d,0x6c,0x47,0x88,0x4a,0x7e,0xde,0xd4,0xf0}
#define ASHA_AUDIOSTATUSPOINT_CHAR_UUID      {0x37,0x48,0x40,0x56,0x6b,0x32,0x41,0xb6,0xac,0x4c,0x11,0xe7,0x1a,0x3f,0x66,0x38}
#define ASHA_VOLUME_CHAR_UUID                {0xdf,0x91,0x7e,0x0c,0xe7,0xf9,0x23,0x88,0xe4,0x41,0x14,0xab,0x9e,0xca,0xe4,0x00}
#define ASHA_LE_PSM_CHAR_UUID                {0x1a,0xcc,0xf8,0x1d,0xe0,0xe2,0x4e,0xb3,0xaa,0x42,0xb6,0x82,0x39,0x03,0x41,0x2d}

/* ASHA service characteristics length */
#define ASHA_READONLYPROPERTIES_CHAR_LENGTH  17
#define ASHA_AUDIOCONTROLPOINT_CHAR_LENGTH   5
#define ASHA_AUDIOSTATUSPOINT_CHAR_LENGTH    1
#define ASHA_VOLUME_CHAR_LENGTH              2
#define ASHA_LE_PSM_CHAR_LENGTH              2

/* ASHA service characteristics names */
#define ASHA_READONLYPROPERTIES_CHAR_NAME    "ReadOnlyProperties"
#define ASHA_AUDIOCONTROLPOINT_CHAR_NAME     "AudioControlPoint"
#define ASHA_AUDIOSTATUSPOINT_CHAR_NAME      "AudioStatusPoint"
#define ASHA_VOLUME_CHAR_NAME                "Volume"
#define ASHA_LE_PSM_CHAR_NAME                "LE_PSM"

/* ASHA general parameter definitions */
#define ASHA_CAPABILITIES_SIDE_LEFT          (0 << 0)
#define ASHA_CAPABILITIES_SIDE_RIGHT         (1 << 0)
#define ASHA_CAPABILITIES_MONAURAL           (0 << 1)
#define ASHA_CAPABILITIES_BINAURAL           (1 << 1)
#define ASHA_FEATURE_MAP_LE_COC_SUPPORTED    (1 << 0)
#define ASHA_CODEC_ID_G722_16KHZ             (1 << 1)
#define ASHA_CODEC_ID_G722_24KHZ             (1 << 2)
#define ASHA_LE_PSM                          0xA8
#define ASHA_AUDIOCONTROLPOINT_START         1
#define ASHA_AUDIOCONTROLPOINT_STOP          2
#define ASHA_AUDIOCONTROLPOINT_STATUS        3
#define ASHA_L2CC_INITIAL_CREDITS            8

/* ----------------------------------------------------------------------------
 * Global variables and types
 * --------------------------------------------------------------------------*/
/** GATT service database IDs */
enum ASHA_idx_att
{
    /** Custom service ID: ASHA */
    ASHA_SERVICE,

    /** ASHA ReadOnlyProperties characteristic */
    ASHA_READONLYPROPERTIES_CHAR_IDX,
    /** ASHA ReadOnlyProperties characteristic value */
    ASHA_READONLYPROPERTIES_VAL_IDX,
    /** ASHA ReadOnlyProperties characteristic user descriptor */
    ASHA_READONLYPROPERTIES_USER_DESC_IDX,

    /** ASHA AudioControlPoint characteristic */
    ASHA_AUDIOCONTROLPOINT_CHAR_IDX,
    /** ASHA AudioControlPoint characteristic value */
    ASHA_AUDIOCONTROLPOINT_VAL_IDX,
    /** ASHA AudioControlPoint characteristic user descriptor */
    ASHA_AUDIOCONTROLPOINT_USER_DESC_IDX,

    /** ASHA AudioStatusPoint characteristic */
    ASHA_AUDIOSTATUSPOINT_CHAR_IDX,
    /** ASHA AudioStatusPoint characteristic value */
    ASHA_AUDIOSTATUSPOINT_VAL_IDX,
    /** ASHA AudioStatusPoint characteristic Client Characteristic Configuration */
    ASHA_AUDIOSTATUSPOINT_VAL_CCC0,
    /** ASHA AudioStatusPoint characteristic user descriptor */
    ASHA_AUDIOSTATUSPOINT_USER_DESC_IDX,

    /** ASHA Volume characteristic */
    ASHA_VOLUME_CHAR_IDX,
    /** ASHA Volume characteristic value */
    ASHA_VOLUME_VAL_IDX,
    /** ASHA Volume characteristic user descriptor */
    ASHA_VOLUME_USER_DESC_IDX,

    /** ASHA Low Energy Protocol/Service Multiplexer (LE_PSM) characteristic */
    ASHA_LE_PSM_CHAR_IDX,
    /** ASHA LE_PSM characteristic value */
    ASHA_LE_PSM_VAL_IDX,
    /** ASHA LE_PSM characteristic user descriptor */
    ASHA_LE_PSM_USER_DESC_IDX,

    /** ASHA Max number of services and characteristics */
    ASHA_MAX_IDX,
};

/**
 * Structure to hold ASHA
 * [ReadOnlyProperties][https://source.android.com/devices/bluetooth/asha#readonlyproperties]
 */
struct ReadOnlyProperties_t
{
    uint8_t version;
    uint8_t deviceCapabilities;
    uint8_t hiSyncId[8];
    uint8_t featureMap;
    uint16_t renderDelay;
    uint16_t preparationDelay;
    uint16_t codecIDs;
};

/** ASHA AudioControlPoint Start arguments  */
struct asha_audio_start
{
    /** The CODEC ID #ASHA_CodecId_t */
    uint8_t codec;
    /** The type of audio #ASHA_AudioType_t */
    uint8_t audiotype;
    /** A value from -128 to 0 representing the audio playback volume */
    int8_t volume;
    /* The otherstate field indicates whether the other side of the
       binaural devices is connected. The field value is 1 when the other peripheral device is connected, otherwise the value is 0. */
    int8_t otherstate;
};

struct asha_audio_status
{
    uint8_t connected;
};

struct AudioControlPoint_t
{
    uint8_t opCode;
    union {
        struct asha_audio_start start;
        struct asha_audio_status status;
    };
};

/** Describes events from the ASHA profile that need to handled by the application
 * code */
enum ASHA_Operation_t
{
    /** Received when the client changes the volume */
    ASHA_VOLUME_CHANGE = 1,
    /** Received when the audio streams starts */
    ASHA_AUDIO_START,
    /** Received when the audio streams stops */
    ASHA_AUDIO_STOP,
    /** Fired when an audio packet is received */
    ASHA_AUDIO_RCVD,
    /** AudioControl Point status */
    ASHA_AUDIO_STATUS
};

/** ASHA environment structure
 */
struct ASHA_Env_t
{
    /* GATT Attributes */
    uint16_t startHandle;
    /** Stores the users readOnlyProperties */
    uint8_t readOnlyProperties[17];
    /** The audioControlPoint data buffer */
    struct AudioControlPoint_t audioControlPoint;
    /** The audioStatusPoint data buffer */
    int8_t audioStatusPoint;
    /** The audioStatusPoint notification configuration buffer */
    uint8_t audioStatusPointCCC[2];
    /** The current volume value */
    int8_t volume;

    /* Status update on the other peripheral.
     * 0 - Other peripheral disconnected
       1 - Other peripheral connected
       2 - A LE Connection Parameter Update occurred on either connection */
    uint8_t otherPeripheralConnected;

    /** The LE Protocol/Service Multiplexer value buffer */
    uint16_t lePSM;

    /* L2C info */
    uint16_t peer_cid;
    uint16_t local_cid;

    /* Application callback */
    void (*appCallback)(enum ASHA_Operation_t op, void *param);

    /** The current connection index */
    uint8_t conidx;
};

enum ASHA_AudioType_t
{
    ASHA_AUDIOTYPE_UNKNOWN,
    ASHA_AUDIOTYPE_RINGTONE,
    ASHA_AUDIOTYPE_PHONECALL,
    ASHA_AUDIOTYPE_MEDIA,
};

/** Used as an argument to the user volume change callback */
struct asha_volume_change
{
    int8_t volume;
};

/** Used to pass the received L2CAP CoC audio streaming packet to the user calback */
struct asha_audio_received
{
    /** Number of credit used */
    uint16_t credit;
    /** SDU Data length */
    uint16_t length;
    /** ASHA sequence number */
    uint8_t seq_number;
    /** data */
    uint8_t data[__ARRAY_EMPTY];
};

/*
 * Advertisement payload structure for ASHA. Refer :
 * https://source.android.com/docs/core/connect/bluetooth/asha#advertisements-for-asha-gatt-service
 */
struct asha_adv_data_t
{
	uint8_t service_uuid[2];
	uint8_t protocol_version;
	uint8_t capability;
	uint8_t hisyncID[4];
};

/* ----------------------------------------------------------------------------
 * Function prototype definitions
 * --------------------------------------------------------------------------*/
void ASHA_Initialize(const struct ReadOnlyProperties_t *readOnlyProperties,
        void (*appCallback)(enum ASHA_Operation_t op, void *param));

void ASHA_AddCredits(uint16_t credits);

void ASHA_MsgHandler(ke_msg_id_t const msg_id, void const *param,
                     ke_task_id_t const dest_id, ke_task_id_t const src_id);

uint8_t ASHA_ReadOnlyProperties_Callback(uint8_t conidx, uint16_t attidx, uint16_t handle,
                                         uint8_t *toData, const uint8_t *fromData, uint16_t lenData,
                                         uint16_t operation);

uint8_t ASHA_AudioControlPoint_Callback(uint8_t conidx, uint16_t attidx, uint16_t handle,
                                        uint8_t *toData, const uint8_t *fromData, uint16_t lenData,
                                        uint16_t operation);

uint8_t ASHA_AudioStatusPoint_Callback(uint8_t conidx, uint16_t attidx, uint16_t handle,
                                       uint8_t *toData, const uint8_t *fromData, uint16_t lenData,
                                       uint16_t operation);

uint8_t ASHA_Volume_Callback(uint8_t conidx, uint16_t attidx, uint16_t handle,
                             uint8_t *toData, const uint8_t *fromData, uint16_t lenData,
                             uint16_t operation);

uint8_t ASHA_LE_PSM_Callback(uint8_t conidx, uint16_t attidx, uint16_t handle,
                             uint8_t *toData, const uint8_t *fromData, uint16_t lenData,
                             uint16_t operation);

int8_t ASHA_GetVolume(void);

void ASHA_NotifyStatusPoint(uint8_t conidx, uint8_t status);

struct ASHA_Env_t* ASHA_GetEnv(void);

/* ----------------------------------------------------------------------------
 * Close the 'extern "C"' block
 * ------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif    /* ifdef __cplusplus */

#endif    /* BLE_ASHA_H */
