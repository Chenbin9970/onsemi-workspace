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

/* 读回推进：由 200ms tick 调用，一条命令一个 tick。一轮跑完自行停止并解析。 */
void dsp_7100_rb_seq_tick(void);

/* ---- 读回规模 ---- */
#define DSP7100_RB_PROGS      4
#define DSP7100_WDRC_CH       16

/* 7100 各块读回长度（协议定义，供校验/存档参考）
 *   WDRC 0x177 = 375B，DFBC 0x32 = 306B，降噪 0xAE = 174B */
#define DSP7100_WDRC_DLEN     375
#define DSP7100_DFBC_DLEN     306
#define DSP7100_NOISE_DLEN    174

/* 每程序解析后的参数（51B）。原始 block 不保留 —— 读到即解析、只留这份。
 * WDRC 通道单元 147 bit，字段相对单元起点：
 *   LowLevelGain  @ +0  7bit 无符号
 *   HighLevelGain @ +14 8bit 有符号
 *   OutputLimit   @ +22 8bit 有符号
 * 见 docs/7100协议/WDRC/7100_WDRC读取.md */
typedef struct
{
    uint8_t denoise_en;                   /* 降噪使能（0xAE payload[0] bit7） */
    uint8_t denoise_lvl;                  /* 降噪档位 0..4 */
    uint8_t dfbc_en;                      /* DFBC 开关（0x32 payload[0] bit7） */
    int8_t  wdrc_ll[DSP7100_WDRC_CH];     /* LowLevelGain  */
    int8_t  wdrc_hl[DSP7100_WDRC_CH];     /* HighLevelGain */
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
