#ifndef BS300_TEST_H
#define BS300_TEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Run BS300 Flash Read test:
 *   1. Initialize I2C hardware
 *   2. Read Program 0 raw data (528 bytes) from BS300 via I2C
 *   3. Dump hex via printf for logic-analyzer cross-validation
 *   4. Parse and print decoded key fields
 *
 * Requires DEBUG_UART_ENABLE for printf output.
 * Called once from main() after App_Initialize(). */
void bs300_test_run(void);

#ifdef __cplusplus
}
#endif

#endif /* BS300_TEST_H */
