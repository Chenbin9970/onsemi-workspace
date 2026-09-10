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
 * **只存解析后的参数，不存原始 block** —— 每程序 51B（原始 855B）。
 *
 * Layout（Main Flash 代码之后，每程序 2KB sector，只写一槽）：
 *   0x0015D000  Program 0
 *   0x0015D800  Program 1
 *   0x0015E000  Program 2
 *   0x0015E800  Program 3
 *
 * 每槽 64B（16 word）：
 *   0   denoise_en
 *   1   denoise_lvl
 *   2   dfbc_en
 *   3   ..18   wdrc_ll[16]
 *   19  ..34   wdrc_hl[16]
 *   35  ..50   wdrc_ol[16]
 *   51  ..54   magic "D71P"
 *   55         version (v3)
 *   56         valid (0xA5)
 *   57  ..58   CRC16-XMODEM（覆盖 0..50）
 */

#define DSP7100_CACHE_PROGS      4

/* 读全部 4 个程序到 RAM。全部无效则返回 false（调用方应改走 I2C 读回）。 */
bool dsp_7100_cache_load(void);

/* 把当前 RAM 中的读回结果全部写入 flash。任一程序写失败返回 false。 */
bool dsp_7100_cache_save(void);

/* 擦除全部 4 个程序 sector（0xFE 重启重读时用）。 */
void dsp_7100_cache_invalidate(void);

#ifdef __cplusplus
}
#endif

#endif /* DSP_7100_STORAGE_H */
