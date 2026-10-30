/**
 * 7100 读回结果 Main Flash 缓存层。
 *
 * 复用 BS300 原程序区（BS300 已移除，该区空出）：
 *   0x0015D000  Program 0   2KB
 *   0x0015D800  Program 1   2KB
 *   0x0015E000  Program 2   2KB
 *   0x0015E800  Program 3   2KB
 *
 * 每程序只写一槽 64B（16 word）；保存时按程序擦该程序的扇区后重写，
 * 与 flash 现有内容逐字节一致的槽**跳过**（不擦不写，见 dsp_7100_cache_save）。
 * 槽内布局见 include/dsp_7100_storage.h。
 *
 * 另有 Settings 扇区 0x0015F000（在链接区 380K 上界之外，代码不会压到），
 * 只存 BLE 89 号写入的 RM 音频流地址（24 位）。布局见本文件 SETS_* 宏。
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

/* ---- 槽布局（只存解析后的参数，54B）----
 *   [0]      denoise_en
 *   [1]      denoise_lvl
 *   [2]      dfbc_en
 *   [3]      eq_low   (int8, App 上次下发的绝对值 ±dB)
 *   [4]      eq_mid
 *   [5]      eq_high
 *   [6..21]  wdrc_ll[16]   **基准**（不含 EQ；App 设的绝对值）
 *   [22..37] wdrc_hl[16]   **基准**（不含 EQ）
 *   [38..53] wdrc_ol[16]
 *   [54..57] magic "D71P"
 *   [58]     version
 *   [59]     valid 0xA5
 *   [60..61] CRC16-XMODEM（覆盖 [0..53]）
 */
#define CACHE_DEN_EN_OFF   0
#define CACHE_DEN_LVL_OFF  1
#define CACHE_DFBC_EN_OFF  2
#define CACHE_EQ_LOW_OFF   3
#define CACHE_EQ_MID_OFF   4
#define CACHE_EQ_HIGH_OFF  5
#define CACHE_LL_OFF       6
#define CACHE_HL_OFF       (CACHE_LL_OFF + DSP7100_WDRC_CH)     /* 22 */
#define CACHE_OL_OFF       (CACHE_HL_OFF + DSP7100_WDRC_CH)     /* 38 */
#define CACHE_PARAM_LEN    (CACHE_OL_OFF + DSP7100_WDRC_CH)     /* 54 */

#define CACHE_MAGIC_OFF    CACHE_PARAM_LEN                      /* 54 */
#define CACHE_VER_OFF      (CACHE_MAGIC_OFF + 4)                /* 58 */
#define CACHE_VALID_OFF    (CACHE_MAGIC_OFF + 5)                /* 59 */
#define CACHE_CRC_OFF      (CACHE_MAGIC_OFF + 6)                /* 60 */

#define CACHE_SLOT_BYTES   64                         /* 54+8=62 → 64，4 字节对齐 */
#define CACHE_SLOT_WORDS   (CACHE_SLOT_BYTES / 4)     /* 16 */

static const uint8_t CACHE_MAGIC[4] = { 'D', '7', '1', 'P' };
#define CACHE_VERSION   6    /* v6: wdrc_ll/hl 恢复为**基准**（不含 EQ）；EQ 写 = 基准 + 绝对值，
                              *     读回只在槽无效时建立基准（v5 的"设备当前值"槽自动失效重读） */
#define CACHE_VALID     0xA5

/* ---- Settings 扇区（最后一个 2KB，在链接区 380K 上界 0x15F000 之外）----
 *   [0..2]  stream_addr（小端 24 位）
 *   [3..6]  magic "RMST"
 *   [7]     version
 *   [8]     valid 0xA5
 *   [9..10] CRC16-XMODEM（覆盖 [0..7]）
 *   [11..15] 0xFF 填充
 */
#define SETTINGS_BASE         0x0015F000
#define SETS_ADDR_OFF         0
#define SETS_MAGIC_OFF        3
#define SETS_VER_OFF          7
#define SETS_VALID_OFF        8
#define SETS_CRC_OFF          9
#define SETS_CRC_LEN          8                       /* CRC 覆盖 [0..7] */
#define SETS_REC_BYTES        16
#define SETS_REC_WORDS        (SETS_REC_BYTES / 4)    /* 4 */

static const uint8_t SETS_MAGIC[4] = { 'R', 'M', 'S', 'T' };
#define SETS_VERSION     1
#define SETS_VALID       0xA5

/* ---- Settings 扇区内的用户状态 append-only 槽（布局见 include/dsp_7100_storage.h）----
 * 偏移 0..15 留给上面的流地址记录，槽从 16 起（16B 对齐，正好是 4 word）。 */
#define SETTINGS_BYTES        2048
#define MEM_SLOT_OFF          16
#define MEM_SLOT_BYTES        16
#define MEM_SLOT_WORDS        (MEM_SLOT_BYTES / 4)                            /* 4 */
#define MEM_SLOT_MAX          ((SETTINGS_BYTES - MEM_SLOT_OFF) / MEM_SLOT_BYTES) /* 127 */

#define MEM_PROG_OFF          0
#define MEM_VOL_OFF           1
#define MEM_MAGIC_OFF         2
#define MEM_VER_OFF           6
#define MEM_VALID_OFF         7
#define MEM_CRC_OFF           8
#define MEM_CRC_LEN           8
#define MEM_VERSION           1
#define MEM_VALID             0xA5

static const uint8_t MEM_MAGIC[4] = { 'D', '7', '1', 'M' };

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

/* 校验 Settings 扇区里的流地址记录（magic / version / valid / CRC）。 */
static bool stream_record_valid(const uint8_t *rec)
{
    uint16_t stored, calc;

    if (memcmp(rec + SETS_MAGIC_OFF, SETS_MAGIC, 4) != 0) return false;
    if (rec[SETS_VALID_OFF] != SETS_VALID) return false;
    if (rec[SETS_VER_OFF] != SETS_VERSION) return false;

    stored = (uint16_t)rec[SETS_CRC_OFF]
           | ((uint16_t)rec[SETS_CRC_OFF + 1] << 8);
    calc = crc16_xmodem(rec, SETS_CRC_LEN);
    return (stored == calc);
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
        p->eq_low      = (int8_t)slot[CACHE_EQ_LOW_OFF];
        p->eq_mid      = (int8_t)slot[CACHE_EQ_MID_OFF];
        p->eq_high     = (int8_t)slot[CACHE_EQ_HIGH_OFF];
        memcpy(p->wdrc_ll, slot + CACHE_LL_OFF, DSP7100_WDRC_CH);
        memcpy(p->wdrc_hl, slot + CACHE_HL_OFF, DSP7100_WDRC_CH);
        memcpy(p->wdrc_ol, slot + CACHE_OL_OFF, DSP7100_WDRC_CH);
        b->valid[prog] = 1;
        hit++;
    }

    return (hit == DSP7100_CACHE_PROGS);   /* 4 个程序全命中才算成功 */
}

/* 把 RAM 里的读回结果写进 flash：**逐槽与 flash 现有内容对比，一致就跳过**
 * （2026-10-09，配合「每次开机都读回」——无差异时一次擦写都不做）。
 * 比的是整槽 64B（含 magic/ver/valid/CRC）：比 CRC16 严（CRC 有碰撞、memcmp 没有），
 * 且槽本来就要在 RAM 里打包好，零新增状态。槽无效/空（全 0xFF）时必然不等 → 照写。 */
bool dsp_7100_cache_save(void)
{
    dsp_7100_rb_bufs_t *b = dsp_7100_rb_bufs();
    uint32_t buf[CACHE_SLOT_WORDS];
    uint8_t *sb = (uint8_t *)buf;
    uint8_t prog;
    uint8_t written = 0;
    uint8_t skipped = 0;
    uint8_t failed  = 0;
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
        sb[CACHE_EQ_LOW_OFF]  = (uint8_t)p->eq_low;
        sb[CACHE_EQ_MID_OFF]  = (uint8_t)p->eq_mid;
        sb[CACHE_EQ_HIGH_OFF] = (uint8_t)p->eq_high;
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

        if (memcmp((const void *)base, sb, CACHE_SLOT_BYTES) == 0) {
            skipped++;
            PRINTF("[7100-cache] prog %u 与 flash 一致，跳过\r\n", prog);
            continue;
        }

        main_flash_unlock();

        Sys_Watchdog_Refresh();
        __disable_irq();
        if (Flash_EraseSector(base) != FLASH_ERR_NONE) {
            __enable_irq();
            PRINTF("[7100-cache] prog %u erase FAIL\r\n", prog);
            all_ok = false;
            failed++;
            continue;
        }
        Sys_Watchdog_Refresh();
        if (Flash_WriteBuffer(base, CACHE_SLOT_WORDS,
                              (unsigned int *)buf) != FLASH_ERR_NONE) {
            __enable_irq();
            PRINTF("[7100-cache] prog %u write FAIL\r\n", prog);
            all_ok = false;
            failed++;
            continue;
        }
        __enable_irq();

        written++;
        PRINTF("[7100-cache] prog %u saved (crc=%04X)\r\n", prog, crc);
    }

    PRINTF("[7100-cache] done: 覆盖 %u 槽 / 跳过 %u 槽 / 失败 %u\r\n",
           written, skipped, failed);

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

/* ---- Settings 扇区：RM 音频流地址 ---- */

bool dsp_7100_settings_save_stream_addr(uint32_t stream_addr)
{
    uint32_t buf[SETS_REC_WORDS];
    uint8_t *sb = (uint8_t *)buf;
    uint16_t crc;
    uint8_t  mem_prog = 0;
    uint8_t  mem_vol  = 0;
    bool     mem_ok;

    /* 本扇区同时存用户状态槽，擦扇区会连带清掉 —— 先读出来，写完再补回 */
    mem_ok = dsp_7100_settings_load_user_state(&mem_prog, &mem_vol);

    memset(sb, 0xFF, SETS_REC_BYTES);
    sb[SETS_ADDR_OFF]     = (uint8_t)(stream_addr & 0xFF);
    sb[SETS_ADDR_OFF + 1] = (uint8_t)((stream_addr >> 8) & 0xFF);
    sb[SETS_ADDR_OFF + 2] = (uint8_t)((stream_addr >> 16) & 0xFF);
    memcpy(sb + SETS_MAGIC_OFF, SETS_MAGIC, 4);
    sb[SETS_VER_OFF]   = SETS_VERSION;
    sb[SETS_VALID_OFF] = SETS_VALID;
    crc = crc16_xmodem(sb, SETS_CRC_LEN);
    sb[SETS_CRC_OFF]     = (uint8_t)(crc & 0xFF);
    sb[SETS_CRC_OFF + 1] = (uint8_t)(crc >> 8);

    main_flash_unlock();

    Sys_Watchdog_Refresh();
    __disable_irq();
    if (Flash_EraseSector(SETTINGS_BASE) != FLASH_ERR_NONE) {
        __enable_irq();
        PRINTF("[7100-settings] erase FAIL\r\n");
        return false;
    }
    Sys_Watchdog_Refresh();
    if (Flash_WriteBuffer(SETTINGS_BASE, SETS_REC_WORDS,
                          (unsigned int *)buf) != FLASH_ERR_NONE) {
        __enable_irq();
        PRINTF("[7100-settings] stream addr write FAIL\r\n");
        return false;
    }
    __enable_irq();

    if (mem_ok && !dsp_7100_settings_save_user_state(mem_prog, mem_vol)) {
        PRINTF("[7100-settings] user state restore FAIL\r\n");
    }

    PRINTF("[7100-settings] stream addr saved (crc=%04X)\r\n", crc);
    return true;
}

bool dsp_7100_settings_load_stream_addr(uint32_t *stream_addr)
{
    const uint8_t *rec = (const uint8_t *)SETTINGS_BASE;

    if (stream_addr == NULL) return false;
    if (!stream_record_valid(rec)) return false;

    *stream_addr = (uint32_t)rec[SETS_ADDR_OFF]
                 | ((uint32_t)rec[SETS_ADDR_OFF + 1] << 8)
                 | ((uint32_t)rec[SETS_ADDR_OFF + 2] << 16);
    return true;
}

/* ---- 用户状态记忆（当前程序号 / 音量档位）---- */

static const uint8_t *mem_slot(uint8_t idx)
{
    return (const uint8_t *)(SETTINGS_BASE + MEM_SLOT_OFF
                             + (uint16_t)idx * MEM_SLOT_BYTES);
}

/* 槽是否是一条完整有效的记忆记录（含范围检查）。 */
static bool mem_slot_valid(const uint8_t *rec)
{
    uint16_t stored, calc;

    if (memcmp(rec + MEM_MAGIC_OFF, MEM_MAGIC, 4) != 0) return false;
    if (rec[MEM_VALID_OFF] != MEM_VALID) return false;
    if (rec[MEM_VER_OFF] != MEM_VERSION) return false;

    stored = (uint16_t)rec[MEM_CRC_OFF]
           | ((uint16_t)rec[MEM_CRC_OFF + 1] << 8);
    calc = crc16_xmodem(rec, MEM_CRC_LEN);
    if (stored != calc) return false;

    if (rec[MEM_PROG_OFF] < 1 || rec[MEM_PROG_OFF] > DSP7100_MEM_PROG_MAX) return false;
    if (rec[MEM_VOL_OFF] > DSP7100_MEM_VOL_MAX) return false;
    return true;
}

/* 槽是否从未写过（整槽 0xFF）。写过一半的坏槽跳过，不覆盖。 */
static bool mem_slot_empty(const uint8_t *rec)
{
    uint8_t i;

    for (i = 0; i < MEM_SLOT_BYTES; i++) {
        if (rec[i] != 0xFF) return false;
    }
    return true;
}

/* 最新一条有效槽号；没有返回 -1。 */
static int mem_find_latest(void)
{
    int slot;

    for (slot = (int)MEM_SLOT_MAX - 1; slot >= 0; slot--) {
        if (mem_slot_valid(mem_slot((uint8_t)slot))) return slot;
    }
    return -1;
}

/* 第一条空槽号；全部写满返回 0xFF。 */
static uint8_t mem_find_empty(void)
{
    uint8_t slot;

    for (slot = 0; slot < MEM_SLOT_MAX; slot++) {
        if (mem_slot_empty(mem_slot(slot))) return slot;
    }
    return 0xFF;
}

static void mem_build_record(uint8_t *buf, uint8_t prog, uint8_t vol)
{
    uint16_t crc;

    memset(buf, 0xFF, MEM_SLOT_BYTES);
    buf[MEM_PROG_OFF] = prog;
    buf[MEM_VOL_OFF]  = vol;
    memcpy(buf + MEM_MAGIC_OFF, MEM_MAGIC, 4);
    buf[MEM_VER_OFF]   = MEM_VERSION;
    buf[MEM_VALID_OFF] = MEM_VALID;
    crc = crc16_xmodem(buf, MEM_CRC_LEN);
    buf[MEM_CRC_OFF]     = (uint8_t)(crc & 0xFF);
    buf[MEM_CRC_OFF + 1] = (uint8_t)(crc >> 8);
}

/* 写一个 16B 槽（4 word）。调用方保证该槽是擦除态。 */
static bool mem_write_slot(uint8_t idx, uint32_t *buf)
{
    uint32_t addr = (uint32_t)(SETTINGS_BASE + MEM_SLOT_OFF
                               + (uint16_t)idx * MEM_SLOT_BYTES);

    main_flash_unlock();
    Sys_Watchdog_Refresh();
    __disable_irq();
    if (Flash_WriteBuffer(addr, MEM_SLOT_WORDS, (unsigned int *)buf) != FLASH_ERR_NONE) {
        __enable_irq();
        PRINTF("[7100-mem] slot %u write FAIL\r\n", (unsigned)idx);
        return false;
    }
    __enable_irq();
    return true;
}

/* 槽写满：整扇区擦除 → 补写流地址记录（原来有才补）→ 把本槽写到 0 号。 */
static bool mem_sector_recycle(uint8_t prog, uint8_t vol)
{
    uint32_t stream_buf[SETS_REC_WORDS];
    uint32_t buf[MEM_SLOT_WORDS];
    bool     stream_ok;

    stream_ok = stream_record_valid((const uint8_t *)SETTINGS_BASE);
    if (stream_ok) {
        memcpy(stream_buf, (const void *)SETTINGS_BASE, SETS_REC_BYTES);
    }

    main_flash_unlock();
    Sys_Watchdog_Refresh();
    __disable_irq();
    if (Flash_EraseSector(SETTINGS_BASE) != FLASH_ERR_NONE) {
        __enable_irq();
        PRINTF("[7100-mem] sector erase FAIL\r\n");
        return false;
    }
    if (stream_ok &&
        Flash_WriteBuffer(SETTINGS_BASE, SETS_REC_WORDS, stream_buf) != FLASH_ERR_NONE) {
        __enable_irq();
        PRINTF("[7100-mem] stream addr restore FAIL\r\n");
        return false;
    }
    __enable_irq();

    mem_build_record((uint8_t *)buf, prog, vol);
    if (!mem_write_slot(0, buf)) return false;

    PRINTF("[7100-mem] slots full -> sector erased (stream addr %s), saved prog=%u vol=%u\r\n",
           stream_ok ? "restored" : "none", prog, vol);
    return true;
}

bool dsp_7100_settings_load_user_state(uint8_t *prog, uint8_t *vol)
{
    int            last;
    const uint8_t *rec;

    if (prog == NULL || vol == NULL) return false;

    last = mem_find_latest();
    if (last < 0) return false;

    rec   = mem_slot((uint8_t)last);
    *prog = rec[MEM_PROG_OFF];
    *vol  = rec[MEM_VOL_OFF];
    return true;
}

bool dsp_7100_settings_save_user_state(uint8_t prog, uint8_t vol)
{
    uint32_t buf[MEM_SLOT_WORDS];
    uint8_t  slot;
    int      last;

    if (prog < 1 || prog > DSP7100_MEM_PROG_MAX) return false;
    if (vol > DSP7100_MEM_VOL_MAX) return false;

    /* 与最新一条相同 → 不写（开机恢复重发不会重复落盘） */
    last = mem_find_latest();
    if (last >= 0) {
        const uint8_t *rec = mem_slot((uint8_t)last);

        if (rec[MEM_PROG_OFF] == prog && rec[MEM_VOL_OFF] == vol) return true;
    }

    slot = mem_find_empty();
    if (slot == 0xFF) return mem_sector_recycle(prog, vol);

    mem_build_record((uint8_t *)buf, prog, vol);
    if (!mem_write_slot(slot, buf)) return false;

    PRINTF("[7100-mem] saved prog=%u vol=%u slot=%u\r\n",
           prog, vol, (unsigned)slot);
    return true;
}
