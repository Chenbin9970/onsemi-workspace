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
 *   6  ..21 wdrc_ll[16]     EQ 基准（不含 EQ）
 *   22 ..37 wdrc_hl[16]     EQ 基准（不含 EQ）
 *   38 ..53 wdrc_ol[16]
 *   54 ..57 magic "D71P"
 *   58      version (v6)
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
 * 1664 没有 BS300 那套设置记录，这里持久化 BLE 89 号写入的 RM 音频流地址（24 位），
 * 以及下方用户状态记忆槽。
 * 保留 magic/version/valid/CRC 是为了擦写中断或布局变更时能判「无记录」并回退默认值。 */

/* 写入 RM 音频流地址（24 位）。擦除/写入任一失败返回 false。
 * ⚠ 本操作擦整扇区：擦前会把最新的用户状态记录读进 RAM，写完补回（否则记忆被清）。 */
bool dsp_7100_settings_save_stream_addr(uint32_t stream_addr);

/* 读回 RM 音频流地址（24 位）。无有效记录时返回 false 且不改动 *stream_addr
 * （调用方应保留默认值）。 */
bool dsp_7100_settings_load_stream_addr(uint32_t *stream_addr);

/* ---- 用户状态记忆：当前程序号 / 音量档位（2026-10-30）----
 * 7100 侧「掉电保存 Volume / Memory」已关闭，改由 RSL10 存 flash。
 * 与流地址记录共用 Settings 扇区：偏移 0..15 仍是流地址记录（格式不变），
 * 16..2047 划成 127 个 16B append-only 槽；每次保存只写一个槽（不擦扇区），
 * 写满 127 个才整扇区擦一次（擦后补写流地址记录）。
 *
 * 槽布局（16B，未写过的槽为全 0xFF）：
 *   [0]      prog (1-4)
 *   [1]      vol  (0-6，与 App 档位同号)
 *   [2..5]   magic "D71M"
 *   [6]      version 1
 *   [7]      valid 0xA5
 *   [8..9]   CRC16-XMODEM（覆盖 [0..7]）
 *   [10..15] 0xFF
 * 读取时从最后一槽往前扫，取最新有效槽（槽按写入顺序递增）。 */
#define DSP7100_MEM_PROG_MAX   4
#define DSP7100_MEM_VOL_MAX    6

/* 读最新一条用户状态记录。无有效记录返回 false 且不改动 *prog / *vol。 */
bool dsp_7100_settings_load_user_state(uint8_t *prog, uint8_t *vol);

/* 追加写入一条用户状态记录（与最新记录相同则直接返回 true，不写）。
 * 槽满时整扇区擦除：擦前把流地址记录读进 RAM，擦后补写流地址再写本槽。
 * 擦除/写入失败返回 false。 */
bool dsp_7100_settings_save_user_state(uint8_t prog, uint8_t vol);

#ifdef __cplusplus
}
#endif

#endif /* DSP_7100_STORAGE_H */
