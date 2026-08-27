/* 7100 上电初始化 — 分阶段复刻 star.csv 参考序列。
 * 每阶段由 dsp_init_step_t 表驱动：TX 写数据、RX 读 len 字节、delay_ms 先延时。
 * 收发全部经 UART 打印，便于逐步对照参考波形。 */

#include "dsp_7100_init.h"
#include "app.h"
#include "i2c_7100_hal.h"

#ifndef PRINTF
#define PRINTF(...) ((void)0)
#endif

typedef enum { DSP_INIT_TX, DSP_INIT_RX } dsp_init_op_t;

typedef struct {
    uint16_t        delay_ms;   /* 本步执行前延时 */
    dsp_init_op_t   op;
    uint16_t        len;        /* TX 数据字节数 / RX 读取字节数 */
    const uint8_t  *data;       /* TX 数据（RX 为 NULL） */
} dsp_init_step_t;

/* ---- TX 数据（不含 I2C 地址字节 0x04） ---- */
static const uint8_t d_a6[]     = { 0xA6, 0x01, 0x13, 0x04, 0x1E, 0xFF, 0xFF, 0x01, 0x00, 0x01, 0x66 };
static const uint8_t d_a8[]     = { 0xA8, 0xE5, 0xBB, 0xB1, 0x46, 0x00, 0xF4 };
static const uint8_t d_poll[]   = { 0x82 };
static const uint8_t d_8c00[]   = { 0x8C, 0x00 };
static const uint8_t d_a1_12[]  = { 0xA1, 0x00, 0x12, 0x4F, 0x85, 0xA0, 0x02, 0x82, 0x62, 0x11, 0xE5, 0x84, 0xA0, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B };
static const uint8_t d_a1_16[]  = { 0xA1, 0x00, 0x16, 0x4F, 0x85, 0xA0, 0x03, 0x82, 0x62, 0x11, 0xE5, 0x84, 0xA0, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B };
static const uint8_t d_a1_1a[]  = { 0xA1, 0x00, 0x1A, 0x4F, 0x85, 0xA0, 0x04, 0x82, 0x62, 0x11, 0xE5, 0x84, 0xA0, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B };
static const uint8_t d_a1_1e[]  = { 0xA1, 0x00, 0x1E, 0x4F, 0x85, 0xA0, 0x05, 0x82, 0x62, 0x11, 0xE5, 0x84, 0xA0, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B };
static const uint8_t d_a1_22[]  = { 0xA1, 0x00, 0x22, 0x4F, 0x85, 0xA0, 0x06, 0x82, 0x62, 0x11, 0xE5, 0x84, 0xA0, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B };
static const uint8_t d_a1_26[]  = { 0xA1, 0x00, 0x26, 0x4F, 0x85, 0xA0, 0x07, 0x82, 0x62, 0x11, 0xE5, 0x84, 0xA0, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B };
static const uint8_t d_a1_2a[]  = { 0xA1, 0x00, 0x2A, 0x4F, 0x85, 0xA0, 0x08, 0x82, 0x62, 0x11, 0xE5, 0x84, 0xA0, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B };
static const uint8_t d_a1_2e[]  = { 0xA1, 0x00, 0x2E, 0x4F, 0x85, 0xA0, 0x09, 0x82, 0x62, 0x11, 0xE5, 0x84, 0xA0, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B };
static const uint8_t d_a1_3a[]  = { 0xA1, 0x00, 0x3A, 0x4F, 0x85, 0xA0, 0x0C, 0x82, 0x62, 0x11, 0xE5, 0x84, 0xA0, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B };
static const uint8_t d_a1_3e[]  = { 0xA1, 0x00, 0x3E, 0x4F, 0x85, 0xA0, 0x0D, 0x82, 0x62, 0x11, 0xE5, 0x84, 0xA0, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B };
static const uint8_t d_a1_42[]  = { 0xA1, 0x00, 0x42, 0x4F, 0x85, 0xA0, 0x0E, 0x82, 0x62, 0x11, 0xE5, 0x84, 0xA0, 0x00, 0x02, 0xA5, 0xD5, 0xC5, 0x1B };

/* ---- 各阶段步骤表 ---- */
static const dsp_init_step_t stage1[] = {   /* 握手写 */
    {114, DSP_INIT_TX, 11, d_a6},
    {  0, DSP_INIT_TX,  7, d_a8},
    {119, DSP_INIT_TX, 11, d_a6},
    {  0, DSP_INIT_TX,  7, d_a8},
};

static const dsp_init_step_t stage2[] = {   /* 读配置（04 82 + 读块） */
    {  4, DSP_INIT_RX,  3, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 10, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 13, NULL},
    {  0, DSP_INIT_TX,  2, d_8c00},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  4, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 19, NULL},
    {  1, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 25, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 25, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  5, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 26, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  5, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  4, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 23, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 31, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 31, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 31, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 47, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 50, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 50, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 31, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 31, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 31, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 50, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 38, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  4, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  8, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  5, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  3, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  3, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  3, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  3, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  3, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  3, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  3, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 11, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  3, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  4, NULL},
};

static const dsp_init_step_t stage3[] = {   /* 参数写（A1 块） */
    { 25, DSP_INIT_TX, 19, d_a1_12},
    {  0, DSP_INIT_TX, 19, d_a1_16},
    {  0, DSP_INIT_TX, 19, d_a1_1a},
    {  0, DSP_INIT_TX, 19, d_a1_1e},
    {  0, DSP_INIT_TX, 19, d_a1_22},
    {  0, DSP_INIT_TX, 19, d_a1_26},
    {  0, DSP_INIT_TX, 19, d_a1_2a},
    {  0, DSP_INIT_TX, 19, d_a1_2e},
    {  0, DSP_INIT_TX, 19, d_a1_3a},
    {  0, DSP_INIT_TX, 19, d_a1_3e},
};

static const dsp_init_step_t stage4[] = {   /* 收尾读 */
    {  0, DSP_INIT_TX, 19, d_a1_42},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  4, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  4, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX, 26, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
    {  0, DSP_INIT_RX,  3, NULL},
    {  0, DSP_INIT_TX,  1, d_poll},
};

static void run_stage(const dsp_init_step_t *steps, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) {
        if (steps[i].delay_ms) i2c_7100_delay_ms(steps[i].delay_ms);

        if (steps[i].op == DSP_INIT_TX) {
            PRINTF("[7100] TX:");
            for (uint16_t j = 0; j < steps[i].len; j++)
                PRINTF(" %02X", steps[i].data[j]);
            PRINTF("\r\n");
            i2c_7100_write(I2C_7100_ADDR, steps[i].data, steps[i].len);
        } else {
            uint8_t rx[64];
            bool ok = i2c_7100_read(I2C_7100_ADDR, rx, steps[i].len);
            PRINTF("[7100] RX (%uB ok=%u):", steps[i].len, ok);
            for (uint16_t j = 0; j < steps[i].len; j++)
                PRINTF(" %02X", rx[j]);
            PRINTF("\r\n");
        }
    }
}

void dsp_7100_boot_init(void)
{
    PRINTF("[7100-init] boot init, max_stage=%d\r\n", DSP7100_INIT_MAX_STAGE);
#if DSP7100_INIT_MAX_STAGE >= 1
    PRINTF("[7100-init] stage1 握手写\r\n");
    run_stage(stage1, sizeof(stage1) / sizeof(stage1[0]));
#endif
#if DSP7100_INIT_MAX_STAGE >= 2
    PRINTF("[7100-init] stage2 读配置\r\n");
    run_stage(stage2, sizeof(stage2) / sizeof(stage2[0]));
#endif
#if DSP7100_INIT_MAX_STAGE >= 3
    PRINTF("[7100-init] stage3 参数写\r\n");
    run_stage(stage3, sizeof(stage3) / sizeof(stage3[0]));
#endif
#if DSP7100_INIT_MAX_STAGE >= 4
    PRINTF("[7100-init] stage4 收尾读\r\n");
    run_stage(stage4, sizeof(stage4) / sizeof(stage4[0]));
#endif
    PRINTF("[7100-init] done\r\n");
}
