/**
 * BS300 Main Flash Storage Layer
 *
 * Each program gets its own 2KB Main Flash sector, fully independent.
 * Programs: 512B slots, append-only (4 slots per 2KB sector, erase every 4 writes).
 * Settings stored as append-only 64B slots in a 2KB sector (32 slots).
 *
 * Layout (Main Flash, after application code):
 *   0x0015C800  Settings   2KB  (32 × 64B slots, append-only)
 *   0x0015D000  Program 0  2KB
 *   0x0015D800  Program 1  2KB
 *   0x0015E000  Program 2  2KB
 *   0x0015E800  Program 3  2KB
 */

#include "bs300_storage.h"
#include <rsl10.h>
#include <rsl10_flash_rom.h>
#include <string.h>
#include "app.h"

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

/* ---- Sector base addresses ---- */
#define SETTINGS_BASE    0x0015C800
#define PROG0_BASE       0x0015D000
#define PROG1_BASE       0x0015D800
#define PROG2_BASE       0x0015E000
#define PROG3_BASE       0x0015E800

static const uint32_t PROG_BASE[4] = {
    PROG0_BASE, PROG1_BASE, PROG2_BASE, PROG3_BASE
};

/* ---- Magic & header ---- */
#define PROG_MAGIC_OFFSET    480   /* trailer: magic(4) + version(2) + CRC(2) after 480B payload */
#define PROG_SLOT_SIZE       512   /* 4 slots per 2KB sector */
#define PROG_MAX_SLOTS       4
static const uint8_t PROG_MAGIC[4] = { 'B', 'S', 'P', 'G' };
#define PROG_VERSION  1

/* ---- Settings: 64B slots, append-only in 2KB sector (32 slots, erase every 32 writes) ---- */
#define SETTINGS_SLOT_SIZE    64
#define SETTINGS_MAX_SLOTS    32    /* 2048 / 64 */

/* Slot layout (offsets within 64B slot):
 *  0:  active_prog
 *  1-4:  volume[4]
 *  5-8:  eq_low[4]
 *  9-12: eq_mid[4]
 *  13-16: eq_high[4]
 *  17-20: denoise[4]
 *  21-24: magic "BSST"
 *  25-26: CRC16 XMODEM over bytes 0-24
 *  27:    version
 *  28-63: reserved (0xFF)
 */
#define SLOT_DATA_LEN     25   /* bytes 0-24: covered by CRC */
#define SLOT_MAGIC_OFF    21
#define SLOT_CRC_OFF      25
#define SLOT_VER_OFF      27

static const uint32_t SETTINGS_MAGIC_WORD = 0x54535342;  /* "BSST" little-endian */
#define SETTINGS_VER  3  /* bumped: added denoise per-program storage */

/* ---- Main Flash unlock (HIGH region: 0x00150000+) ---- */
static void main_flash_unlock(void)
{
    FLASH->MAIN_CTRL = (MAIN_HIGH_W_ENABLE
                      | MAIN_MIDDLE_W_DISABLE
                      | MAIN_LOW_W_DISABLE);
    FLASH->MAIN_WRITE_UNLOCK = FLASH_MAIN_KEY;
}

/* ---- CRC16 (XMODEM, polynomial 0x1021) ---- */
static uint16_t crc16_xmodem(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0;
    uint16_t i;
    uint8_t b;
    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (b = 0; b < 8; b++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* ============================================================
 *  Program storage — per-sector
 * ============================================================ */

bool bs300_storage_write_program(uint8_t idx, const uint8_t *data)
{
    uint32_t base;
    uint32_t buf[BS300_TOTAL_DATA / 4];
    uint32_t thdr[2];
    uint32_t slot_base;
    uint16_t crc;
    int empty_slot = -1;
    int i;

    if (idx > 3 || data == NULL) return false;
    main_flash_unlock();
    base = PROG_BASE[idx];

    /* Build payload + trailer */
    memcpy(buf, data, BS300_TOTAL_DATA);
    memcpy(thdr, PROG_MAGIC, 4);
    ((uint8_t *)thdr)[4] = PROG_VERSION;
    ((uint8_t *)thdr)[5] = 0;
    crc = crc16_xmodem(data, BS300_TOTAL_DATA);
    ((uint8_t *)thdr)[6] = (uint8_t)(crc & 0xFF);
    ((uint8_t *)thdr)[7] = (uint8_t)(crc >> 8);

    /* Find next empty slot (magic at offset 480 == 0xFFFFFFFF) */
    for (i = 0; i < PROG_MAX_SLOTS; i++) {
        uint32_t magic = *(const uint32_t *)(base
                          + (uint32_t)i * PROG_SLOT_SIZE + PROG_MAGIC_OFFSET);
        if (magic == 0xFFFFFFFF) {
            empty_slot = i;
            break;
        }
    }

    /* Sector full — erase */
    if (empty_slot < 0) {
        Sys_Watchdog_Refresh();
        __disable_irq();
        if (Flash_EraseSector(base) != FLASH_ERR_NONE) {
            __enable_irq();
            PRINTF("[BS300] program %u erase FAIL\r\n", idx);
            return false;
        }
        __enable_irq();
        empty_slot = 0;
        PRINTF("[BS300] program %u sector erased (4 slots full)\r\n", idx);
    }

    slot_base = base + (uint32_t)empty_slot * PROG_SLOT_SIZE;

    /* Write 480B data */
    Sys_Watchdog_Refresh();
    __disable_irq();
    if (Flash_WriteBuffer(slot_base, BS300_TOTAL_DATA / 4, buf) != FLASH_ERR_NONE) {
        __enable_irq();
        PRINTF("[BS300] program %u write FAIL\r\n", idx);
        return false;
    }

    /* Write trailer at offset 480 */
    Sys_Watchdog_Refresh();
    if (Flash_WriteBuffer(slot_base + BS300_TOTAL_DATA, 2, thdr) != FLASH_ERR_NONE) {
        __enable_irq();
        PRINTF("[BS300] program %u hdr write FAIL\r\n", idx);
        return false;
    }
    __enable_irq();

    PRINTF("[BS300] program %u saved slot=%d\r\n", idx, empty_slot);
    return true;
}

void bs300_storage_load_program(uint8_t idx, uint8_t *data_out)
{
    const uint8_t *base;
    int i;

    if (idx > 3 || data_out == NULL) return;
    base = (const uint8_t *)PROG_BASE[idx];

    for (i = PROG_MAX_SLOTS - 1; i >= 0; i--) {
        const uint8_t *slot = base + (uint32_t)i * PROG_SLOT_SIZE;
        const uint8_t *trailer = slot + PROG_MAGIC_OFFSET;
        uint32_t magic = *(const uint32_t *)trailer;

        if (magic == 0xFFFFFFFF) continue;
        if (memcmp(trailer, PROG_MAGIC, 4) != 0) continue;

        {
            uint16_t stored = (uint16_t)trailer[6]
                            | ((uint16_t)trailer[7] << 8);
            uint16_t calc = crc16_xmodem(slot, BS300_TOTAL_DATA);
            if (stored == calc) {
                memcpy(data_out, slot, BS300_TOTAL_DATA);
                return;
            }
        }
    }

    /* No valid slot — return erased state */
    memset(data_out, 0xFF, BS300_TOTAL_DATA);
}

bool bs300_storage_is_valid(uint8_t idx)
{
    const uint8_t *base;
    int i;

    if (idx > 3) return false;
    base = (const uint8_t *)PROG_BASE[idx];

    for (i = PROG_MAX_SLOTS - 1; i >= 0; i--) {
        const uint8_t *slot = base + (uint32_t)i * PROG_SLOT_SIZE;
        const uint8_t *trailer = slot + PROG_MAGIC_OFFSET;
        uint32_t magic = *(const uint32_t *)trailer;

        if (magic == 0xFFFFFFFF) continue;
        if (memcmp(trailer, PROG_MAGIC, 4) != 0) continue;

        {
            uint16_t stored = (uint16_t)trailer[6]
                            | ((uint16_t)trailer[7] << 8);
            uint16_t calc = crc16_xmodem(slot, BS300_TOTAL_DATA);
            if (stored == calc) return true;
        }
    }
    return false;
}

void bs300_storage_invalidate(uint8_t idx)
{
    uint32_t base;
    if (idx > 3) return;
    main_flash_unlock();
    base = PROG_BASE[idx];
    Flash_EraseSector(base);
    PRINTF("[BS300] program %u invalidated\r\n", idx);
}

/* ============================================================
 *  Settings storage
 * ============================================================ */

static void settings_build_slot(uint8_t active_prog,
                                  const uint8_t *volume,
                                  const int8_t *eq_low,
                                  const int8_t *eq_mid,
                                  const int8_t *eq_high,
                                  const uint8_t *denoise,
                                  uint8_t *slot)
{
    uint16_t crc;
    uint8_t i;
    memset(slot, 0xFF, SETTINGS_SLOT_SIZE);

    slot[0] = active_prog;
    if (volume != NULL) {
        slot[1] = volume[0];
        slot[2] = volume[1];
        slot[3] = volume[2];
        slot[4] = volume[3];
    }
    if (eq_low != NULL) {
        for (i = 0; i < 4; i++) slot[5 + i] = (uint8_t)eq_low[i];
    }
    if (eq_mid != NULL) {
        for (i = 0; i < 4; i++) slot[9 + i] = (uint8_t)eq_mid[i];
    }
    if (eq_high != NULL) {
        for (i = 0; i < 4; i++) slot[13 + i] = (uint8_t)eq_high[i];
    }
    if (denoise != NULL) {
        for (i = 0; i < 4; i++) slot[17 + i] = denoise[i];
    }

    memcpy(slot + SLOT_MAGIC_OFF, &SETTINGS_MAGIC_WORD, 4);
    slot[SLOT_VER_OFF] = SETTINGS_VER;

    crc = crc16_xmodem(slot, SLOT_DATA_LEN);
    slot[SLOT_CRC_OFF]     = (uint8_t)(crc & 0xFF);
    slot[SLOT_CRC_OFF + 1] = (uint8_t)(crc >> 8);
}

bool bs300_settings_save(uint8_t active_prog, const uint8_t *volume,
                          const int8_t *eq_low, const int8_t *eq_mid,
                          const int8_t *eq_high, const uint8_t *denoise)
{
    uint32_t slot_buf[SETTINGS_SLOT_SIZE / 4];
    uint8_t *slot = (uint8_t *)slot_buf;
    int empty_slot = -1;
    int i;

    settings_build_slot(active_prog, volume, eq_low, eq_mid, eq_high,
                        denoise, slot);

    main_flash_unlock();

    /* Find next empty slot (magic word == 0xFFFFFFFF) */
    for (i = 0; i < SETTINGS_MAX_SLOTS; i++) {
        uint32_t magic = *(const uint32_t *)(SETTINGS_BASE
                          + (uint32_t)i * SETTINGS_SLOT_SIZE + SLOT_MAGIC_OFF);
        if (magic == 0xFFFFFFFF) {
            empty_slot = i;
            break;
        }
    }

    /* Sector full — erase and rewrite to slot 0 */
    if (empty_slot < 0) {
        Sys_Watchdog_Refresh();
        __disable_irq();
        if (Flash_EraseSector(SETTINGS_BASE) != FLASH_ERR_NONE) {
            __enable_irq();
            PRINTF("[BS300] settings erase FAIL\r\n");
            return false;
        }
        __enable_irq();
        empty_slot = 0;
        PRINTF("[BS300] settings sector erased (32 slots full)\r\n");
    }

    /* Write slot (no erase — slot is already 0xFF) */
    Sys_Watchdog_Refresh();
    __disable_irq();
    if (Flash_WriteBuffer(SETTINGS_BASE + (uint32_t)empty_slot * SETTINGS_SLOT_SIZE,
                          SETTINGS_SLOT_SIZE / 4, slot_buf) != FLASH_ERR_NONE) {
        __enable_irq();
        PRINTF("[BS300] settings write FAIL\r\n");
        return false;
    }
    __enable_irq();

    PRINTF("[BS300] settings saved prog=%u slot=%d vol=[%u,%u,%u,%u]\r\n",
           active_prog, empty_slot,
           volume ? volume[0] : 0, volume ? volume[1] : 0,
           volume ? volume[2] : 0, volume ? volume[3] : 0);
    return true;
}

bool bs300_settings_load(uint8_t *active_prog, uint8_t *volume,
                          int8_t *eq_low, int8_t *eq_mid, int8_t *eq_high,
                          uint8_t *denoise)
{
    int i;
    uint8_t j;

    /* Scan backwards: latest valid slot wins */
    for (i = SETTINGS_MAX_SLOTS - 1; i >= 0; i--) {
        const uint8_t *slot = (const uint8_t *)(SETTINGS_BASE
                                + (uint32_t)i * SETTINGS_SLOT_SIZE);
        uint32_t magic = *(const uint32_t *)(slot + SLOT_MAGIC_OFF);

        if (magic == 0xFFFFFFFF) continue;          /* empty */
        if (magic != SETTINGS_MAGIC_WORD) continue; /* corrupted */

        {
            uint16_t stored_crc = (uint16_t)slot[SLOT_CRC_OFF]
                                | ((uint16_t)slot[SLOT_CRC_OFF + 1] << 8);
            uint16_t calc_crc = crc16_xmodem(slot, SLOT_DATA_LEN);
            if (stored_crc != calc_crc) continue;   /* CRC fail */
        }

        if (slot[SLOT_VER_OFF] > SETTINGS_VER) continue; /* too new */

        /* Found valid slot */
        {
            uint8_t prog = slot[0];
            /* Program 3 is audio mode — never restore it */
            if (active_prog != NULL) *active_prog = (prog == 3) ? 0 : prog;

            if (volume != NULL) {
                volume[0] = slot[1];
                volume[1] = slot[2];
                volume[2] = slot[3];
                volume[3] = slot[4];
            }
            if (eq_low != NULL)
                for (j = 0; j < 4; j++) eq_low[j] = (int8_t)slot[5 + j];
            if (eq_mid != NULL)
                for (j = 0; j < 4; j++) eq_mid[j] = (int8_t)slot[9 + j];
            if (eq_high != NULL)
                for (j = 0; j < 4; j++) eq_high[j] = (int8_t)slot[13 + j];
            if (denoise != NULL)
                for (j = 0; j < 4; j++) denoise[j] = slot[17 + j];
        }

        PRINTF("[BS300] settings loaded prog=%u slot=%d vol=[%u,%u,%u,%u]\r\n",
               active_prog ? *active_prog : 0xFF, i,
               volume ? volume[0] : 0, volume ? volume[1] : 0,
               volume ? volume[2] : 0, volume ? volume[3] : 0);
        return true;
    }

    PRINTF("[BS300] settings not found\r\n");
    return false;
}

void bs300_settings_invalidate(void)
{
    main_flash_unlock();
    Flash_EraseSector(SETTINGS_BASE);
    PRINTF("[BS300] settings invalidated\r\n");
}
