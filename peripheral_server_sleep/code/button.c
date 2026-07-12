#include "button.h"
#include "app.h"
#include "bs300_ram_sync.h"
#include <rsl10.h>

/* ---- constants ---- */
#define BTN_HOLD_THRESHOLD   8   /* 8 * 200ms = 1600ms, ~1.5s */
#define BTN_DEBOUNCE         4   /* 4 * slowclk/1024 = ~125us */

/* ---- state ---- */
static volatile int16_t   btn_hold_ticks;
static volatile bool      btn_pressed;
static volatile bool      btn_has_action;
static volatile btn_action_t btn_action;
static volatile uint8_t   btn_pending;

/* ---- external: low power control ---- */
extern struct low_power_clk_param_tag low_power_clk_param;

/* ---- DIO2 ISR — handles edges while CPU is awake ----
 *   RSL10 DIO debounce has a known limitation: the hardware generates
 *   a spurious interrupt after each real transition.  We skip every
 *   other interrupt (the ignore_next pattern) and only process genuine
 *   state changes. ---- */
void DIO2_IRQHandler(void)
{
    static uint8_t ignore_next;

    if (ignore_next) {
        ignore_next = 0;
        return;
    }

    if (DIO_DATA->ALIAS[BUTTON_DIO] == 0) {
        /* button pressed (or spurious re-assert) */
        ignore_next = 1;
        if (btn_pressed) return;   /* already tracking, ignore repeats */
        btn_pressed    = true;
        btn_hold_ticks = 0;
        btn_has_action = false;
        low_power_clk_param.low_power_enable = false;
    } else {
        /* button released (or spurious) */
        ignore_next = 1;
        if (!btn_pressed) return;  /* not tracking a press, ignore */
        btn_pressed = false;
        if (!btn_has_action) {
            btn_action  = (btn_hold_ticks >= BTN_HOLD_THRESHOLD)
                        ? BTN_ACTION_SWITCH_PROGRAM
                        : BTN_ACTION_VOLUME_UP;
            btn_has_action = true;
            btn_pending    = 1;
        }
    }
}

/* ---- covers the case where button was pressed during sleep,
 *     so the falling-edge was missed by the ISR ---- */
void button_wakeup_check(void)
{
    if (!btn_pressed && DIO_DATA->ALIAS[BUTTON_DIO] == 0) {
        btn_pressed    = true;
        btn_hold_ticks = 0;
        btn_has_action = false;
        low_power_clk_param.low_power_enable = false;
    }
}

/* ---- called from APP_Timer every 200ms ---- */
void button_hold_tick(void)
{
    if (btn_pressed && !btn_has_action) {
        btn_hold_ticks++;
        if (btn_hold_ticks >= BTN_HOLD_THRESHOLD) {
            btn_action    = BTN_ACTION_SWITCH_PROGRAM;
            btn_has_action = true;
            btn_pending    = 1;
        }
    }
}

/* ---- called from Main_Loop ---- */
btn_action_t button_get_action(void)
{
    if (btn_pending == 0) return BTN_ACTION_NONE;
    return btn_action;
}

void button_clear_action(void)
{
    btn_pending = 0;
}

/* ---- called from async BS300 completion ----
 *   Only re-enable low power when I2C bus is truly idle. ---- */
void button_done_callback(void)
{
    if (!btn_pressed && !bs300_sync_is_busy()) {
        low_power_clk_param.low_power_enable = true;
    }
}

/* ---- init ---- */
void button_init(void)
{
    btn_hold_ticks = 0;
    btn_pressed    = false;
    btn_has_action = false;
    btn_action     = BTN_ACTION_NONE;
    btn_pending    = 0;

    Sys_DIO_Config(BUTTON_DIO,
                   DIO_MODE_INPUT | DIO_WEAK_PULL_UP | DIO_LPF_DISABLE);

    Sys_DIO_IntConfig(2,
                      DIO_SRC_DIO_2 | DIO_EVENT_TRANSITION,
                      DIO_DEBOUNCE_SLOWCLK_DIV1024,
                      BTN_DEBOUNCE);

    NVIC_ClearPendingIRQ(DIO2_IRQn);
    NVIC_SetPriority(DIO2_IRQn, 3);
    NVIC_EnableIRQ(DIO2_IRQn);
}

void button_deinit(void)
{
    NVIC_DisableIRQ(DIO2_IRQn);
    NVIC_ClearPendingIRQ(DIO2_IRQn);
    Sys_DIO_IntConfig(2, DIO_EVENT_NONE, DIO_DEBOUNCE_SLOWCLK_DIV1024, 0);
    btn_pending    = 0;
    btn_has_action = false;
    btn_pressed    = false;
}
