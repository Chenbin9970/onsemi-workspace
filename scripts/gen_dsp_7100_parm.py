#!/usr/bin/env python3
"""从 parm1604.txt 生成 A7 三元组的写命令表（dsp_parm_cmds）。

只取 W 行且 data[0]==0xA7 的命令（每个后跟 R + W 0x82 三元组），按顺序产出。
输出：remote_mic_rx_coex/code/dsp_7100_parm_tables.c
"""

import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "parm1604.txt")
OUT = os.path.join(ROOT, "remote_mic_rx_coex", "code", "dsp_7100_parm_tables.c")

def parse(path):
    rows = []
    with open(path, "r", encoding="utf-8-sig") as f:
        f.readline()
        for ln in f:
            ln = ln.rstrip("\r\n")
            if not ln.strip():
                continue
            t, pid, addr, op, data = ln.split(",", 4)
            rows.append((int(pid), float(t), op.strip(),
                         [int(x, 16) for x in data.split()]))
    return rows

def main():
    rows = parse(SRC)
    cmds = [(pid, by) for pid, t, op, by in rows if op == "W" and by and by[0] == 0xA7]
    n = len(cmds)
    lines = []
    lines.append("/* 由 scripts/gen_dsp_7100_parm.py 从 parm1604.txt 生成。勿手改。 */")
    lines.append('#include "dsp_7100_init.h"')
    lines.append("")
    for i, (pid, by) in enumerate(cmds):
        body = " ".join("0x%02X," % b for b in by)
        lines.append("static const uint8_t dsp_parm_p%d[] = { %s };  /* pid %d */"
                     % (i, body, pid))
    lines.append("")
    lines.append("const dsp_a7_cmd_t dsp_parm_cmds[] = {")
    for i, (pid, by) in enumerate(cmds):
        lines.append("    { dsp_parm_p%d, (uint16_t)sizeof(dsp_parm_p%d) },"
                     % (i, i))
    lines.append("};")
    lines.append("")
    lines.append("const uint16_t dsp_parm_cmd_cnt = %d;" % n)
    lines.append("")
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("wrote", OUT, "groups =", n)

if __name__ == "__main__":
    main()
