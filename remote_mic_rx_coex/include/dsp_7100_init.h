#ifndef DSP_7100_INIT_H
#define DSP_7100_INIT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 7100 上电初始化：按 start-connect-parm.txt（Packet 13..118）复刻。
 * 步骤表由 scripts/gen_dsp_7100_init.py 生成到 code/dsp_7100_init_tables.c，
 * 顺序/字节与抓包一致；执行引擎在 code/dsp_7100_init.c。
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

/* 生成的序列表（dsp_7100_init_tables.c） */
extern const dsp_init_step_t dsp_init_steps[];
extern const uint16_t        dsp_init_step_cnt;

/* 只跑到前 N 步用于调试；0 = 全部。 */
#define DSP7100_INIT_MAX_STEPS  0

void dsp_7100_boot_init(void);

/* 生成的 A7 命令表（parm1604.txt 三元组，A7 写载荷） */
typedef struct
{
    const uint8_t *wr;
    uint16_t       wl;
} dsp_a7_cmd_t;
extern const dsp_a7_cmd_t dsp_parm_cmds[];
extern const uint16_t     dsp_parm_cmd_cnt;

/* parm1604 推进（200ms tick 调）：每组 A7 两段读校验通过才进下一条，循环 */
void dsp_7100_parm_seq_tick(void);

/* 7 组 A7 推进（200ms tick 调）：每组重发到读回逐字节全等，进下一条，循环 */
void dsp_7100_a7_seq_tick(void);

/* 使能 DIO13 上升沿中断(7100 允许读) + 清标志 */
void dsp_7100_a7_arm(void);

/* 主循环每圈调用：检测 DIO13 上升沿标志 */
bool dsp_7100_a7_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* DSP_7100_INIT_H */
