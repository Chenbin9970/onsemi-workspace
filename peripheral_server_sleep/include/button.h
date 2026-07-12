#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_ACTION_NONE = 0,
    BTN_ACTION_VOLUME_UP,
    BTN_ACTION_SWITCH_PROGRAM,
} btn_action_t;

void button_init(void);
void button_deinit(void);
btn_action_t button_get_action(void);
void button_clear_action(void);
void button_hold_tick(void);
void button_done_callback(void);

extern void DIO2_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_H */
