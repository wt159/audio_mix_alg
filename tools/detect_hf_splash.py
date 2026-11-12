#!/usr/bin/env python3
# 高频溅射（HF splash）检测器：定位 WAV 中的瞬态高频毛刺事件。
# 背景（票 #21 讨论链）：硬削波在波形角溅射宽带高频，稀疏瞬态在整段频谱占比里不可见，
# 需要时间分辨的局部检测——对每个 STFT 帧算「高频带 ÷ 参照带」比值，与滑动中位数
# 基线比较，超出阈值即报一次溅射事件。
# 用法: python3 tools/detect_hf_splash.py <wav> [<wav>...] [--thresh-db 6] [--top 8]
# 盲态安全：只分析给定文件，不涉及解盲钥匙；输出不含任何链路身份。

import argparse
import os
import sys

import numpy as np
import soundfile as sf


def read_mono(path):
    try:
        x, fs = sf.read(path, dtype="float64")
    except OSError as e:
        sys.exit(f"error: cannot read {path}: {e}")
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x, fs


def splash_series(x, fs, hf_lo, hf_hi, ref_lo, ref_hi):
    """逐帧「高频带 ÷ 参照带」比值与其滑动中位数基线（dB）。"""
    win, hop = 2048, 1024
    n = max(1, (len(x) - win) // hop + 1)
    frames = np.stack([x[k * hop:k * hop + win] * np.hanning(win) for k in range(n)])
    spec = np.abs(np.fft.rfft(frames, axis=1)) ** 2
    freqs = np.fft.rfftfreq(win, 1.0 / fs)
    hf = spec[:, (freqs >= hf_lo) & (freqs <= hf_hi)].sum(axis=1)
    ref = spec[:, (freqs >= ref_lo) & (freqs < ref_hi)].sum(axis=1)
    ratio = 10 * np.log10(hf + 1e-14) - 10 * np.log10(ref + 1e-14)
    half = max(1, int(round(0.75 * fs / hop)))  # ±0.75 s 滑窗中位数基线
    pad = np.pad(ratio, half, mode="edge")
    windows = np.lib.stride_tricks.sliding_window_view(pad, 2 * half + 1)
    return ratio - np.median(windows, axis=1), hop / fs


def merge_runs(hit, frame_s):
    """合并命中帧为事件（间隔 ≤0.15 s 视为同一事件），返回每事件最强帧下标。"""
    events = []
    i = 0
    while i < len(hit):
        if not hit[i]:
            i += 1
            continue
        j = i
        while j + 1 < len(hit) and (hit[j + 1] or (hit[min(j + 2, len(hit) - 1)]
                                                   and (j + 2 - i) * frame_s < 0.15)):
            j += 1
        events.append((i, j))
        i = j + 1
    return events


def main():
    ap = argparse.ArgumentParser(description="瞬态高频溅射检测（盲态安全）")
    ap.add_argument("files", nargs="+", help="待检测的 WAV 文件")
    ap.add_argument("--thresh-db", type=float, default=6.0, help="超出局部基线的阈值 dB（默认 6）")
    ap.add_argument("--top", type=int, default=8, help="每文件最多报告的事件数（默认 8，按强度排）")
    ap.add_argument("--hf", nargs=2, type=float, default=[10e3, 16.5e3],
                    metavar="LO HI", help="高频带 Hz（默认 10000 16500）")
    ap.add_argument("--ref", nargs=2, type=float, default=[2e3, 4e3],
                    metavar="LO HI", help="参照带 Hz（默认 2000 4000）")
    ap.add_argument("--diff-vs", default=None, metavar="ORIG",
                    help="差分模式：报告处理文件相对 ORIG 在同一时刻的溅射增量（链路引入成分），"
                         "而非绝对瞬态（钢琴起音天然宽带，绝对模式会全部命中）")
    args = ap.parse_args()

    if args.diff_vs is not None and len(args.files) != 1:
        sys.exit("error: --diff-vs 模式每次只接受一个处理文件")

    for path in args.files:
        if not os.path.isfile(path):
            sys.exit(f"error: no such file: {path}")
        x, fs = read_mono(path)
        excess, frame_s = splash_series(x, fs, args.hf[0], args.hf[1],
                                        args.ref[0], args.ref[1])
        name = os.path.basename(path)
        if args.diff_vs:
            if not os.path.isfile(args.diff_vs):
                sys.exit(f"error: no such file: {args.diff_vs}")
            x0, fs0 = read_mono(args.diff_vs)
            if fs0 != fs:
                sys.exit(f"error: 采样率不一致: {fs} vs {fs0}")
            n = min(len(x), len(x0))
            excess0, _ = splash_series(x0[:n], fs, args.hf[0], args.hf[1],
                                       args.ref[0], args.ref[1])
            excess = excess[:len(excess0)]
            delta = excess - excess0          # 同时刻的溅射增量 = 链路引入成分
            hit = delta > args.thresh_db
            ev = merge_runs(hit, frame_s)
            if not ev:
                print(f"{name} vs {os.path.basename(args.diff_vs)}: "
                      f"无链路引入溅射（阈值 {args.thresh_db} dB）")
                continue
            peaks = sorted(((float(delta[a:b + 1].max()),
                             round((a + int(np.argmax(delta[a:b + 1]))) * frame_s, 3))
                            for a, b in ev), reverse=True)[:args.top]
            detail = ", ".join(f"{t:.3f}s (+{v:.1f}dB)" for v, t in peaks)
            print(f"{name} vs {os.path.basename(args.diff_vs)}: {len(ev)} 起链路引入溅射 | "
                  f"最强 {detail}")
            continue
        hit = excess > args.thresh_db
        ev = merge_runs(hit, frame_s)
        events = sorted((float(excess[a:b + 1].max()),
                         round((a + int(np.argmax(excess[a:b + 1]))) * frame_s, 3))
                        for a, b in ev)
        events = [{"excess_db": v, "t": t} for v, t in reversed(events)]
        if not events:
            print(f"{name}: 无溅射事件（阈值 {args.thresh_db} dB）")
            continue
        shown = events[:args.top]
        detail = ", ".join(f"{e['t']:.3f}s (+{e['excess_db']}dB)" for e in shown)
        more = f" …共 {len(events)} 起" if len(events) > len(shown) else ""
        print(f"{name}: {len(events)} 起溅射 | 最强 {detail}{more}")


if __name__ == "__main__":
    main()
