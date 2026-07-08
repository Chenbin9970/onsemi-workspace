/**
 * BS300 Driver Integration Test
 *
 * Uses bs300_driver_init() for one-shot init with NVR caching.
 * On first boot: reads all 4 programs from BS300 chip via I2C (~2-3s).
 * On subsequent boots: loads from NVR3 cache (~0 I2C time).
 */

#include "bs300_test.h"
#include "bs300_driver.h"
#include "app.h"

#ifdef DEBUG_UART_ENABLE
#include "printf.h"
#endif

#ifndef PRINTF
#define PRINTF(...) ((void)0)
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
 * Full program printer
 * ============================================================ */

static void print_raw_data(const uint8_t *raw)
{
    PRINTF("Raw 480B hex:");
    hex_dump(raw, BS300_TOTAL_DATA);
}

static void print_wdrc(const bs300_wdrc_t *w)
{
    PRINTF("  num_channels=%u  KP=%u  limiter=%u\r\n",
           w->num_channels,
           w->kneepoints_per_channel ? 2 : 1,
           w->output_limiting_sel);

    PRINTF("  bin_gain (signed, offset 27):\r\n    ");
    for (uint8_t i = 0; i < BS300_WDRC_BANDS; i++) {
        PRINTF("%d ", (int8_t)(w->bin_gain[i] - 27));
        if (i % 16 == 15) PRINTF("\r\n    ");
    }

    for (uint8_t i = 0; i < w->num_channels; i++) {
        const bs300_wdrc_channel_t *ch = &w->channels[i];
        PRINTF("  CH%-2u: fidx=%-2u  kp1_th=%-3d kp2_th=%-3d  lmt_th=%-3d  "
               "epd(a=%u r=%u r_idx=%u)  "
               "kp1(a=%u r=%u r_idx=%u)  "
               "kp2(a=%u r=%u r_idx=%u)  "
               "lmt(a=%u r=%u r_idx=%u)\r\n",
               i, ch->frequency_idx,
               (int8_t)ch->kp1_th, (int8_t)ch->kp2_th, (int8_t)ch->lmt_th,
               ch->epd_at, ch->epd_rt, ch->epd_r,
               ch->kp1_at, ch->kp1_rt, ch->kp1_r,
               ch->kp2_at, ch->kp2_rt, ch->kp2_r,
               ch->lmt_at, ch->lmt_rt, ch->lmt_r);
    }
}

static void print_volume(const bs300_volume_t *v)
{
    PRINTF("  beep_level=%u  beep_freq=%u  min_vol=%d  max_vol=%d  "
           "batt_beep_level=%u  batt_beep_freq=%u\r\n",
           v->beep_level, v->beep_frequency,
           v->min_volume, v->max_volume,
           v->battery_flat_beep_level, v->battery_flat_beep_frequency);
}

static void print_inputs(const bs300_inputs_t *inp)
{
    PRINTF("  type=%s", inp->input_type);
    if (inp->input_type[0] == 'm' && inp->input_type[1] == 'm')
        PRINTF("  ratio=%u  mm_type=%u", inp->mic_mixing_ratio, inp->mm_type);
    if (inp->input_type[0] == 'd' && inp->input_type[1] == 'd')
        PRINTF("  omni_thr=%u  mode=%u  cutoff=%lu",
               inp->omni_threshold, inp->mode, inp->cutoff_frequency);
    PRINTF("\r\n");
}

static void print_dfbc(const bs300_dfbc_t *d)
{
    PRINTF("  mode=0x%02X\r\n", d->dfbc_mode);
}

static void print_enr(const bs300_enr_t *e)
{
    PRINTF("  num_channels=%u  nfsf=%u  nhsf=%u  nnsf=%u  snasf=%u\r\n",
           e->num_channels, e->nfsf, e->nhsf, e->nnsf, e->snasf);
    for (uint8_t i = 0; i < e->num_channels; i++) {
        const bs300_enr_channel_t *ch = &e->channels[i];
        PRINTF("  CH%-2u: fidx=%-2u  ma=%-2u  snrth=%-2u  nt=%-2u  unt=%-2u  "
               "etr=%-3u  nrr=%-2u\r\n",
               i, ch->frequency_idx, ch->ma, ch->snrth, ch->nt, ch->unt,
               ch->etr, ch->nrr);
    }
}

static void print_iss(const bs300_iss_t *s)
{
    PRINTF("  threshold=%u\r\n", s->iss_threshold);
}

static void print_wnr(const bs300_wnr_t *w)
{
    PRINTF("  dual_mic_mode=%u  strength_preset=%u\r\n",
           w->dual_mic_mode_sel, w->suppression_strength_preset);
}

static void print_agco(const bs300_agco_t *a)
{
    PRINTF("  atk=%ums  rel=%ums  threshold=%u\r\n",
           a->attack_time, a->release_time, a->threshold);
}

static void print_program_full(uint8_t idx, const bs300_program_data_t *p)
{
    PRINTF("\r\n========================================\r\n");
    PRINTF("=== Program %u ===\r\n", idx);
    PRINTF("========================================\r\n");

    PRINTF("--- WDRC ---\r\n");
    print_wdrc(&p->wdrc);

    PRINTF("--- Volume/Beep ---\r\n");
    print_volume(&p->volume);

    PRINTF("--- Input ---\r\n");
    print_inputs(&p->inputs);

    if (p->has_dfbc) {
        PRINTF("--- DFBC ---\r\n");
        print_dfbc(&p->dfbc);
    }

    if (p->has_enr) {
        PRINTF("--- ENR ---\r\n");
        print_enr(&p->enr);
    }

    if (p->has_iss) {
        PRINTF("--- ISS ---\r\n");
        print_iss(&p->iss);
    }

    if (p->has_wnr) {
        PRINTF("--- WNR ---\r\n");
        print_wnr(&p->wnr);
    }

    if (p->has_agco) {
        PRINTF("--- AGCO ---\r\n");
        print_agco(&p->agco);
    }
}

/* ============================================================
 * Test Runner
 * ============================================================ */

void bs300_test_run(void)
{
    PRINTF("\r\n=== BS300 Driver Test ===\r\n");
    PRINTF("UART ready\r\n");

    /* One-shot init: handles I2C + NVR cache transparently */
    if (!bs300_driver_init()) {
        PRINTF("BS300: driver init FAIL\r\n");
        return;
    }

    /* Calibration */
    const uint8_t *calib = bs300_driver_get_calibration();
    if (calib) {
        PRINTF("Calibration (144 bytes):");
        hex_dump(calib, 144);
    } else {
        PRINTF("Calibration: not loaded (NVR cached boot)\r\n");
    }

    /* Print all 4 programs fully */
    for (uint8_t i = 0; i < 4; i++) {
        const bs300_program_data_t *prog = bs300_driver_get_program(i);
        if (prog) {
            print_program_full(i, prog);
        } else {
            PRINTF("Program %u: NULL\r\n", i);
        }
    }

    /* Sync program 0 to BS300 RAM */
    PRINTF("\r\n=== Syncing Program 0 to RAM ===\r\n");
    if (!bs300_driver_sync_ram(0)) {
        PRINTF("BS300: RAM sync FAIL\r\n");
    }

    PRINTF("\r\n=== BS300 Driver Test DONE ===\r\n");
}
