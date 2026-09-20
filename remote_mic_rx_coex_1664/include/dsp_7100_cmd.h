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
 * 启动返回 true = 已受理，实际完成看日志 [7100] --- session done ok=? ---。
 * 命令数：降噪/DFBC = 8（16 tick ≈ 3.2s）；EQ = 14（28 tick ≈ 5.6s）。 */

/* 降噪档位：prog 1-4，level 0-4。返回 true = 会话已启动。 */
bool dsp_7100_set_denoise(uint8_t prog, uint8_t level);

/* DFBC 开关：prog 1-4，onoff 0/1。返回 true = 会话已启动。 */
bool dsp_7100_set_dfbc(uint8_t prog, uint8_t onoff);

/* 三段均衡器：prog 1-4，band 0=低音 1=中音 2=高音，db = App 下发的**绝对值**（±dB，1dB/LSB）。
 * 写入值 = 设备当前值 + (本次绝对值 − 上次保存的绝对值)，作用于该段通道的
 * LowLevelGain 与 HighLevelGain；写入后的值落盘，下次据此算差值。
 * 通道映射（数组下标）：低音 {1,2} ｜ 中音 {3,4} ｜ 高音 {6,7}。
 * db 超出 ±10 会被钳位。返回 true = 会话已启动。
 * ⚠ 会话时长（一条命令跨 2 个 tick = 400ms）：每段 2 通道 = 14 命令 / 5.6s，
 *   全程静音（mute → 写 → 解除），属已知取舍。 */
bool dsp_7100_set_eq(uint8_t prog, uint8_t band, int8_t db);

/* 一个 WDRC 设置项：ch 1-16，val = **7100 侧 dB 绝对值** */
typedef struct
{
    uint8_t ch;
    int8_t  val;
} dsp_7100_wdrc_item_t;

/* ---- WDRC 参数（LL / HL / OL），异步会话，骨架同降噪 ----
 * items[0..n-1] 每项一个通道一个值；n=1 即单通道。
 * **多通道共用一次 静音 / 选程序 / 提交 / 解除静音**，每通道只多发"选块 + 写值"两条命令。
 *
 * val 是 7100 侧 dB 绝对值（**不是增量**，与 set_eq 不同）；瑞听侧的换算在 BLE 层做：
 *   LowLevelGain  ：-30 .. 60         （瑞听 SetGain 0-90 → 减 30）
 *   HighLevelGain ：-30 .. 60         （瑞听 SetHighLevelGain 0-90 → 减 30）
 *   OutputLimit   ：-60 .. 0          （瑞听 SetMPO 0-60 → 减 60）
 * 越界会被钳位（同 set_eq 的风格）。prog 1-4，n 1-16。
 * ⚠ prog 是设备侧程序号 1-4，不是 App 侧 scene 0-3（scene + 1 = prog）。
 * 返回 true = 会话已受理；完成情况看日志 [7100] --- session done ok=? ---。
 * 会话时长 = (6 + 2n) 条命令 × 2 tick × 200ms：n=1 约 3.2s，n=16 约 **15.2s**（全程静音）。
 * 详见 docs/7100协议/瑞听设置指令.md、docs/7100协议/WDRC/7100_WDRC设置.md。 */
bool dsp_7100_set_low_level_gain (uint8_t prog, const dsp_7100_wdrc_item_t *items, uint8_t n);
bool dsp_7100_set_high_level_gain(uint8_t prog, const dsp_7100_wdrc_item_t *items, uint8_t n);
bool dsp_7100_set_output_limit   (uint8_t prog, const dsp_7100_wdrc_item_t *items, uint8_t n);

/* ---- 纯音（测听）----
 * 出音 / 停音各是一条 A7 写帧，**没有 mute / 选程序 / commit**，与 WDRC 那套会话不同。
 * 仍走同一套 tick 机制（1 条命令 = 2 tick ≈ 0.4s）。
 *
 *   db     : 20 .. 100（协议文档规定，步进 5）
 *   freq_hz: 需是**已标定的频点**，目前只有 500/1000/2000/3000/4000/6000 Hz；
 *            其余频点返回 false（未标定，见 docs/7100协议/纯音测听.md）
 * 返回 true = 已受理（异步）。停音时 freq/db 无意义。
 * ⚠ 电平表由抓包反解得到，`K=4.39902` 可能是**整机校准常数**，换机需重新标定。 */
bool dsp_7100_play_tone(uint16_t freq_hz, uint8_t db);
bool dsp_7100_stop_tone(void);

/* 静音 / 解除静音：单命令会话（`A7 01 00 00 00 25` / `26`），无公共帧。
 * 对应 BLE 的 SetMuteData (21)。返回 true = 已受理（异步）。
 * ⚠ 7100 的独立静音命令是按抓包推的（抓包里 25/26 总在会话内部），单独发是否生效待上板确认。 */
bool dsp_7100_set_mute(bool mute);

/* 测听模式寄存器 `0x2E`（阻塞）：on → `A2 00 2E 00`，off → `A2 00 2E 58`。
 * 进测听前要置 on、退出后置 off（抓包 tonestar/tonestop）。 */
bool dsp_7100_set_tone_mode(bool on);

/* 会话是否进行中（进行中时应暂停读回/心跳，避免抢 I2C） */
bool dsp_7100_cmd_busy(void);

/* 200ms tick 调：推进一条命令 */
void dsp_7100_cmd_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* DSP_7100_CMD_H */
