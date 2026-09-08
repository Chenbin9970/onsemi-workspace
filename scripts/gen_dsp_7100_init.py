#!/usr/bin/env python3
"""从 start-connect-parm.txt 生成 dsp_7100_init 的步骤表（行 Packet 13..108）。

输出 C 数据到 remote_mic_rx_coex/code/dsp_7100_init_tables.c：
  - dsp_init_wpool[]   ：所有 TX 数据拼接（不含 I2C 地址字节）
  - dsp_init_steps[]   ：dsp_init_step_t，顺序/字节与抓包完全一致
  - dsp_init_step_cnt  ：步数

delay_us = round((t[i]-t[i-1])*1e6)，首步为 0（暖机在 main 里做）。
读取端点：Packet ID 118（含，A2 00 47 写，t=10.8788114）——A7(119+) 段之前。
"""

import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "start-connect-parm.txt")
OUT = os.path.join(ROOT, "remote_mic_rx_coex", "code", "dsp_7100_init_tables.c")

ID_MIN = 13  # 首个数据包（A6 握手）
ID_MAX = 118  # 端点：A7 段之前（改文件后需同步，或改成自动取 max）

def parse_rows(path):
    rows = []
    with open(path, "r", encoding="utf-8-sig") as f:
        f.readline()  # 表头
        for ln in f:
            ln = ln.rstrip("\r\n")
            if not ln.strip():
                continue
            t, pid, addr, op, data = ln.split(",", 4)
            by = [int(x, 16) for x in data.split()]
            rows.append((int(pid), float(t), op.strip(), by))
    return rows

def main():
    rows = [r for r in parse_rows(SRC) if ID_MIN <= r[0] <= ID_MAX]
    if not rows:
        raise SystemExit(f"no rows in [{ID_MIN},{ID_MAX}]")
    # 顺序、连续性校验
    ids = [r[0] for r in rows]
    assert ids == list(range(ids[0], ids[0] + len(ids))), "Packet ID 不连续"
    assert rows[0][0] == ID_MIN and rows[-1][0] == ID_MAX

    pool = bytearray()
    offs = []          # 每步 TX 在 pool 中的偏移（R 步为 None）
    lens = []
    ops = []
    for pid, t, op, by in rows:
        if op == "W":
            offs.append(len(pool))
            pool.extend(by)
            lens.append(len(by))
            ops.append("DSP_INIT_TX")
        else:
            offs.append(None)
            lens.append(len(by))   # R：len = 返回字节数
            ops.append("DSP_INIT_RX")

    delays = []   # µs（start-to-start，保留亚 ms）
    for i, (pid, t, op, by) in enumerate(rows):
        if i == 0:
            delays.append(0)
        else:
            delays.append(round((t - rows[i - 1][1]) * 1000000.0))

    # 写 pool（按每步 TX 分组注释）
    pool_lines = ["static const uint8_t dsp_init_wpool[] = {"]
    step_lines = ["const dsp_init_step_t dsp_init_steps[] = {"]
    idx = 0
    cur = ""
    pool_idx = 0
    for i, (pid, t, op, by) in enumerate(rows):
        note = f"/* {pid} t={t:.6f} {'W' if op=='W' else 'R'} */"
        if op == "W":
            step_lines.append(f"    {{ {delays[i]}, DSP_INIT_TX, {len(by)}, dsp_init_wpool + {offs[i]} }},   {note}")
        else:
            step_lines.append(f"    {{ {delays[i]}, DSP_INIT_RX, {len(by)}, 0 }},   {note}")
    # pool 内容 12/行
    pool_lines.append("    " + " ".join("0x%02X," % b for b in pool))

    lines = []
    lines.append("/* 由 scripts/gen_dsp_7100_init.py 生成（start-connect-parm.txt Packet %d..%d）。勿手改。 */" % (ID_MIN, ID_MAX))
    lines.append('#include "dsp_7100_init.h"')
    lines.append("")
    lines.append("\n".join(pool_lines) + "\n};")
    lines.append("")
    lines.append("\n".join(step_lines) + "\n};")
    lines.append("")
    lines.append("const uint16_t dsp_init_step_cnt = %d;" % len(rows))
    lines.append("")
    txt = "\n".join(lines)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(txt)
    print("wrote", OUT)
    print("steps =", len(rows), " W =", ops.count("DSP_INIT_TX"), " R =", ops.count("DSP_INIT_RX"))
    print("pool bytes =", len(pool))
    print("max delay_us =", max(delays), " sum =", sum(delays), "us (",
          sum(delays) / 1e6, "s)")

if __name__ == "__main__":
    main()
