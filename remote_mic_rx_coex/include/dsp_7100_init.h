#ifndef DSP_7100_INIT_H
#define DSP_7100_INIT_H

/* 7100 上电初始化（对照 star.csv 参考序列，分阶段复刻）。
 *
 * DSP7100_INIT_MAX_STAGE 控制跑到哪个阶段（逐阶段调试用）：
 *   0 = 跳过全部
 *   1 = 握手写（A6/A8）
 *   2 = 读配置（04 82 + 读块）
 *   3 = 参数写（A1 块）
 *   4 = 收尾读
 * 每次收发都会经 UART 打印 [7100] TX/RX。 */
#define DSP7100_INIT_MAX_STAGE  3

void dsp_7100_boot_init(void);

#endif /* DSP_7100_INIT_H */
