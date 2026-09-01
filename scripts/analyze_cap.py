#!/usr/bin/env python3
"""7160test 音频数据内容分析（cap_in / cap_out 对比）

用法:
  python analyze_cap.py cap_in.bin cap_out.bin [--fs_in 16000] [--fs_out 12000] [--csv spec.csv]

输入（J-Link savebin 导出，int16 小端原始采样）:
  cap_in.bin  : 16k DSP 解码输出（进 ASRC 前，未滤波/未 dither）
  cap_out.bin : 12k PCM 输出（出 ASRC 后，7100 实际收到的音频）
  每路 CAP_N=1024 点（cap_in≈64ms @16k，cap_out≈85ms @12k），FFT 分辨率 ~15.6Hz。

判定逻辑:
  1. 静音嗡嗡: 看 cap_in 时域是否接近 0。
     - cap_in 干净(±几 LSB)而 cap_out 出现 <300Hz 周期峰 -> ASRC 极限环，dither 修。
     - cap_in 本身就有低频周期 -> 问题在 G722 解码/源，dither 无效。
  2. 金属尾音: 对比 4~6k 频带能量。
     - cap_out 4~6k 明显强于 cap_in 的 4~6k（cap_in 里没有的频谱）-> 下采样混叠/镜像，
       进 ASRC 前加 LPF 修。
     - cap_in 本身就有 4~6k 异常 -> 问题在解码/源，LPF 无效。

输出: 时域统计 + FFT 峰值表(dB) + 频带能量对比 + 判定提示；--csv 导出全频谱供画图。
"""
import sys
import struct
import math
import argparse

def read_i16(path):
    """读 int16 小端采样，返回全部样本。"""
    with open(path, "rb") as f:
        raw = f.read()
    n = len(raw) // 2
    if n == 0:
        sys.exit(f"[error] {path} 为空或不是 int16 数据")
    return list(struct.unpack("<%dh" % n, raw[: 2 * n]))


def fft(x):
    """迭代 radix-2 复数 FFT, x 为实序列（虚部 0），就地计算。"""
    n = len(x)
    if n & (n - 1):
        raise ValueError("FFT 长度必须是 2 的幂")
    re = list(x)
    im = [0.0] * n
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            re[i], re[j] = re[j], re[i]
            im[i], im[j] = im[j], im[i]
    length = 2
    while length <= n:
        ang = -2.0 * math.pi / length
        w_re = math.cos(ang)
        w_im = math.sin(ang)
        half = length >> 1
        for i in range(0, n, length):
            cur_re = 1.0
            cur_im = 0.0
            for k in range(half):
                a = i + k
                b = a + half
                t_re = cur_re * re[b] - cur_im * im[b]
                t_im = cur_re * im[b] + cur_im * re[b]
                re[b] = re[a] - t_re
                im[b] = im[a] - t_im
                re[a] += t_re
                im[a] += t_im
                nxt_re = cur_re * w_re - cur_im * w_im
                nxt_im = cur_re * w_im + cur_im * w_re
                cur_re = nxt_re
                cur_im = nxt_im
        length <<= 1
    return re, im


def hann(x):
    n = len(x)
    return [x[i] * 0.5 * (1.0 - math.cos(2.0 * math.pi * i / (n - 1))) for i in range(n)]


def spectrum(samples):
    """返回 (freq[], mag_db[]) 单边谱。

    mag_db 刻度: 0dB = 幅度 1.0 的正弦（无窗时峰值 bin 显示 20*log10(A)；
    Hann 窗相干增益 0.5, 峰值约 -6dB）。同一信号两路比较时刻度一致, 只关心相对差。
    采样数不足 2 的幂时自动补零到下一个 2 的幂。
    """
    n = len(samples)
    n2 = 1 << (n - 1).bit_length()
    if n2 != n:
        samples = samples + [0] * (n2 - n)
    n = n2
    re, im = fft(hann(samples))
    half = n // 2
    freq = [i * 1.0 / n for i in range(half + 1)]  # 归一化频率 (0~0.5)
    mag = []
    for k in range(half + 1):
        amp = math.hypot(re[k], im[k]) / n * 2.0
        amp = max(amp, 1e-12)
        mag.append(20.0 * math.log10(amp))
    return freq, mag


def band_energy_db(freq, mag, f0, f1):
    """带内功率 -> dB（相对满幅正弦）。"""
    power = 0.0
    for k, f in enumerate(freq):
        if f0 <= f < f1:
            amp = 10.0 ** (mag[k] / 20.0)
            power += amp * amp
    return 10.0 * math.log10(max(power, 1e-12))


def report_time(name, samples):
    n = len(samples)
    mean = sum(samples) / n
    peak = max(abs(s) for s in samples)
    rms = math.sqrt(sum(s * s for s in samples) / n)
    print(f"  {name:8s}  min={min(samples):6d} max={max(samples):6d} "
          f"DC={mean:7.1f} 峰值={peak:6d} RMS={rms:7.1f}")
    return rms, abs(mean)


def peaks(freq, mag, fs, k=15, fmin_hz=20.0):
    """找局部极大峰。freq 为归一化频率, fmin_hz 用 Hz 指定下限。"""
    fmin = fmin_hz / fs
    out = []
    for i in range(1, len(mag) - 1):
        if freq[i] < fmin:
            continue
        if mag[i] >= mag[i - 1] and mag[i] > mag[i + 1]:
            out.append((freq[i], mag[i]))
    out.sort(key=lambda t: t[1], reverse=True)
    return out[:k]


def print_peaks(title, freq, mag, fs):
    pk = peaks(freq, mag, fs)
    print(f"  {title} 前{len(pk)}峰 (dB, 幅度1.0=0dB, Nyquist={fs//2}Hz):")
    for f, m in pk:
        print(f"    {f * fs:8.1f} Hz   {m:7.1f} dB")
    return pk


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cap_in")
    ap.add_argument("cap_out")
    ap.add_argument("--fs_in", type=float, default=16000.0)
    ap.add_argument("--fs_out", type=float, default=12000.0)
    ap.add_argument("--csv")
    a = ap.parse_args()

    sin = read_i16(a.cap_in)
    sout = read_i16(a.cap_out)

    print("== 时域统计 ==")
    rms_in, dc_in = report_time("cap_in", sin)
    rms_out, dc_out = report_time("cap_out", sout)

    fin, min_ = spectrum(sin)
    fout, mout = spectrum(sout)

    print("\n== FFT 峰值 ==")
    print_peaks("cap_in (16k DSP 输出)", fin, min_, a.fs_in)
    print_peaks("cap_out (12k PCM 输出)", fout, mout, a.fs_out)

    print("\n== 频带能量对比 (dB) ==")
    bands = [(0, 500), (500, 2000), (2000, 4000), (4000, 6000), (6000, 8000)]
    for f0, f1 in bands:
        e_in = band_energy_db(fin, min_, f0 / a.fs_in, f1 / a.fs_in) if f1 <= a.fs_in / 2 else None
        e_out = band_energy_db(fout, mout, f0 / a.fs_out, f1 / a.fs_out) if f1 <= a.fs_out / 2 else None
        txt = "  %4d-%4d Hz: " % (f0, f1)
        if e_in is not None:
            txt += "in=%6.1f  " % e_in
        else:
            txt += "in=  n/a  "
        if e_out is not None:
            txt += "out=%6.1f  (out-in=%+6.1f)" % (e_out, e_out - e_in if e_in is not None else float("nan"))
        else:
            txt += "out=  n/a  (超出 12k Nyquist)"
        print(txt)

    print("\n== 判定 ==")
    # 静音判断: cap_in 近零
    if rms_in < 20 and abs(dc_in) < 30:
        print("  cap_in 接近静音(±20 LSB 内)。")
        lo = peaks(fout, mout, a.fs_out, k=3, fmin_hz=20.0)
        lo_below300 = [p for p in lo if p[0] * a.fs_out < 300]
        if lo_below300:
            f_, m_ = lo_below300[0]
            print(f"  -> cap_out 有 {f_*a.fs_out:.1f}Hz 低频峰({m_:.1f}dB), cap_in 干净:")
            print("     判定: ASRC 极限环。开启 ASRC_DITHER(±8 LSB dither) 修复。")
        else:
            print("  -> cap_out 未见明显低频峰, 嗡嗡可能在其他频段或来自 7100/放大链路。")
    else:
        print("  cap_in 非静音(有实际信号), 此项只看嗡嗡: 需在手机音量最小、无声音时重抓。")

    if rms_in >= 20:
        e4_6_in = band_energy_db(fin, min_, 4000 / a.fs_in, 6000 / a.fs_in)
        e4_6_out = band_energy_db(fout, mout, 4000 / a.fs_out, 6000 / a.fs_out)
        if e4_6_out > e4_6_in + 6:
            print("  cap_out 4~6k 比 cap_in 强 >6dB:")
            print("     判定: 下采样混叠/镜像(16k->12k)。进 ASRC 前加 LPF(fc~4k) 修复, 阶数不够再提。")
        else:
            print("  cap_out 4~6k 未明显强于 cap_in: 金属尾音可能非混叠, 需结合峰值表人工判断。")
    else:
        print("  cap_in 静音, 金属尾音判定跳过(静音场景归上面的低频嗡嗡判定)。")

    if a.csv:
        n = min(len(fin), len(fout))
        with open(a.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["freq_in_Hz", "dB_in", "freq_out_Hz", "dB_out"])
            for i in range(n):
                w.writerow([round(fin[i] * a.fs_in, 1), round(min_[i], 2),
                            round(fout[i] * a.fs_out, 1), round(mout[i], 2)])
        print(f"\n全频谱已导出: {a.csv} (可在 Excel 画图)")


if __name__ == "__main__":
    main()
