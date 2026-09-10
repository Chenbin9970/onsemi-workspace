/**
 * 7100 读回结果 Main Flash 缓存层。
 *
 * 复用 BS300 原程序区（BS300 已移除，该区空出）：
 *   0x0015D000  Program 0   2KB
 *   0x0015D800  Program 1   2KB
 *   0x0015E000  Program 2   2KB
 *   0x0015E800  Program 3   2KB
 *
 * 每程序只写一槽 864B（216 word），保存时整扇区擦除后重写。
 * 槽内布局见 include/dsp_7100_storage.h。
 */

#include "dsp_7100_storage.h"
#include "dsp_7100_init.h"
#include <rsl10.h>
#include <rsl10_flash_rom.h>
#include <string.h>
#include "app.h"
#include <printf.h>
#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

/* ---- Sector base addresses ---- */
#define CACHE_PROG0_BASE   0x0015D000
#define CACHE_PROG1_BASE   0x0015D800
#define CACHE_PROG2_BASE   0x0015E000
#define CACHE_PROG3_BASE   0x0015E800

static const uint32_t CACHE_BASE[DSP7100_CACHE_PROGS] = {
    CACHE_PROG0_BASE, CACHE_PROG1_BASE, CACHE_PROG2_BASE, CACHE_PROG3_BASE
};

/* ---- 槽布局（只存解析后的参数，51B）----
 *   [0]      denoise_en
 *   [1]      denoise_lvl
 *   [2]      dfbc_en
 *   [3..18]  wdrc_ll[16]
 *   [19..34] wdrc_hl[16]
 *   [35..50] wdrc_ol[16]
 *   [51..54] magic "D71P"
 *   [55]     version
 *   [56]     valid 0xA5
 *   [57..58] CRC16-XMODEM（覆盖 [0..50]）
 */
#define CACHE_DEN_EN_OFF   0
#define CACHE_DEN_LVL_OFF  1
#define CACHE_DFBC_EN_OFF  2
#define CACHE_LL_OFF       3
#define CACHE_HL_OFF       (CACHE_LL_OFF + DSP7100_WDRC_CH)     /* 19 */
#define CACHE_OL_OFF       (CACHE_HL_OFF + DSP7100_WDRC_CH)     /* 35 */
#define CACHE_PARAM_LEN    (CACHE_OL_OFF + DSP7100_WDRC_CH)     /* 51 */

#define CACHE_MAGIC_OFF    CACHE_PARAM_LEN                      /* 51 */
#define CACHE_VER_OFF      (CACHE_MAGIC_OFF + 4)                /* 55 */
#define CACHE_VALID_OFF    (CACHE_MAGIC_OFF + 5)                /* 56 */
#define CACHE_CRC_OFF      (CACHE_MAGIC_OFF + 6)                /* 57 */

#define CACHE_SLOT_BYTES   64                         /* 51+8=59 → 64，4 字节对齐 */
#define CACHE_SLOT_WORDS   (CACHE_SLOT_BYTES / 4)     /* 16 */

static const uint8_t CACHE_MAGIC[4] = { 'D', '7', '1', 'P' };
#define CACHE_VERSION   3    /* v3: 只存解析后的参数（v2 存原始块，格式不兼容） */
#define CACHE_VALID     0xA5

/* ---- Main Flash unlock（HIGH region 0x00150000+，同 BS300）---- */
static void main_flash_unlock(void)
{
    FLASH->MAIN_CTRL = (MAIN_HIGH_W_ENABLE
                      | MAIN_MIDDLE_W_DISABLE
                      | MAIN_LOW_W_DISABLE);
    FLASH->MAIN_WRITE_UNLOCK = FLASH_MAIN_KEY;
}

/* ---- CRC16-XMODEM (poly 0x1021)，与 BS300 存储层一致 ---- */
static uint16_t crc16_xmodem(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0;
    uint16_t i;
    uint8_t b;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (b = 0; b < 8; b++) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else              crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* 校验某程序的 flash 槽；通过则返回 payload 指针，否则 NULL。 */
static const uint8_t *cache_find_valid(uint8_t prog)
{
    const uint8_t *slot;
    uint16_t stored, calc;

    if (prog >= DSP7100_CACHE_PROGS) return NULL;
    slot = (const uint8_t *)CACHE_BASE[prog];

    if (memcmp(slot + CACHE_MAGIC_OFF, CACHE_MAGIC, 4) != 0) return NULL;
    if (slot[CACHE_VALID_OFF] != CACHE_VALID) return NULL;
    if (slot[CACHE_VER_OFF] != CACHE_VERSION) return NULL;

    stored = (uint16_t)slot[CACHE_CRC_OFF]
           | ((uint16_t)slot[CACHE_CRC_OFF + 1] << 8);
    calc = crc16_xmodem(slot, CACHE_PARAM_LEN);
    if (stored != calc) return NULL;

    return slot;
}

bool dsp_7100_cache_load(void)
{
    dsp_7100_rb_bufs_t *b = dsp_7100_rb_bufs();
    uint8_t prog;
    uint8_t hit = 0;

    for (prog = 0; prog < DSP7100_CACHE_PROGS; prog++) {
        const uint8_t *slot = cache_find_valid(prog);
        dsp_7100_prog_t *p = &b->prog[prog];

        b->valid[prog] = 0;
        if (slot == NULL) continue;

        p->denoise_en  = slot[CACHE_DEN_EN_OFF];
        p->denoise_lvl = slot[CACHE_DEN_LVL_OFF];
        p->dfbc_en     = slot[CACHE_DFBC_EN_OFF];
        memcpy(p->wdrc_ll, slot + CACHE_LL_OFF, DSP7100_WDRC_CH);
        memcpy(p->wdrc_hl, slot + CACHE_HL_OFF, DSP7100_WDRC_CH);
        memcpy(p->wdrc_ol, slot + CACHE_OL_OFF, DSP7100_WDRC_CH);
        b->valid[prog] = 1;
        hit++;
    }

    return (hit == DSP7100_CACHE_PROGS);   /* 4 个程序全命中才算成功 */
}

bool dsp_7100_cache_save(void)
{
    dsp_7100_rb_bufs_t *b = dsp_7100_rb_bufs();
    uint32_t buf[CACHE_SLOT_WORDS];
    uint8_t *sb = (uint8_t *)buf;
    uint8_t prog;
    bool all_ok = true;

    for (prog = 0; prog < DSP7100_CACHE_PROGS; prog++) {
        const dsp_7100_prog_t *p = &b->prog[prog];
        uint32_t base;
        uint16_t crc;

        if (!b->valid[prog]) {
            PRINTF("[7100-cache] prog %u 未读回，跳过\r\n", prog);
            all_ok = false;
            continue;
        }

        memset(sb, 0xFF, CACHE_SLOT_BYTES);
        sb[CACHE_DEN_EN_OFF]  = p->denoise_en;
        sb[CACHE_DEN_LVL_OFF] = p->denoise_lvl;
        sb[CACHE_DFBC_EN_OFF] = p->dfbc_en;
        memcpy(sb + CACHE_LL_OFF, p->wdrc_ll, DSP7100_WDRC_CH);
        memcpy(sb + CACHE_HL_OFF, p->wdrc_hl, DSP7100_WDRC_CH);
        memcpy(sb + CACHE_OL_OFF, p->wdrc_ol, DSP7100_WDRC_CH);

        memcpy(sb + CACHE_MAGIC_OFF, CACHE_MAGIC, 4);
        sb[CACHE_VER_OFF]   = CACHE_VERSION;
        sb[CACHE_VALID_OFF] = CACHE_VALID;
        crc = crc16_xmodem(sb, CACHE_PARAM_LEN);
        sb[CACHE_CRC_OFF]     = (uint8_t)(crc & 0xFF);
        sb[CACHE_CRC_OFF + 1] = (uint8_t)(crc >> 8);

        base = CACHE_BASE[prog];
        main_flash_unlock();

        Sys_Watchdog_Refresh();
        __disable_irq();
        if (Flash_EraseSector(base) != FLASH_ERR_NONE) {
            __enable_irq();
            PRINTF("[7100-cache] prog %u erase FAIL\r\n", prog);
            all_ok = false;
            continue;
        }
        Sys_Watchdog_Refresh();
        if (Flash_WriteBuffer(base, CACHE_SLOT_WORDS,
                              (unsigned int *)buf) != FLASH_ERR_NONE) {
            __enable_irq();
            PRINTF("[7100-cache] prog %u write FAIL\r\n", prog);
            all_ok = false;
            continue;
        }
        __enable_irq();

        PRINTF("[7100-cache] prog %u saved (crc=%04X)\r\n", prog, crc);
    }

    return all_ok;
}

void dsp_7100_cache_invalidate(void)
{
    dsp_7100_rb_bufs_t *b = dsp_7100_rb_bufs();
    uint8_t prog;

    main_flash_unlock();
    for (prog = 0; prog < DSP7100_CACHE_PROGS; prog++) {
        Sys_Watchdog_Refresh();
        __disable_irq();
        Flash_EraseSector(CACHE_BASE[prog]);
        __enable_irq();
        b->valid[prog] = 0;
    }
    PRINTF("[7100-cache] invalidated (4 sectors)\r\n");
}
