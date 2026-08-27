/* ----------------------------------------------------------------------------
 * app_trace.h — minimal stub for SDK's ble_asha.c PRINTF dependency
 * In sleep project, debug output uses 'printf.h' via DEBUG_UART_ENABLE.
 * This stub provides empty PRINTF / TRACE_INIT / ASSERT macros.
 * ------------------------------------------------------------------------- */

#ifndef APP_TRACE_H
#define APP_TRACE_H

#include <stdio.h>
#define TRACE_INIT()
#ifdef DEBUG_UART_ENABLE
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif
#define ASSERT(msg)

#endif /* APP_TRACE_H */
