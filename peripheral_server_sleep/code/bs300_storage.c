/**
 * BS300 NVR3 Storage Layer
 *
 * Stores 4 programs' raw 480B flash data in RSL10 NVR3 (0x00081000, 2KB).
 * Incremental write: erase → write_program × 4 → finalize.
 * Per-program read: load_program(idx) does a single 480B memcpy from NVR3.
 *
 * NVR3 layout (2KB):
 *   Offset 0:     Program 0 raw data (480B)
 *   Offset 480:   Program 1 raw data (480B)
 *   Offset 960:   Program 2 raw data (480B)
 *   Offset 1440:  Program 3 raw data (480B)
 *   Offset 1920:  Magic "BS30" (4B) + Version (2B) + CRC16 (2B)
 */

#include "bs300_storage.h"
#include <rsl10.h>
#include <rsl10_flash_rom.h>
#include <string.h>
#include "app.h"

#define HEADER_OFFSET           1920
#define HEADER_SIZE             8
#define PROG_OFFSET(idx)        ((idx) * BS300_TOTAL_DATA)

static const uint8_t STORAGE_MAGIC[4] = { 'B', 'S', '3', '0' };
#define STORAGE_VERSION  1

static uint8_t s_written_mask;  /* bitmask: bit i set = program i written */

/* ============================================================
 * CRC16 (XMODEM, polynomial 0x1021)
 * ============================================================ */

static uint16_t crc16_xmodem(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* ============================================================
 * NVR3 unlock
 * ============================================================ */

static void nvr3_unlock(void)
{
    FLASH->NVR_CTRL = (NVR1_WRITE_DISABLE | NVR2_WRITE_DISABLE | NVR3_WRITE_ENABLE);
    FLASH->NVR_WRITE_UNLOCK = FLASH_NVR_KEY;
}

/* ============================================================
 * Public API
 * ============================================================ */

bool bs300_storage_erase(void)
{
    nvr3_unlock();
    s_written_mask = 0;
    return Flash_EraseSector(FLASH_NVR3_BASE) == FLASH_ERR_NONE;
}

bool bs300_storage_write_program(uint8_t idx, const uint8_t *data)
{
    if (idx > 3) return false;

    /* 480 bytes = 120 words (even), must be uint32_t-aligned */
    static uint32_t buf[BS300_TOTAL_DATA / 4];
    memcpy(buf, data, BS300_TOTAL_DATA);

    if (Flash_WriteBuffer(FLASH_NVR3_BASE + PROG_OFFSET(idx),
                          BS300_TOTAL_DATA / 4, buf) != FLASH_ERR_NONE)
        return false;

    s_written_mask |= (1 << idx);
    return true;
}

bool bs300_storage_finalize(void)
{
    if (s_written_mask != 0x0F) return false;  /* all 4 programs required */

    /* Compute CRC16 over all 4 programs in NVR3 */
    const uint8_t *nvr = (const uint8_t *)FLASH_NVR3_BASE;
    uint16_t crc = 0;
    for (uint8_t i = 0; i < 4; i++)
        crc = crc16_xmodem(nvr + PROG_OFFSET(i), BS300_TOTAL_DATA);

    /* Write header: magic[4] + version[2] + crc16[2] */
    uint8_t header[HEADER_SIZE];
    memcpy(header, STORAGE_MAGIC, 4);
    header[4] = STORAGE_VERSION;
    header[5] = 0;
    header[6] = (uint8_t)(crc & 0xFF);
    header[7] = (uint8_t)(crc >> 8);

    static uint32_t hdr_buf[2];
    memcpy(hdr_buf, header, HEADER_SIZE);

    if (Flash_WriteBuffer(FLASH_NVR3_BASE + HEADER_OFFSET, 2, hdr_buf)
        != FLASH_ERR_NONE)
        return false;

    PRINTF("BS300: NVR3 finalized (CRC=%04X)\r\n", crc);
    return true;
}

void bs300_storage_load_program(uint8_t idx, uint8_t *data_out)
{
    if (idx > 3) return;
    const uint8_t *nvr = (const uint8_t *)FLASH_NVR3_BASE;
    memcpy(data_out, nvr + PROG_OFFSET(idx), BS300_TOTAL_DATA);
}

bool bs300_storage_is_valid(void)
{
    const uint8_t *nvr = (const uint8_t *)FLASH_NVR3_BASE;

    if (memcmp(nvr + HEADER_OFFSET, STORAGE_MAGIC, 4) != 0)
        return false;

    uint16_t stored_crc = (uint16_t)nvr[HEADER_OFFSET + 6]
                        | ((uint16_t)nvr[HEADER_OFFSET + 7] << 8);
    uint16_t calc_crc = 0;
    for (uint8_t i = 0; i < 4; i++)
        calc_crc = crc16_xmodem(nvr + PROG_OFFSET(i), BS300_TOTAL_DATA);

    return stored_crc == calc_crc;
}

void bs300_storage_invalidate(void)
{
    nvr3_unlock();
    uint32_t zero[2] = { 0, 0 };
    Flash_WriteBuffer(FLASH_NVR3_BASE + HEADER_OFFSET, 2, zero);
}
