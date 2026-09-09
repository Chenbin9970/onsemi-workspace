#!/usr/bin/env python3
"""生成 remote_mic_rx_coex 的 7100 精简写会话序列表（程序1/P字节01：
WDRC LowLevelGain 全 16 通道置 0）。

输入 : 无（帧从 docs/7100 协议反推：
       WDRC LL 参数号 = 0x15 + 0x11×(N−1)，ch1..10 抓包验证，ch11..16 线性外推 + 高字节
       —— 依据 docs/7100协议/WDRC/7100_WDRC设置.md 与 d:/tmp/7111_proto.txt(模块07, 16bit 参数区)。
       会话骨架参照 p0dfbc0-1（mute->select P01->写->confirm->unmute->commit）。
       2026-09-09：DFBC/降噪 先不动，只写 WDRC LL=0。
输出 : remote_mic_rx_coex/code/dsp_7100_set_tables.c

会话（单次静音）：
  mute -> select P01 -> WDRC LL=0 ×16ch -> confirm(10 01) -> unmute -> select P01 -> commit(0C)
每条命令由引擎按「写命令 -> 读 3B 应答(46 00 00) -> 写 04 82」推进。
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "remote_mic_rx_coex", "code", "dsp_7100_set_tables.c")

# WDRC LL 参数号（模块07 内 16bit 地址）：ch N = 0x15 + 0x11*(N-1)
# 准备命令尾 = 01 <addr_hi> <addr_lo>（<addr_hi>=0 的 ch1..14 与抓包逐字节一致）
def ll_param(n):
    return 0x15 + 0x11 * (n - 1)


def build():
    cmds = []
    tags = []

    def add(b, tag):
        cmds.append(b)
        tags.append(tag)

    add([0xA7, 0x01, 0x00, 0x00, 0x00, 0x25], "mute")
    add([0xA7, 0x02, 0x00, 0x00, 0x00, 0x12, 0x01], "select P01")
    for n in range(1, 17):
        a = ll_param(n)
        add([0xA7, 0x05, 0x00, 0x00, 0x00, 0x05, 0x07, 0x01,
             (a >> 8) & 0xFF, a & 0xFF], "prep WDRC LL ch%d (0x%04X)" % (n, a))
        add([0xA7, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00],
            "WDRC LL ch%d = 0" % n)
    add([0xA7, 0x02, 0x00, 0x00, 0x00, 0x10, 0x01], "confirm")
    add([0xA7, 0x01, 0x00, 0x00, 0x00, 0x26], "unmute")
    add([0xA7, 0x02, 0x00, 0x00, 0x00, 0x12, 0x01], "select P01")
    add([0xA7, 0x01, 0x00, 0x00, 0x00, 0x0C], "commit")
    return cmds, tags


def main():
    cmds, tags = build()
    assert len(cmds) == 38, "expect 38 cmds, got %d" % len(cmds)
    lines = []
    lines.append("/* 由 scripts/gen_dsp_7100_set.py 自动生成 —— 勿手改。 */")
    lines.append("/* 精简写会话：程序1(P字节01) WDRC LowLevelGain 全 16 通道置 0。 */")
    lines.append('#include "dsp_7100_init.h"')
    lines.append("#include <stdint.h>")
    lines.append("")
    for i, b in enumerate(cmds):
        lines.append("static const uint8_t dsp_set_c%d[] = { %s };" %
                     (i, " ".join("0x%02X," % x for x in b)))
    lines.append("")
    lines.append("const dsp_a7_cmd_t dsp_set_cmds[] = {")
    for i in range(len(cmds)):
        lines.append("    { dsp_set_c%d, (uint16_t)sizeof(dsp_set_c%d) }," % (i, i))
    lines.append("};")
    lines.append("")
    lines.append("const uint16_t dsp_set_cmd_cnt = %d;" % len(cmds))
    lines.append("")
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("cmds=%d -> %s" % (len(cmds), OUT))
    for n in (11, 12, 13, 14, 15, 16):
        print("  ch%d LL param=0x%04X" % (n, ll_param(n)))


if __name__ == "__main__":
    main()
