/* ----------------------------------------------------------------------------
 * Copyright (c) 2017 Semiconductor Components Industries, LLC (d/b/a
 * ON Semiconductor), All Rights Reserved
 *
 * This code is the property of ON Semiconductor and may not be redistributed
 * in any form without prior written permission from ON Semiconductor.
 * The terms of use and warranty for this code are covered by contractual
 * agreements between ON Semiconductor and the licensee.
 *
 * This is Reusable Code.
 *
 * ------------------------------------------------------------------------- */

#include "sharedBuffers.h"

/* We define the shared memory in one place and it's location is controlled
 * by the linker file. This buffer is located in DRAM which is shared between
 * the LPDSP32 and the ARM
 */
struct _sharedMemory Buffer __attribute__ ((section (".shared")));
