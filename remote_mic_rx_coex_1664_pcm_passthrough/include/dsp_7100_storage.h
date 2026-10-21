#ifndef DSP_7100_STORAGE_H
#define DSP_7100_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 7100 解析后参数的 Main Flash 缓存（复用 BS300 原程序区，BS300 已移除）。
 *
 * 目的：开机只需从 7100 读一次并解析；之后开机直接读 flash，不再走 I2C。
 * **只存解析后的参数，不存原始 block** —— 每程序 54B（原始 855B）。
 *
 * Layout（Main Flash 代码之后，每程序 2KB sector，只写一槽）：
 *   0x0015D000  Program 0
 *   0x0015D800  Program 1
 *   0x0015E000  Program 2
 *   0x0015E800  Program 3
 *
 * 每槽 64B（16 word）：
 *   0       denoise_en
 *   1       denoise_lvl
 *   2       dfbc_en
 *   3       eq_low          EQ 绝对值（App 上次下发）
 *   4       eq_mid
 *   5       eq_high
 *   6  ..21 wdrc_ll[16]     设备当前值（含 EQ）
 *   22 ..37 wdrc_hl[16]     设备当前值（含 EQ）
 *   38 ..53 wdrc_ol[16]
 *   54 ..57 magic "D71P"
 *   58      version (v5)
 *   59      valid (0xA5)
 *   60 ..61 CRC16-XMODEM（覆盖 0..53）
 *
 * 另有独立的 Settings sector（与上面 4 个程序 sector 无关）：
 *   0x0015F000  设置记录（RM 音频流地址），见 dsp_7100_settings_*()
 */

#define DSP7100_CACHE_PROGS      4

/* 读全部 4 个程序到 RAM。全部无效则返回 false。
 * ⚠ 2026-10-09 起调用方**总是**接着走 I2C 读回（读回结果与 flash 对比后决定覆盖），
 *   本函数不再有「命中即跳过读回」的含义。 */
bool dsp_7100_cache_load(void);

/* 把当前 RAM 中的读回结果写入 flash：**逐槽与 flash 现有内容对比，一致就跳过**
 * （不擦不写）。任一槽擦/写失败返回 false；「无差异跳过」不算失败。 */
bool dsp_7100_cache_save(void);

/* 擦除全部 4 个程序 sector（0xFE 重启重读时用）。 */
void dsp_7100_cache_invalidate(void);

/* ---- Settings 扇区（0x0015F000，2KB，整扇区擦除后写）----
 * 1664 没有 BS300 那套设置记录，这里只持久化 BLE 89 号写入的 RM 音频流地址（24 位）。
 * 保留 magic/version/valid/CRC 是为了擦写中断或布局变更时能判「无记录」并回退默认值。 */

/* 写入 RM 音频流地址（24 位）。擦除/写入任一失败返回 false。 */
bool dsp_7100_settings_save_stream_addr(uint32_t stream_addr);

/* 读回 RM 音频流地址（24 位）。无有效记录时返回 false 且不改动 *stream_addr
 * （调用方应保留默认值）。 */
bool dsp_7100_settings_load_stream_addr(uint32_t *stream_addr);

#ifdef __cplusplus
}
#endif

#endif /* DSP_7100_STORAGE_H */
