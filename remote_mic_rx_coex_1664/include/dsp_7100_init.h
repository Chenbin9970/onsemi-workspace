#ifndef DSP_7100_INIT_H
#define DSP_7100_INIT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ezairo 7100 通讯子系统（移植自 remote_mic_rx_coex）。
 *
 * 阶段一范围 = 通讯层：上电引导 + 4 程序读回。写路径（动态 A7 编码 / 会话状态机 /
 * flash 持久化）留待阶段二，届时本文件再增补。
 *
 * 步骤表由 scripts/gen_dsp_7100_init.py 生成到 code/dsp_7100_init_tables.c，
 * 读回表由 scripts/gen_dsp_7100_rb.py 生成到 code/dsp_7100_rb_tables.c。
 * RX 缓冲按表内最大读长取 700。 */
#define DSP_INIT_RX_BUF  700

typedef enum { DSP_INIT_TX, DSP_INIT_RX } dsp_init_op_t;

typedef struct
{
    uint32_t        delay_us;   /* 本步执行前延时（µs，保留亚 ms，抓包逐包复刻） */
    dsp_init_op_t   op;
    uint16_t        len;        /* TX 数据字节数 / RX 读取字节数 */
    const uint8_t  *data;       /* TX 数据（RX 为 NULL） */
} dsp_init_step_t;

/* 生成的引导序列表（dsp_7100_init_tables.c） */
extern const dsp_init_step_t dsp_init_steps[];
extern const uint16_t        dsp_init_step_cnt;

/* 只跑到前 N 步用于调试；0 = 全部。 */
#define DSP7100_INIT_MAX_STEPS  0

/* DIO13 边沿中断：把 7100 的「A7 响应就绪」边沿变成可观测事件（上升+下降都数）。
 * 1 = 开（ISR 只置计数，主循环打印），0 = 关（不装中断）。
 *
 * 为什么 DIO13 能做中断：RSL10 的 4 条 DIO 中断线（DIO0_IRQn..DIO3_IRQn）是
 * **可编程源**的 —— 任意 DIO 都能经 DIO_SRC_DIO_<n> 挂上去（见
 * rsl10_sys_dio.h 的 Sys_DIO_IntConfig），与 pad 号无关。本项占 2、3 号两条。
 *
 * ⚠ DIO2_IRQn / DIO3_IRQn 只是 NVIC 线号，与 DIO2/DIO3 pad（PCM DATA/CLK）
 *   无绑定关系（INT_CFG 里配的源是 DIO13），理论上不冲突；但同属一批 DIO 资源，
 *   上板时一并确认 PCM 输出无异常。1664 目前没有别处用 DIO 中断，两条线都是空的。
 *
 * 为什么采双边沿而不是只采上升沿：协议只说是「窄脉冲」，没说极性；而 7100 在
 * 上电握手里是**拉低** DIO13 表示 ready，所以「低=有效」同样说得通。只测一个
 * 方向可能白跑一趟上板。双边沿一次拿全。
 *
 * 目的：参考设计（remote_mic_rx_coex/code/dsp_7100_init.c）用它做过 A7 响应
 * 中断，但调用点被注释掉、没留下结论。本开关去把结论补上 —— 发 A7 的时候
 * 7100 到底会不会回跳，串口就能看出来，不用示波器。测完改回 0。 */
#define DSP7100_DIO13_IRQ_ENABLE    1

/* 开机调一次，**握手之前**：DIO13 配输入 + 使能上升/下降沿中断 + 计数清零
 * （关闭时为空实现）。放握手前是为了让握手本身当阳性对照。 */
void dsp_7100_dio13_irq_arm(void);

/* 有新的边沿就打印一次增量（关闭时为空实现）。共三处调用：
 * boot_init 里每步一条（逐命令定位）+ app.c 握手后一条当基线、主循环里一条当总数。 */
void dsp_7100_dio13_irq_poll(void);

/* DIO13 上升/下降沿累计数。读回拿它们当「7100 已吃下这一步」的加速判据
 * （见 dsp_7100_rb_poll）。中断没开时恒为 0 ⇒ 加速路径永不触发，自动退回 200ms。 */
uint32_t dsp_7100_dio13_rise_cnt(void);
uint32_t dsp_7100_dio13_fall_cnt(void);

/* 同步跑「首个 A7 之前」的引导步。开机调用一次，阻塞（≥1ms 分段喂狗）。 */
void dsp_7100_boot_init(void);

/* 生成的 A7 命令表 */
typedef struct
{
    const uint8_t *wr;
    uint16_t       wl;
} dsp_a7_cmd_t;

/* 4 程序×(WDRC/DFBC/降噪) 读回表（dsp_7100_rb_tables.c） */
extern const dsp_a7_cmd_t dsp_rb_cmds[];
extern const uint16_t     dsp_rb_cmd_cnt;

/* 读回推进一步（一条命令一个相位）。一轮跑完自行停止并解析。
 * 外部不要直接调 —— 走下面两个入口，见 dsp_7100_rb_poll 的说明。 */
void dsp_7100_rb_seq_tick(void);

/* 200ms tick 调（app_process.c）：只置超时标志，不推进。 */
void dsp_7100_rb_tick(void);

/* 主循环调（app.c）：读回唯一的推进点 —— 两个相位都由 DIO13 边沿驱动
 * （RB_SEND 等下降沿、RB_READ 等上升沿），边沿不来则由 dsp_7100_rb_tick 的超时兜底。 */
void dsp_7100_rb_poll(void);

/* ---- 读回规模 ---- */
#define DSP7100_RB_PROGS      4
#define DSP7100_WDRC_CH       16

/* 7100 各块读回长度（协议定义，供校验/存档参考）
 *   WDRC 0x177 = 375B，DFBC 0x32 = 306B，降噪 0xAE = 174B */
#define DSP7100_WDRC_DLEN     375
#define DSP7100_DFBC_DLEN     306
#define DSP7100_NOISE_DLEN    174

/* 每程序解析后的参数（54B）。原始 block 不保留 —— 读到即解析、只留这份。
 * WDRC 通道单元 147 bit，字段相对单元起点：
 *   LowLevelGain  @ +0  7bit 无符号
 *   HighLevelGain @ +14 8bit 有符号
 *   OutputLimit   @ +22 8bit 有符号
 * 见 docs/7100协议/WDRC/7100_WDRC读取.md
 *
 * wdrc_ll/hl 保存的是**设备当前值**（含 EQ）；eq_* 保存 App 上次下发的 EQ 绝对值。
 * 设 EQ 时用 本次值 − 上次值 得到差值，加到 wdrc_ll/hl 上，两者一起落盘
 * （见 dsp_7100_cmd.c a7_build_session / a7_session_finish）。 */
typedef struct
{
    uint8_t denoise_en;                   /* 降噪使能（0xAE payload[0] bit7） */
    uint8_t denoise_lvl;                  /* 降噪档位 0..4 */
    uint8_t dfbc_en;                      /* DFBC 开关（0x32 payload[0] bit7） */
    int8_t  eq_low;                       /* 均衡器低音：App 上次下发的绝对值（±dB） */
    int8_t  eq_mid;                       /* 均衡器中音：同上 */
    int8_t  eq_high;                      /* 均衡器高音：同上 */
    int8_t  wdrc_ll[DSP7100_WDRC_CH];     /* LowLevelGain  **设备当前值**（含 EQ） */
    int8_t  wdrc_hl[DSP7100_WDRC_CH];     /* HighLevelGain **设备当前值**（含 EQ） */
    int8_t  wdrc_ol[DSP7100_WDRC_CH];     /* OutputLimit   */
} dsp_7100_prog_t;

/* 全部程序参数 + 有效性（存储层 load/save 用） */
typedef struct
{
    dsp_7100_prog_t prog[DSP7100_RB_PROGS];
    uint8_t         valid[DSP7100_RB_PROGS];
} dsp_7100_rb_bufs_t;

dsp_7100_rb_bufs_t *dsp_7100_rb_bufs(void);

/* 一轮读回是否已完成 */
bool dsp_7100_rb_done(void);

/* 是否需要 I2C 读回（flash 缓存命中则为 false）。boot 后由 cache_try_load 设定。 */
bool dsp_7100_rb_needed(void);

/* 开机调：尝试从 flash 载入读回缓存；命中则后续不再走 I2C 读回。 */
void dsp_7100_cache_try_load(void);

/* 主循环调：读回跑完一轮后把结果落盘（flash 擦写不在定时器上下文做）。 */
void dsp_7100_process_deferred(void);

/* 请求把当前 RAM 参数落盘（运行时改了参数后调，实际擦写由 process_deferred 做）。 */
void dsp_7100_cache_save_request(void);

/* 指定程序参数是否有效（prog 越界返回 false） */
bool dsp_7100_prog_valid(uint8_t prog);

/* 取指定程序参数（prog 越界或无效返回 NULL） */
const dsp_7100_prog_t *dsp_7100_get_prog(uint8_t prog);

#ifdef __cplusplus
}
#endif

#endif /* DSP_7100_INIT_H */
