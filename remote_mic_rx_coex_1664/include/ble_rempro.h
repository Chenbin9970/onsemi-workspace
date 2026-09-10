#ifndef BLE_REMPRO_H
#define BLE_REMPRO_H

#ifdef __cplusplus
extern "C" {
#endif

/* REMPRO Service UUID — F36F8680-ABEC-11F1-8F9E-7265746F6E65 */
#define REMPRO_SVC_UUID                        { 0x65, 0x6e, 0x6f, 0x74, 0x65, 0x72, \
                                                 0x9e, 0x8f, 0xf1, 0x11, 0xec, 0xab, \
                                                 0x80, 0x86, 0x6f, 0xf3 }
/* 手机→设备 命令写入通道（Read/Write）— F36F8681-ABEC-11F1-8F9E-7265746F6E65 */
#define REMPRO_CHARACTERISTIC_ROLE_UUID        { 0x65, 0x6e, 0x6f, 0x74, 0x65, 0x72, \
                                                 0x9e, 0x8f, 0xf1, 0x11, 0xec, 0xab, \
                                                 0x81, 0x86, 0x6f, 0xf3 }
/* 设备→手机 Notify — F36F8683-ABEC-11F1-8F9E-7265746F6E65 */
#define REMPRO_CHARACTERISTIC_ONOFF_UUID       { 0x65, 0x6e, 0x6f, 0x74, 0x65, 0x72, \
                                                 0x9e, 0x8f, 0xf1, 0x11, 0xec, 0xab, \
                                                 0x83, 0x86, 0x6f, 0xf3 }

enum rempro_idx_att
{
    REMPRO_IDX_ROLE_VALUE_CHAR,
    REMPRO_IDX_ROLE_VALUE_VAL,
    REMPRO_IDX_ROLE_VALU_CCC,
    REMPRO_IDX_ROLE_VALUE_USR_DSCP,

    REMPRO_IDX_ONOFF_VALUE_CHAR,
    REMPRO_IDX_ONOFF_VALUE_VAL,
    REMPRO_IDX_ONOFF_VALU_CCC,

    REMPRO_IDX_NB,
};

#define REMPRO_ROLE_VALUE_MAX_LENGTH        20
#define REMPRO_ONOFF_VALUE_MAX_LENGTH       20
#define REMPRO_USER_DESCRIPTION_MAX_LENGTH  16

struct rempro_env_tag
{
    uint16_t start_hdl;

    uint8_t  role_value[REMPRO_ROLE_VALUE_MAX_LENGTH];
    uint16_t role_cccd_value;
    bool     role_value_changed;
    uint8_t  role_value_len;

    uint8_t  onoff_value[REMPRO_ONOFF_VALUE_MAX_LENGTH];
    uint16_t onoff_cccd_value;
    bool     onoff_value_changed;
    bool     sentSuccess;

    uint8_t  state;
    uint16_t cnt_notifc;
    uint8_t  val_notif;
};

extern struct rempro_env_tag rempro_env;

void RemproService_Env_Initialize(void);
void RemproService_ServiceAdd(void);
void RemproService_SendNotification(uint8_t conidx, uint8_t attidx,
                                    uint8_t *value, uint8_t length);

#ifdef __cplusplus
}
#endif

#endif
