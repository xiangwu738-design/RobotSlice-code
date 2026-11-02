"""结算场景声音核对（10-31）：拿游戏里录下来的本机主混音（-MechResultsRecord → Saved/Audio/Results/*_rec.wav）和同一次的日志，
对日志里每一条「[results] 声音 场景第 X 秒：名字」，在录音里那一刻附近用原始 wav 做互相关，找出这个声音实际响起的时刻，看对不对得上。
用法：py -3.11 tools/results_audio_check.py <录音.wav> <日志.log> [输出图.png]
输出：每个声音 日志时刻 / 录音里找到的时刻 / 相关系数；数字跳动那一段数一数起音次数；可选画一张录音的波形 + 标注。"""
import re
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import fftconvolve, resample_poly

REC, LOG = sys.argv[1], sys.argv[2]
PNG = sys.argv[3] if len(sys.argv) > 3 else None
SRC = 'D:/RobotSlice/SourceAssets/sfx_results/'
FILES = {'配乐': 'results_music.wav', 'MVP 聚光打亮': 'results_spot.wav', '镜头推近': 'results_whoosh_in.wav', '镜头拉远': 'results_whoosh_out.wav',
         '数字定格': 'results_lock.wav', '结算表弹出': 'results_table.wav', '底噪循环': 'results_bed.wav'}


def mono(path, sr_to=None):
    sr, x = wavfile.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    if np.abs(x).max() > 2:
        x /= 32768.0
    if sr_to and sr != sr_to:
        g = np.gcd(sr, sr_to)
        x = resample_poly(x, sr_to // g, sr // g)
        sr = sr_to
    return sr, x


sr, rec = mono(REC)
text = open(LOG, encoding='utf-8', errors='replace').read()
m = re.search(r'开始录本机主混音（场景第 (-?[\d.]+) 秒）', text)
rec0 = float(m.group(1)) if m else 0.0
cues = [(float(t), name) for t, name in re.findall(r'\[results\] 声音 场景第 (-?[\d.]+) 秒：([^（\n]+)', text)]
ticks = re.search(r'\[results\] 声音 场景第 ([\d.]+)–([\d.]+) 秒：数字跳动 (\d+) 下', text)
print(f'录音 {len(rec) / sr:.1f} 秒，录音第 0 秒 = 场景第 {rec0:.2f} 秒；日志里 {len(cues)} 条声音')
marks = []
for t, name in cues:
    key = next((k for k in FILES if name.startswith(k)), None)
    if not key:
        continue
    _, ref = mono(SRC + FILES[key], sr)
    ref = ref[:int(min(len(ref), 2.0 * sr))]
    at = t - rec0
    lo, hi = int(max(0, (at - 0.4) * sr)), int(min(len(rec), (at + 0.8) * sr + len(ref)))
    seg = rec[lo:hi]
    if len(seg) < len(ref):
        print(f'  {name:16s} 日志 {t:6.2f} 秒 —— 录音里这一段不够长')
        continue
    corr = fftconvolve(seg, ref[::-1], 'valid')
    # 归一化互相关（按滑动窗口的能量）
    e = np.sqrt(np.convolve(seg ** 2, np.ones(len(ref)), 'valid') * (ref ** 2).sum()) + 1e-12
    nc = corr / e
    k = int(np.argmax(nc))
    found = (lo + k) / sr + rec0
    marks.append((found, name))
    print(f'  {name:16s} 日志 场景第 {t:6.2f} 秒  录音里找到 场景第 {found:6.2f} 秒（差 {(found - t) * 1000:+5.0f} 毫秒）  相关 {nc[k]:.2f}')
if ticks:
    a, b, n = float(ticks.group(1)), float(ticks.group(2)), int(ticks.group(3))
    # 数字跳动：那一段 2–8 kHz 的起音（短时能量突然升高）数一数
    from scipy.signal import butter, sosfilt
    hpx = sosfilt(butter(4, [2500, 8000], 'band', fs=sr, output='sos'), rec)
    lo, hi = int((a - rec0 - 0.05) * sr), int((b - rec0 + 0.08) * sr)
    env = np.abs(hpx[lo:hi])
    win = int(0.004 * sr)
    sm = np.convolve(env, np.ones(win) / win, 'same')
    thr = np.median(sm) * 3 + 1e-6
    on = np.flatnonzero((sm[1:] > thr) & (sm[:-1] <= thr))
    on = on[np.insert(np.diff(on) > int(0.03 * sr), 0, True)] if len(on) else on
    print(f'  数字跳动         日志 场景第 {a:.2f}–{b:.2f} 秒 {n} 下；录音里那一段数到 {len(on)} 个起音')
if PNG:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams['font.sans-serif'] = ['Microsoft YaHei']
    t = np.arange(len(rec)) / sr + rec0
    fig, ax = plt.subplots(figsize=(16, 4))
    step = max(1, sr // 200)
    ax.plot(t[::step], rec[::step], lw=0.4, color='#5aa0d0')
    for f, name in marks:
        ax.axvline(f, color='#e07040', lw=0.8)
        ax.text(f, ax.get_ylim()[1] * 0.85, name, rotation=90, fontsize=8, va='top')
    if ticks:
        ax.axvspan(float(ticks.group(1)), float(ticks.group(2)), color='#f0c040', alpha=0.2)
    ax.set_xlabel('场景第几秒')
    ax.set_title('游戏里录下来的主混音 + 日志里每个声音（橙线 = 互相关找到的时刻，黄底 = 数字跳动）')
    plt.tight_layout()
    plt.savefig(PNG, dpi=80)
    print(PNG)
