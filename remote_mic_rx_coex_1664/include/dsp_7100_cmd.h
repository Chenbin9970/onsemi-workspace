#ifndef DSP_7100_CMD_H
#define DSP_7100_CMD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 7100 运行时命令（切程序 / 调音量），移植自 peripheral_server_sleep7160test。
 * 抓包波形（program1-4.csv / volume.csv）里是：
 *   写帧  A2 00 <reg> <val>        （reg: 16=程序, 12=音量）
 *   读回  43 03 00 00 <reg> <val>   （从机确认）
 *   结束  82
 * 调音量 = 写帧 → 读确认 → 82（3 帧）
 * 切程序 = 写帧 → 读确认 → 82 → 读状态 → 82（5 帧，对照 program1 波形）
 *
 * ⚠ 2026-09-30 起**只发写帧 + 82，读确认/读状态全部不发**（2 帧）。
 *   原因：A2 应答格式在本工程从未验证过 —— 实测写完 77ms 时 7100 回 `65 01 00`
 *   （未就绪签名），而固定长度盲读还会把它后面的事务读错位
 *   （读到 `28 65 01 00 28 43`）。读不出可信的东西，不如不读。
 *   82 保留：它是实验证过的解锁字节（漏发会让下一条拿不到 46）。
 *   → 代价明确：命令**不再被校验**，ok=1 只代表 I2C 写成功，不代表 7100 照做了。
 *   ⚠ 测听进入 / 退出是多命令会话（3 条，见下），且保留了写帧后的固定延时。
 *
 * 程序号 1-4；音量档位 1-6（映射 0x11/0x21/0x32/0x43/0x53/0x64）。
 *
 * **全部异步**（2026-09-30 起）：与读回同一套推进模型 —— 主循环单点
 * dsp_7100_cmd_poll() 在 DIO13 边沿上推进，200ms tick 只做兜底。
 * 返回 true = 已塞进队列（**不等于已完成**），完成情况看日志
 * [7100] --- session done ok=? ---。同一时刻只允许一个会话，占用期间
 * dsp_7100_cmd_busy() 为真、读回停摆。 */

/* 切程序 N（1-4）。返回 true = 已受理。 */
bool dsp_7100_switch_program(uint8_t prog);

/* 调音量档位 L（1-6）。返回 true = 已受理。 */
bool dsp_7100_set_volume(uint8_t level);

/* 当前程序 / 当前音量档位（供查询/推送使用） */
uint8_t dsp_7100_get_program(void);
uint8_t dsp_7100_get_volume_level(void);

/* ---- 降噪 / DFBC 设置（异步，与读回同一套推进模型）----
 * 命令表 + DIO13 边沿推进，一条命令 = 写帧 → 读 3B 应答 → 写 04 82。
 * 会话骨架（与 rx_coex 写会话一致，无 0x03 状态读）：
 *   静音 → 选程序 → 写块准备 → 写块 → confirm → 解除静音 → 选回程序0 → commit
 * 由主循环的 dsp_7100_cmd_poll() 推进（200ms tick 兜底加速不了的部分）。
 * 启动返回 true = 已受理，实际完成看日志 [7100] --- session done ok=? ---。
 * ⚠ 2026-09-30 起节奏由 200ms tick 改为 DIO13 边沿：原来一条命令要 400ms
 * （跨 2 tick），现在一条 ~2 个边沿间隔，WDRC n=16 从 15.2s 掉到几秒。
 *
 * ⚠ 2026-09-30 **DFBC 单独走变体**（§23）：8 条命令**不再读那 3B 应答**，
 * 改成「写帧 → 等 DIO13 上升沿 → 直接 `04 82` → 等下降沿 → 下一条」，
 * 边沿等不到仍由 200ms tick 兜底。骨架、命令字节、条数都不变，
 * 只是中间少一次读 —— 所以**总时长基本不变**（快慢由边沿间隔决定，不由那次读决定）。
 * 日志里 DFBC 会话**不再出现 `[7100] R` 行**。
 * 降噪 / WDRC / EQ 本轮**未改**，仍读应答；验证通过后再推到它们。
 *
 * ⚠⚠ 2026-09-30 上板实测（开发文档 §23.6）**推翻了「等回铃」这半截**：
 *   **写帧产生零边沿**，DIO13 只在 `82` 之后 0~2ms 出现一对 rise+fall。
 *   于是「等写帧后的上升沿再发 82」永远等不到 —— 每步退到 200ms tick 兜底，
 *   会话 8 步 = 8 tick = **1430ms**，静音窗口 **810ms**（用户可感知）。
 *   「等下降沿再发下一条」那半截是好的（0~2ms）。
 *   → 待办（开发文档 §23.7 方案 A）：把 wait_rise 去掉，改成「写完立刻 82」，
 *     会话约 40ms。**改之前先用耳朵确认 DFBC 开关真的生效。** */

/* 降噪档位：prog 1-4，level 0-4。返回 true = 会话已启动。 */
bool dsp_7100_set_denoise(uint8_t prog, uint8_t level);

/* DFBC 开关：prog 1-4，onoff 0/1。返回 true = 会话已启动。 */
bool dsp_7100_set_dfbc(uint8_t prog, uint8_t onoff);

/* 三段均衡器：prog 1-4，band 0=低音 1=中音 2=高音，db = App 下发的**绝对值**（±dB，1dB/LSB）。
 * 写入值 = 设备当前值 + (本次绝对值 − 上次保存的绝对值)，作用于该段通道的
 * LowLevelGain 与 HighLevelGain；写入后的值落盘，下次据此算差值。
 * 通道映射（数组下标）：低音 {1,2} ｜ 中音 {3,4} ｜ 高音 {6,7}。
 * db 超出 ±10 会被钳位。返回 true = 会话已启动。
 * ⚠ 每段 2 通道 = 14 命令，全程静音（mute → 写 → 解除），属已知取舍。 */
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
 * 出音 / 停音各是一条 A7 写帧 + 82，**没有 mute / 选程序 / commit**，与 WDRC 那套会话不同。
 * 仍走同一套异步会话机制（1 条命令）。
 *
 * ⚠ 2026-09-30 起**不读应答**：写帧完直接 82（抓包 tone*.txt 里原本是 写→读 3B→82）。
 *   与切程序/调音量同形，代价相同 —— ok=1 只代表 I2C 写成功，不代表 7100 照做了。
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

/* ---- 测听（audiometry）进入 / 退出 ----
 * **三条命令编在一个会话里**（照 tonestar / tonestop 抓包顺序），都不读应答：
 *   ① `A2 00 16 <3 | prev_prog>` → 等 80ms → `82`   切程序
 *   ② `A2 00 2E <00 | 58>`       → 等  2ms → `82`   测听位（00 = 进，58 = 退）
 *   ③ `A7 01 00 00 00 26`        → 立刻   → `82`   解除静音（进出都有）
 *
 * 80ms / 2ms 取自 HEAD 原值（`dsp_7100_switch_program` 的程序加载窗口、
 * `dsp_7100_set_tone_mode` 的帧间间隔）；原波形里每句后面都跟着一次读，已砍掉。
 * ⚠ 单独调 `dsp_7100_set_mute()` 凑第 ③ 句会撞「同一时刻只允许一个会话」，别那么用。
 *
 * prev_prog 由调用方在**调用前**用 dsp_7100_get_program() 取好并传进来
 * （会话是异步的，跑起来时 s_cur_prog 还没变）。返回 true = 已受理。 */
bool dsp_7100_audiometry(bool enter, uint8_t prev_prog);

/* 会话是否进行中（进行中时应暂停读回/心跳，避免抢 I2C） */
bool dsp_7100_cmd_busy(void);

/* 200ms tick（app_process.c）调：只置超时标志，**不推进**。
 * 推进统一由主循环的 dsp_7100_cmd_poll() 做 —— 与读回
 * dsp_7100_rb_tick / dsp_7100_rb_poll 的分工完全相同。 */
void dsp_7100_cmd_tick(void);

/* 主循环（app.c）调：命令会话**唯一**的推进点，由 DIO13 边沿驱动。
 * 边沿等不到时退回 200ms tick 兜底。 */
void dsp_7100_cmd_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* DSP_7100_CMD_H */
