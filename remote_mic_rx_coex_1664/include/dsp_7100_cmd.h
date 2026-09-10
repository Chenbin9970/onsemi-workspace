#ifndef DSP_7100_CMD_H
#define DSP_7100_CMD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 7100 运行时命令（切程序 / 调音量），移植自 peripheral_server_sleep7160test。
 * 波形解析（program1-4.csv / volume.csv）：
 *   写帧  A2 00 <reg> <val>        （reg: 16=程序, 12=音量）
 *   读回  43 03 00 00 <reg> <val>   （从机确认）
 *   结束  82
 * 调音量 = 写帧 → 读确认 → 82（3 帧）
 * 切程序 = 写帧 → 读确认 → 82 → 读状态 → 82（5 帧，对照 program1 波形）
 *
 * 程序号 1-4；音量档位 1-6（映射 0x11/0x21/0x32/0x43/0x53/0x64）。
 * 均为阻塞调用（内含 ms 级延时），须在主循环上下文调用。 */

/* 切程序 N（1-4），完整序列，成功返回 true */
bool dsp_7100_switch_program(uint8_t prog);

/* 调音量档位 L（1-6），完整序列，成功返回 true */
bool dsp_7100_set_volume(uint8_t level);

/* 当前程序 / 当前音量档位（供查询/推送使用） */
uint8_t dsp_7100_get_program(void);
uint8_t dsp_7100_get_volume_level(void);

/* ---- 降噪 / DFBC 设置（异步，照 remote_mic_rx_coex 已验证的 tick 模型）----
 * 命令表 + 200ms tick，一条命令一个 tick：发命令 → 读 3B 应答 → 写 04 82 → 下一条。
 * 会话骨架（与 rx_coex 写会话一致，无 0x03 状态读）：
 *   静音 → 选程序 → 写块准备 → 写块 → confirm → 解除静音 → 选回程序0 → commit
 * 由 APP_7100_HB_Handler 每 tick 调 dsp_7100_cmd_tick() 推进。
 * 启动返回 true = 已受理，实际完成看日志 [7100] --- session done ok=? ---。 */

/* 降噪档位：prog 1-4，level 0-4。返回 true = 会话已启动。 */
bool dsp_7100_set_denoise(uint8_t prog, uint8_t level);

/* DFBC 开关：prog 1-4，onoff 0/1。返回 true = 会话已启动。 */
bool dsp_7100_set_dfbc(uint8_t prog, uint8_t onoff);

/* 会话是否进行中（进行中时应暂停读回/心跳，避免抢 I2C） */
bool dsp_7100_cmd_busy(void);

/* 200ms tick 调：推进一条命令 */
void dsp_7100_cmd_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* DSP_7100_CMD_H */
