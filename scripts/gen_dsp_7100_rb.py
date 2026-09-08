#!/usr/bin/env python3
"""从 parm1604.txt 过滤出 4 程序 × (降噪/DFBC/WDRC) 的读回命令（抓包原样字节）。

匹配规则：
  - 选程序 A7 02 00 00 00 12 <P>
  - 选模块 A7 03 00 00 00 37 <sub> <P>；读块 A7 01 00 <blo> <bhi> 38
  - 目标块：(blo,bhi)=77 01->sub07(WDRC 378B), 32 01->sub0A(DFBC 309B),
            AE 00->sub09(降噪 177B)
输出 code/dsp_7100_rb_tables.c。
"""

import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "parm1604.txt")
OUT = os.path.join(ROOT, "remote_mic_rx_coex", "code", "dsp_7100_rb_tables.c")

TARGET = {(0x77, 0x01): 0x07, (0x32, 0x01): 0x0A, (0xAE, 0x00): 0x09}

def parse(path):
    rows = []
    with open(path, "r", encoding="utf-8-sig") as f:
        f.readline()
        for ln in f:
            ln = ln.rstrip("\r\n")
            if not ln.strip():
                continue
            t, pid, addr, op, data = ln.split(",", 4)
            rows.append((int(pid), op.strip(),
                         [int(x, 16) for x in data.split()]))
    return rows

def main():
    prog_row = {}      # P -> 选程序行字节
    mod_row  = {}      # (P,sub) -> 选模块行字节
    last_mod = {}      # sub -> (P, row) 最近一次选模块
    out = []           # (kind,P,bytes)
    done_mod = set()
    done_prog = set()

    for pid, op, by in parse(SRC):
        if op != "W" or not by or by[0] != 0xA7:
            continue
        if len(by) >= 6 and by[1] == 0x02 and by[2] == 0x00 and by[5] == 0x12:
            P = by[6]
            prog_row.setdefault(P, (pid, by))
        elif len(by) >= 8 and by[1] == 0x03 and by[2] == 0x00 and by[5] == 0x37:
            sub, P = by[6], by[7]
            mod_row[(P, sub)] = (pid, by)
            last_mod[sub] = (P, (pid, by))
        elif len(by) >= 6 and by[1] == 0x01 and by[2] == 0x00 and by[5] == 0x38:
            key = (by[3], by[4])
            if key in TARGET:
                sub = TARGET[key]
                lm = last_mod.get(sub)
                if not lm:
                    continue
                P, (mpid, mby) = lm
                if P not in done_prog:
                    done_prog.add(P)
                    out.append(("prog", P, prog_row[P][1]))
                if (P, sub) not in done_mod:
                    done_mod.add((P, sub))
                    out.append(("mod", P, mby))
                out.append(("read", P, by))

    cmds = [b for _, _, b in out]
    lines = ["/* 由 scripts/gen_dsp_7100_rb.py 从 parm1604.txt 过滤生成：",
             " * 4 程序 × (WDRC/DFBC/降噪) 选程序+选模块+读块。勿手改。 */",
             '#include "dsp_7100_init.h"', ""]
    for i, b in enumerate(cmds):
        lines.append("static const uint8_t dsp_rb_p%d[] = { %s };"
                     % (i, " ".join("0x%02X," % x for x in b)))
    lines.append("")
    lines.append("const dsp_a7_cmd_t dsp_rb_cmds[] = {")
    for i in range(len(cmds)):
        lines.append("    { dsp_rb_p%d, (uint16_t)sizeof(dsp_rb_p%d) }," % (i, i))
    lines.append("};")
    lines.append("")
    lines.append("const uint16_t dsp_rb_cmd_cnt = %d;" % len(cmds))
    lines.append("")
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print("wrote", OUT, "cmds =", len(cmds))
    for i, b in enumerate(cmds):
        print("%2d %s" % (i, " ".join("%02X" % x for x in b)))

if __name__ == "__main__":
    main()
