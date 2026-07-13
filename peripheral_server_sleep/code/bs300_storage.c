/**
 * BS300 Main Flash Storage Layer
 *
 * Each program gets its own 2KB Main Flash sector, fully independent.
 * Settings (active_prog + volume[4]) stored in a separate sector.
 *
 * Layout (Main Flash, after application code):
 *   0x0015C800  Settings   2KB
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
static const uint8_t PROG_MAGIC[4] = { 'B', 'S', 'P', 'G' };
#define PROG_VERSION  1

/* ---- Settings layout (16 bytes in 2KB sector) ---- */
#define SETTINGS_ACTIVE_PROG   0
#define SETTINGS_VOLUME        1   /* volume[0..3] at offset 1..4 */
#define SETTINGS_RESERVED      5
#define SETTINGS_MAGIC         8
#define SETTINGS_CRC           12
#define SETTINGS_VERSION       14

static const uint8_t SETTINGS_MAGIC_VAL[4] = { 'B', 'S', 'S', 'T' };
#define SETTINGS_VER  1

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
    uint8_t hdr[8];

    if (idx > 3 || data == NULL) return false;
    main_flash_unlock();
    base = PROG_BASE[idx];

    /* Build 480B payload + trailer magic/version/crc */
    memcpy(buf, data, BS300_TOTAL_DATA);

    /* Write magic + version + CRC at the tail of the sector (after 480B) */
    memcpy(hdr, PROG_MAGIC, 4);
    hdr[4] = PROG_VERSION;
    hdr[5] = 0;
    {
        uint16_t crc = crc16_xmodem(data, BS300_TOTAL_DATA);
        hdr[6] = (uint8_t)(crc & 0xFF);
        hdr[7] = (uint8_t)(crc >> 8);
    }

    if (Flash_EraseSector(base) != FLASH_ERR_NONE) {
        PRINTF("[BS300] program %u erase FAIL\r\n", idx);
        return false;
    }

    if (Flash_WriteBuffer(base, BS300_TOTAL_DATA / 4, buf) != FLASH_ERR_NONE) {
        PRINTF("[BS300] program %u write FAIL\r\n", idx);
        return false;
    }

    /* Write trailer at 480B offset */
    {
        uint32_t thdr[2];
        memcpy(thdr, hdr, 8);
        if (Flash_WriteBuffer(base + BS300_TOTAL_DATA, 2, thdr) != FLASH_ERR_NONE) {
            PRINTF("[BS300] program %u hdr write FAIL\r\n", idx);
            return false;
        }
    }

    PRINTF("[BS300] program %u saved\r\n", idx);
    return true;
}

void bs300_storage_load_program(uint8_t idx, uint8_t *data_out)
{
    if (idx > 3 || data_out == NULL) return;
    memcpy(data_out, (const uint8_t *)PROG_BASE[idx], BS300_TOTAL_DATA);
}

bool bs300_storage_is_valid(uint8_t idx)
{
    const uint8_t *base;
    uint8_t magic_buf[4];
    uint16_t stored_crc, calc_crc;

    if (idx > 3) return false;
    base = (const uint8_t *)PROG_BASE[idx];

    /* Check magic at offset 476 */
    memcpy(magic_buf, base + PROG_MAGIC_OFFSET, 4);
    if (memcmp(magic_buf, PROG_MAGIC, 4) != 0)
        return false;

    /* Check CRC */
    stored_crc = (uint16_t)base[PROG_MAGIC_OFFSET + 6]
               | ((uint16_t)base[PROG_MAGIC_OFFSET + 7] << 8);
    calc_crc = crc16_xmodem(base, BS300_TOTAL_DATA);

    return stored_crc == calc_crc;
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

static void settings_build_payload(uint8_t active_prog,
                                   const uint8_t *volume,
                                   uint8_t *out)
{
    uint16_t crc;
    memset(out, 0xFF, BS300_TOTAL_DATA);

    out[SETTINGS_ACTIVE_PROG] = active_prog;
    if (volume != NULL) {
        out[SETTINGS_VOLUME + 0] = volume[0];
        out[SETTINGS_VOLUME + 1] = volume[1];
        out[SETTINGS_VOLUME + 2] = volume[2];
        out[SETTINGS_VOLUME + 3] = volume[3];
    }

    memcpy(out + SETTINGS_MAGIC, SETTINGS_MAGIC_VAL, 4);
    out[SETTINGS_VERSION]     = SETTINGS_VER;
    out[SETTINGS_VERSION + 1] = 0;

    crc = crc16_xmodem(out, SETTINGS_CRC);
    out[SETTINGS_CRC]     = (uint8_t)(crc & 0xFF);
    out[SETTINGS_CRC + 1] = (uint8_t)(crc >> 8);
}

bool bs300_settings_save(uint8_t active_prog, const uint8_t *volume)
{
    /* Reuse global work buffer (word-aligned) — avoids 960B stack alloc
     * that overflows the 1024B main stack during deferred switch. */
    extern uint8_t bs300_work_buf[BS300_TOTAL_DATA];
    uint32_t *wbuf = (uint32_t *)(void *)bs300_work_buf;
    uint8_t *buf = bs300_work_buf;

    main_flash_unlock();
    settings_build_payload(active_prog, volume, buf);

    /* Refresh watchdog — ROM flash-erase may not service it */
    Sys_Watchdog_Refresh();

    if (Flash_EraseSector(SETTINGS_BASE) != FLASH_ERR_NONE) {
        PRINTF("[BS300] settings erase FAIL\r\n");
        return false;
    }

    /* buf already at the same address as wbuf — no memcpy needed */
    if (Flash_WriteBuffer(SETTINGS_BASE, BS300_TOTAL_DATA / 4, wbuf) != FLASH_ERR_NONE) {
        PRINTF("[BS300] settings write FAIL\r\n");
        return false;
    }

    PRINTF("[BS300] settings saved prog=%u vol=[%u,%u,%u,%u]\r\n",
           active_prog,
           volume ? volume[0] : 0, volume ? volume[1] : 0,
           volume ? volume[2] : 0, volume ? volume[3] : 0);
    return true;
}

bool bs300_settings_load(uint8_t *active_prog, uint8_t *volume)
{
    const uint8_t *base = (const uint8_t *)SETTINGS_BASE;
    uint8_t magic[4];
    uint16_t stored_crc, calc_crc;

    memcpy(magic, base + SETTINGS_MAGIC, 4);
    if (memcmp(magic, SETTINGS_MAGIC_VAL, 4) != 0) {
        PRINTF("[BS300] settings not found\r\n");
        return false;
    }

    stored_crc = (uint16_t)base[SETTINGS_CRC]
               | ((uint16_t)base[SETTINGS_CRC + 1] << 8);
    calc_crc = crc16_xmodem(base, SETTINGS_CRC);
    if (stored_crc != calc_crc) {
        PRINTF("[BS300] settings CRC fail\r\n");
        return false;
    }

    if (active_prog != NULL)
        *active_prog = base[SETTINGS_ACTIVE_PROG];

    if (volume != NULL) {
        volume[0] = base[SETTINGS_VOLUME + 0];
        volume[1] = base[SETTINGS_VOLUME + 1];
        volume[2] = base[SETTINGS_VOLUME + 2];
        volume[3] = base[SETTINGS_VOLUME + 3];
    }

    PRINTF("[BS300] settings loaded prog=%u vol=[%u,%u,%u,%u]\r\n",
           active_prog ? *active_prog : 0xFF,
           volume ? volume[0] : 0, volume ? volume[1] : 0,
           volume ? volume[2] : 0, volume ? volume[3] : 0);
    return true;
}

void bs300_settings_invalidate(void)
{
    main_flash_unlock();
    Flash_EraseSector(SETTINGS_BASE);
    PRINTF("[BS300] settings invalidated\r\n");
}
