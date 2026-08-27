#ifndef DSP_7100_CMD_H
#define DSP_7100_CMD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 7100 运行时命令（切程序 / 调音量）。
 * 波形解析：写帧 A2 00 <reg> <val> → 读确认(6B) → 结束帧 82。
 * 程序号 1-4；音量档位 1-6（映射 0x11/0x21/0x32/0x43/0x53/0x64）。 */

/* 切程序 N（1-4），完整序列，成功返回 true */
bool dsp_7100_switch_program(uint8_t prog);

/* 调音量档位 L（1-6），完整序列，成功返回 true */
bool dsp_7100_set_volume(uint8_t level);

/* 当前程序 / 当前音量档位（供查询/推送使用） */
uint8_t dsp_7100_get_program(void);
uint8_t dsp_7100_get_volume_level(void);

#ifdef __cplusplus
}
#endif

#endif /* DSP_7100_CMD_H */
