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

/* ---- 7100 A7 参数读回（开机一次性，DSP7100_READBACK_ENABLE 控制） ---- */

#define DSP7100_READBACK_PROGS   4    /* 程序数（0..3） */
#define DSP7100_WDRC_CHANNELS   16    /* WDRC 通道数（1..16） */

/* 单个程序读回结果（见 docs/7100协议/ 降噪/DFBC/WDRC 读取文档） */
typedef struct {
    uint8_t  valid;        /* 三模块均读回成功 = 1 */
    uint8_t  noise_en;     /* 降噪使能（0xAE 块 data[0] bit7） */
    uint8_t  noise_level;  /* 降噪档位 0..4（48×4bit 统一值=3×(档+1)） */
    uint8_t  dfbc_on;      /* DFBC 开关（0x32 块 data[0] bit7） */
    int8_t   wdrc_low [DSP7100_WDRC_CHANNELS];  /* ch1..16 LowLevelGain  (0x177 块) */
    int8_t   wdrc_high[DSP7100_WDRC_CHANNELS];  /* ch1..16 HighLevelGain (LowLevel位+15) */
} dsp7100_prog_read_t;

/* 读回结果（4 程序，供 UART 打印 / J-Link 抓取） */
extern dsp7100_prog_read_t g_dsp7100_read[DSP7100_READBACK_PROGS];

/* 开机一次性读回：4 程序 × (降噪/DFBC/WDRC)，解析存 g_dsp7100_read 并经 UART 打印。
 * 须在 dsp_7100_boot_init() 完成后、DSP 运行前调用（内含 I2C + 延时）。 */
void dsp_7100_readback_all(void);

#ifdef __cplusplus
}
#endif

#endif /* DSP_7100_CMD_H */
