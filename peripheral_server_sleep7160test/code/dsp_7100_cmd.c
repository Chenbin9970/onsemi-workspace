/* 7100 运行时命令层。
 * 波形解析（program1-4.csv / volume.csv）：
 *   写帧  A2 00 <reg> <val>        （reg: 16=程序, 12=音量）
 *   读回  43 03 00 00 <reg> <val>   （从机确认）
 *   结束  82
 * 调音量 = 写帧 → 读确认 → 82（3 帧）
 * 切程序 = 写帧 → 读确认 → 82 → 读状态 → 82（5 帧，对照 program1 波形） */

#include "dsp_7100_cmd.h"
#include "app.h"
#include "i2c_7100_hal.h"

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

#define DSP7100_CMD_PARAM_WRITE  0xA2
#define DSP7100_REG_PROGRAM      0x16
#define DSP7100_REG_VOLUME       0x12
#define DSP7100_CMD_END          0x82
#define DSP7100_RX_LEN           6

/* 6 档音量 → 0-100 值：round(档位 * 100 / 6) = 17/33/50/67/83/100 */
static const uint8_t s_volume_value[6] = {0x11, 0x21, 0x32, 0x43, 0x53, 0x64};

static uint8_t s_cur_prog = 1;
static uint8_t s_cur_vol_level = 6;

static bool dsp_7100_write_cmd(uint8_t reg, uint8_t value)
{
    uint8_t wr[4] = {DSP7100_CMD_PARAM_WRITE, 0x00, reg, value};
    bool ok = i2c_7100_write(I2C_7100_ADDR, wr, sizeof(wr));
    PRINTF("[7100] TX: %02X %02X %02X %02X (ok=%u)\r\n",
           wr[0], wr[1], wr[2], wr[3], ok);
    return ok;
}

static bool dsp_7100_read6(uint8_t *rx)
{
    bool ok = i2c_7100_read(I2C_7100_ADDR, rx, DSP7100_RX_LEN);
    PRINTF("[7100] rd: %02X %02X %02X %02X %02X %02X (ok=%u)\r\n",
           rx[0], rx[1], rx[2], rx[3], rx[4], rx[5], ok);
    return ok;
}

/* 调音量：写帧 → 2ms → 读确认 → 1ms → 82 */
bool dsp_7100_set_volume(uint8_t level)
{
    uint8_t rx[DSP7100_RX_LEN];
    uint8_t end = DSP7100_CMD_END;

    if (level < 1 || level > 6) return false;
    if (!dsp_7100_write_cmd(DSP7100_REG_VOLUME, s_volume_value[level - 1])) return false;
    i2c_7100_delay_ms(2);
    if (!dsp_7100_read6(rx)) return false;
    i2c_7100_delay_ms(1);
    if (!i2c_7100_write(I2C_7100_ADDR, &end, sizeof(end))) return false;
    s_cur_vol_level = level;
    return true;
}

/* 切程序（对照 program1 完整 5 帧）：
 * 写帧 → 80ms → 读确认 → 1ms → 82 → 1ms → 读状态 → 1ms → 82 */
bool dsp_7100_switch_program(uint8_t prog)
{
    uint8_t rx[DSP7100_RX_LEN];
    uint8_t end = DSP7100_CMD_END;

    if (prog < 1 || prog > 4) return false;
    if (!dsp_7100_write_cmd(DSP7100_REG_PROGRAM, prog)) return false;
    i2c_7100_delay_ms(80);
    if (!dsp_7100_read6(rx)) return false;
    i2c_7100_delay_ms(1);
    if (!i2c_7100_write(I2C_7100_ADDR, &end, sizeof(end))) return false;
    i2c_7100_delay_ms(1);
    if (!dsp_7100_read6(rx)) return false;
    i2c_7100_delay_ms(1);
    if (!i2c_7100_write(I2C_7100_ADDR, &end, sizeof(end))) return false;
    s_cur_prog = prog;
    return true;
}

uint8_t dsp_7100_get_program(void)      { return s_cur_prog; }
uint8_t dsp_7100_get_volume_level(void) { return s_cur_vol_level; }
