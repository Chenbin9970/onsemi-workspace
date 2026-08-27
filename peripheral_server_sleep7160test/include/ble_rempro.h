#ifndef BLE_REMPRO_H
#define BLE_REMPRO_H

#ifdef __cplusplus
extern "C" {
#endif

/* REMPRO Service UUID */
#define REMPRO_SVC_UUID                        { 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, \
                                                 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, \
                                                 0x00, 0xaf, 0x00, 0x00 }
#define REMPRO_CHARACTERISTIC_ROLE_UUID        { 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, \
                                                 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, \
                                                 0x01, 0xaf, 0x00, 0x00 }
#define REMPRO_CHARACTERISTIC_ONOFF_UUID       { 0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, \
                                                 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, \
                                                 0x02, 0xaf, 0x00, 0x00 }

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
