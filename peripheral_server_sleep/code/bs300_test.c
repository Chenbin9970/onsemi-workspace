/**
 * BS300 Flash Read — Hardware I2C Verification Test
 *
 * Complete startup sequence + program read:
 *   Phase 1: bs300_startup()  — MUTE → KEY_LOCK → VERIFY_COMM
 *   Phase 2: Calibration read — 3 packets (144 bytes)
 *   Phase 2: Global Profile   — 1 packet (48 bytes)
 *   Phase 3: Program Read     — READ_START → 10 packets (480 bytes)
 *   Phase 4: Parse + Print
 */

#include "bs300_test.h"
#include "bs300_hal.h"
#include "bs300_startup.h"
#include "bs300_program_read.h"
#include "app.h"

#ifdef DEBUG_UART_ENABLE
#include "printf.h"
#endif

/* ============================================================
 * Hex dump helper
 * ============================================================ */

static void hex_dump(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        if (i % 16 == 0) PRINTF("\r\n%04X: ", i);
        PRINTF("%02X ", data[i]);
    }
    PRINTF("\r\n");
}

/* ============================================================
 * Key field printer
 * ============================================================ */

static void print_program_summary(const bs300_program_data_t *p)
{
    PRINTF("=== BS300 Program 0 Decoded ===\r\n");

    /* WDRC */
    PRINTF("WDRC: %uKP, %u channels, limiter=%u\r\n",
           p->wdrc.kneepoints_per_channel ? 2 : 1,
           p->wdrc.num_channels,
           p->wdrc.output_limiting_sel);

    PRINTF("WDRC BinGain[0..7]: ");
    for (uint8_t i = 0; i < 8; i++)
        PRINTF("%d ", (int8_t)(p->wdrc.bin_gain[i] - 27));
    PRINTF("\r\n");

    /* Volume */
    PRINTF("Volume: beep=%u freq=%u min=%d max=%d\r\n",
           p->volume.beep_level, p->volume.beep_frequency,
           p->volume.min_volume, p->volume.max_volume);

    /* Input */
    PRINTF("Input: %s", p->inputs.input_type);
    if (p->inputs.input_type[0] == 'm' && p->inputs.input_type[1] == 'm')
        PRINTF(" ratio=%u type=%u", p->inputs.mic_mixing_ratio,
               p->inputs.mm_type);
    if (p->inputs.input_type[0] == 'd' && p->inputs.input_type[1] == 'd')
        PRINTF(" mode=%u freq=%lu", p->inputs.mode, p->inputs.cutoff_frequency);
    PRINTF("\r\n");

    /* DFBC */
    if (p->has_dfbc)
        PRINTF("DFBC: mode=0x%02X\r\n", p->dfbc.dfbc_mode);

    /* ENR */
    if (p->has_enr) {
        PRINTF("ENR: %u channels, nfsf=%u nhsf=%u nnsf=%u snasf=%u\r\n",
               p->enr.num_channels, p->enr.nfsf, p->enr.nhsf,
               p->enr.nnsf, p->enr.snasf);
    }

    /* ISS */
    if (p->has_iss)
        PRINTF("ISS: threshold=%d\r\n", p->iss.iss_threshold);

    /* WNR */
    if (p->has_wnr)
        PRINTF("WNR: dual_mic=%u strength=%u\r\n",
               p->wnr.dual_mic_mode_sel,
               p->wnr.suppression_strength_preset);

    /* AGCO */
    if (p->has_agco)
        PRINTF("AGCO: atk=%ums rel=%ums thr=%u\r\n",
               p->agco.attack_time, p->agco.release_time,
               p->agco.threshold);

    PRINTF("---\r\n");
}

/* ============================================================
 * Test Runner
 * ============================================================ */

void bs300_test_run(void)
{
    static uint8_t             raw[BS300_TOTAL_DATA];
    static uint8_t             profile[48];
    static bs300_program_data_t prog;

    PRINTF("\r\n=== BS300 Flash Read Test ===\r\n");
    PRINTF("UART ready\r\n");

    /* --- Hardware init --- */
    PRINTF("I2C init... ");
    if (!bs300_hal_init()) {
        PRINTF("FAIL\r\n");
        return;
    }
    PRINTF("OK (SCL=DIO%u SDA=DIO%u, %luHz)\r\n",
           BS300_I2C_SCL_DIO, BS300_I2C_SDA_DIO,
           (uint32_t)BS300_I2C_SPEED_HZ);

    /* Wait 800ms for DSP power rail to stabilize */
    bs300_delay_ms(800);

    /* --- Phase 1: Startup (unlock chip) --- */
    PRINTF("BS300 startup (MUTE → KEY_LOCK → VERIFY_COMM)... ");
    if (!bs300_startup()) {
        PRINTF("FAIL\r\n");
        return;
    }
    PRINTF("OK\r\n");

    /* --- Phase 2: Global Profile read --- */
    PRINTF("Global Profile read... ");
    if (!bs300_read_global_profile(profile)) {
        PRINTF("FAIL\r\n");
        return;
    }
    PRINTF("OK (48 bytes)\r\n");
    hex_dump(profile, 48);

    /* --- Phase 3: Program read --- */
    PRINTF("Program 0 read (READ_START → 10 packets)... ");
    if (!bs300_program_read(0, raw)) {
        PRINTF("FAIL\r\n");
        return;
    }
    PRINTF("OK (%u bytes)\r\n", BS300_TOTAL_DATA);
    hex_dump(raw, BS300_TOTAL_DATA);

    /* --- Phase 4: Parse --- */
    PRINTF("Parsing... ");
    if (!bs300_program_parse(raw, &prog)) {
        PRINTF("FAIL\r\n");
        return;
    }
    PRINTF("OK\r\n");
    print_program_summary(&prog);

    PRINTF("=== BS300 Flash Read Test DONE ===\r\n");
}
