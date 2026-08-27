#!/usr/bin/env python3
"""Decode logic-analyzer I2C captures exported as CSV (Time[s], ch0, ch1).

Auto-detects SCL/SDA by transition count (SCL clocks more). Prints each
frame as hex bytes with ACK/NACK marks.

Usage: python decode_i2c.py <file.csv>
"""
import csv
import sys


def load_csv(path):
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        rows = []
        for line in csv.reader(f):
            if len(line) >= 3:
                try:
                    rows.append((float(line[0]), int(line[1]), int(line[2])))
                except ValueError:
                    pass
    return rows


def decode(path):
    rows = load_csv(path)
    if not rows:
        print("no samples")
        return

    # SCL = channel with more transitions
    t0 = sum(1 for i in range(1, len(rows)) if rows[i][1] != rows[i - 1][1])
    t1 = sum(1 for i in range(1, len(rows)) if rows[i][2] != rows[i - 1][2])
    scl_ch = 2 if t1 >= t0 else 1
    sda_ch = 1 if scl_ch == 2 else 2
    print("# SCL=ch%d(%d trans) SDA=ch%d(%d trans), %d samples"
          % (scl_ch, t1 if scl_ch == 2 else t0, sda_ch,
             t0 if scl_ch == 2 else t1, len(rows)))

    def scl(r):
        return r[scl_ch]

    def sda(r):
        return r[sda_ch]

    frames = []      # list of (start_idx, bytes_list, acks_list)
    cur_bytes = []
    cur_acks = []
    bit_cnt = 0
    cur_val = 0
    started = False
    prev_scl = scl(rows[0])
    prev_sda = sda(rows[0])

    for i in range(1, len(rows)):
        scl_cur = scl(rows[i])
        sda_cur = sda(rows[i])

        # START/STOP: SDA toggles while SCL high
        if scl_cur == 1 and prev_sda != sda_cur:
            if prev_sda == 1 and sda_cur == 0:
                # START
                started = True
                cur_bytes = []
                cur_acks = []
                bit_cnt = 0
                cur_val = 0
            elif prev_sda == 0 and sda_cur == 1:
                # STOP
                if started:
                    frames.append((cur_bytes[:], cur_acks[:]))
                started = False
        # sample data on SCL rising edge
        if scl_cur == 1 and prev_scl == 0 and started:
            if bit_cnt < 8:
                cur_val = (cur_val << 1) | sda_cur
                bit_cnt += 1
            else:
                cur_bytes.append(cur_val)
                cur_acks.append(sda_cur)  # 0=ACK, 1=NACK
                bit_cnt = 0
                cur_val = 0
        prev_scl = scl_cur
        prev_sda = sda_cur

    if started:
        frames.append((cur_bytes[:], cur_acks[:]))

    for fi, (b, a) in enumerate(frames):
        parts = []
        for j, byte in enumerate(b):
            ack = "" if j >= len(a) else ("A" if a[j] == 0 else "N")
            parts.append("%02X%s" % (byte, ack))
        print("frame %02d: %s" % (fi, " ".join(parts)))


if __name__ == "__main__":
    for p in sys.argv[1:]:
        print("\n===== %s =====" % p)
        decode(p)
