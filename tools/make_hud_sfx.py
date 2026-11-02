"""HUD 告警音（10-31 战斗界面沉浸感：战机式告警）全部用代码合成：正弦 / 带限方波 + 包络，48 kHz 16 位单声道。
输出到 SourceAssets/sfx_hud/*.wav（tools/ue_import_hud_sfx.py 导入成 /Game/Audio/HUD/S_hud_*），
另外拼一份按顺序的试听 SourceAssets/sfx_hud/preview/试听_全部.wav（不导入）。

设计：干净的电子音、每种告警一个声音（别都一个声音），尖的只给最急的（导弹来袭 / 锁定），持续性的告警（低装甲）低沉、柔和。
音量都压在 -7 ~ -14 dBFS 峰值，游戏里再乘设置里的「告警音量」。
用法：py -3 -I tools/make_hud_sfx.py
"""
import os
import wave

import numpy as np
from scipy import signal

SR = 48000
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'SourceAssets', 'sfx_hud')
OUT = os.path.normpath(OUT)
RNG = np.random.default_rng(20261031)


def t_axis(sec):
    return np.arange(int(SR * sec)) / SR


def env(n, attack=0.004, release=0.03, shape='lin'):
    """起音 / 收尾包络（秒）；收尾用余弦，免得「咔」一声"""
    e = np.ones(n)
    a = max(1, int(attack * SR))
    r = max(1, int(release * SR))
    e[:a] = np.linspace(0.0, 1.0, a) if shape == 'lin' else np.sin(np.linspace(0, np.pi / 2, a))
    if r < n:
        e[-r:] *= 0.5 * (1 + np.cos(np.linspace(0, np.pi, r)))
    return e


def tone(freq, sec, harmonics=((1, 1.0),), phase=0.0):
    """freq 可以是数或逐点数组（扫频）；harmonics = (倍数, 幅度)，高于 18 kHz 的谐波丢掉（带限）"""
    n = int(SR * sec)
    f = np.full(n, float(freq)) if np.isscalar(freq) else np.asarray(freq, dtype=float)
    ph = 2 * np.pi * np.cumsum(f) / SR + phase
    out = np.zeros(n)
    for k, amp in harmonics:
        mask = (f * k) < 18000
        out += amp * np.sin(ph * k) * mask
    return out


SQUARE = ((1, 1.0), (3, 1 / 3), (5, 1 / 5), (7, 1 / 7))      # 带限方波（前四个奇次谐波）
SOFTSQ = ((1, 1.0), (3, 0.22), (5, 0.08))                     # 软方波：亮但不刺
TRI = ((1, 1.0), (3, 1 / 9), (5, 1 / 25))                     # 三角波：圆
BELL = ((1, 1.0), (2, 0.18), (2.76, 0.12))                    # 带一点金属感的泛音


def lowpass(x, cutoff, order=2):
    b, a = signal.butter(order, cutoff / (SR / 2), 'low')
    return signal.lfilter(b, a, x)


def highpass(x, cutoff, order=2):
    b, a = signal.butter(order, cutoff / (SR / 2), 'high')
    return signal.lfilter(b, a, x)


def bandpass(x, lo, hi, order=2):
    b, a = signal.butter(order, [lo / (SR / 2), hi / (SR / 2)], 'band')
    return signal.lfilter(b, a, x)


def place(dst, src, at):
    i = int(at * SR)
    j = min(len(dst), i + len(src))
    dst[i:j] += src[: j - i]
    return dst


def silence(sec):
    return np.zeros(int(SR * sec))


def normalize(x, peak_db):
    m = np.max(np.abs(x))
    if m <= 0:
        return x
    return x / m * (10 ** (peak_db / 20))


def write(name, x, peak_db):
    x = normalize(x, peak_db)
    # 首尾各 1 毫秒淡入淡出兜底
    k = int(0.001 * SR)
    x[:k] *= np.linspace(0, 1, k)
    x[-k:] *= np.linspace(1, 0, k)
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype(np.int16)
    path = os.path.join(OUT, name + '.wav')
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    print(f'{name}.wav  {len(x) / SR:.3f} s  峰值 {peak_db} dBFS')
    return x


def noise(sec):
    return RNG.uniform(-1, 1, int(SR * sec))


def crush(x, hold, bits):
    """采样保持 + 降位深：数字干扰的「沙沙」"""
    y = np.repeat(x[::hold], hold)[: len(x)]
    q = 2 ** (bits - 1)
    return np.round(y * q) / q


def click(sec=0.004, center=3200):
    n = noise(sec) * np.exp(-np.linspace(0, 6, int(SR * sec)))
    return bandpass(n, center * 0.5, min(center * 2, 20000))


# ---------------------------------------------------------------- 每种告警
def make_track():
    # 被跟踪（敌人导弹在锁 / 狙击镜扫到 / 火炮对着）：柔和的中音单响，慢节奏（游戏里约 0.55 秒一下）
    x = tone(880, 0.12, ((1, 1.0), (2, 0.2))) * env(int(0.12 * SR), 0.005, 0.07)
    return write('hud_track', x, -11)


def make_lock():
    # 被锁定：更高、更亮的短促方波，快节奏（约 0.15 秒一下，连起来是急促的「嘀嘀嘀」）
    x = tone(1320, 0.085, SOFTSQ) * env(int(0.085 * SR), 0.003, 0.025)
    x = lowpass(x, 7000)
    return write('hud_lock', x, -9)


def make_missile():
    # 导弹来袭（重复）：一声往上滑的短促尖音 1650→2150 Hz，离得越近游戏里重复得越快（0.30 → 0.075 秒一下）
    n = int(0.07 * SR)
    f = np.linspace(1650, 2150, n)
    x = tone(f, 0.07, SOFTSQ) * env(n, 0.002, 0.02)
    x = lowpass(x, 8000)
    return write('hud_missile', x, -8)


def make_missile_launch():
    # 导弹发射（新的一批朝自己飞来时响一次）：两个音高快速交替的颤音 1500/2000 Hz、16 Hz，0.42 秒
    sec = 0.42
    n = int(sec * SR)
    t = np.arange(n) / SR
    gate = (np.floor(t * 16) % 2).astype(float)
    gate = lowpass(gate, 300)  # 交替处圆一点，不咔哒
    f = 1500 + 500 * gate
    x = tone(f, sec, SOFTSQ) * env(n, 0.004, 0.06)
    x = lowpass(x, 8000)
    return write('hud_missile_launch', x, -7)


def make_caution():
    # 低装甲主告警（Master Caution 式）：低沉的两个音往下走 392→311 Hz（G4→Eb4），三角波、软起音；约 1.5 秒循环一次
    a = tone(392, 0.15, TRI) * env(int(0.15 * SR), 0.012, 0.05, 'sin')
    b = tone(311, 0.19, TRI) * env(int(0.19 * SR), 0.012, 0.09, 'sin')
    sub = 0.25
    a += sub * tone(196, 0.15) * env(int(0.15 * SR), 0.012, 0.05, 'sin')
    b += sub * tone(155.5, 0.19) * env(int(0.19 * SR), 0.012, 0.09, 'sin')
    x = silence(0.42)
    place(x, a, 0.0)
    place(x, b, 0.2)
    return write('hud_caution', x, -9)


def make_overheat():
    # 推进能量耗尽 / 过热：三个往下掉的短音 990→740→550 Hz（「电量见底」）
    x = silence(0.30)
    for i, f in enumerate((990, 740, 550)):
        s = tone(f, 0.07, SOFTSQ) * env(int(0.07 * SR), 0.003, 0.03)
        place(x, s, i * 0.095)
    x = lowpass(x, 6000)
    return write('hud_overheat', x, -11)


def make_thrust_ready():
    # 推进恢复：两个往上的小音 660→990 Hz，很轻（正面确认，别抢戏）
    x = silence(0.17)
    for i, f in enumerate((660, 990)):
        s = tone(f, 0.055, ((1, 1.0), (2, 0.15))) * env(int(0.055 * SR), 0.003, 0.03)
        place(x, s, i * 0.07)
    return write('hud_thrust_ready', x, -15)


def make_stagger():
    # 失衡槽将满：两下带金属泛音的高音「嗒嗒」+ 第一下底下一点低频
    x = silence(0.24)
    for i in range(2):
        n = int(0.06 * SR)
        s = tone(2200, 0.06, BELL) * np.exp(-np.linspace(0, 9, n))
        place(x, s, i * 0.09)
    thump = tone(180, 0.08) * np.exp(-np.linspace(0, 7, int(0.08 * SR)))
    place(x, 0.6 * thump, 0.0)
    return write('hud_stagger', x, -9)


def make_net():
    # 被网住：通电的「嗡」——带限锯齿 110 Hz、22 Hz 调幅、夹几下噼啪
    sec = 0.45
    n = int(sec * SR)
    t = np.arange(n) / SR
    saw = tone(110, sec, tuple((k, 1.0 / k) for k in range(1, 36)))
    am = 0.55 + 0.45 * np.sign(np.sin(2 * np.pi * 22 * t))
    am = lowpass(am, 400)
    x = saw * am
    for at in (0.03, 0.12, 0.2, 0.31):
        place(x, 2.5 * click(0.006, 4500), at)
    x = bandpass(x, 90, 5000) * env(n, 0.005, 0.15)
    return write('hud_net', x, -11)


def make_drag():
    # 被钩爪拖拽：越来越快的棘轮「咔咔咔」+ 底下一道吃力往上走的低音
    sec = 0.52
    x = silence(sec)
    at = 0.0
    gap = 0.07
    while at < sec - 0.02:
        c = click(0.005, 3000) + 0.35 * tone(3000, 0.005) * np.exp(-np.linspace(0, 5, int(0.005 * SR)))
        place(x, 2.0 * c, at)
        at += gap
        gap = max(0.032, gap * 0.88)
    n = int(sec * SR)
    f = np.linspace(140, 210, n)
    hum = tone(f, sec, ((1, 1.0), (2, 0.4), (3, 0.2))) * env(n, 0.02, 0.12)
    x += 0.35 * hum
    return write('hud_drag', x, -10)


def make_emp():
    # 中 EMP：数字噪声（采样保持 + 降位深）+ 一道往下掉的「掉电」扫频 800→60 Hz + 随机断续
    sec = 0.8
    n = int(sec * SR)
    t = np.arange(n) / SR
    nz = crush(noise(sec), 12, 4)
    nz = bandpass(nz, 300, 9000)
    gate = (RNG.uniform(0, 1, int(sec * 60)) > 0.25).astype(float)
    gate = np.repeat(gate, n // len(gate) + 1)[:n]
    gate = lowpass(gate, 200)
    f = 60 + 740 * np.exp(-t * 5.0)
    sweep = tone(f, sec, ((1, 1.0), (2, 0.3), (3, 0.15)))
    x = (0.55 * nz * gate + 0.6 * sweep) * env(n, 0.002, 0.3)
    x *= np.exp(-t * 1.6)
    return write('hud_emp', x, -7)


def make_static():
    # EMP 干扰期间 HUD 闪断时偶尔的一下短「滋」
    sec = 0.09
    n = int(sec * SR)
    x = bandpass(crush(noise(sec), 8, 3), 800, 8000) * env(n, 0.002, 0.05)
    return write('hud_static', x, -14)


def make_power_down():
    # 机体被击毁 · 系统断电：继电器「咚」一下 + 往下掉的电流声 1400→90 Hz，越来越弱，尾巴上一点噼啪
    sec = 0.95
    n = int(sec * SR)
    t = np.arange(n) / SR
    f = 90 + 1310 * np.exp(-t * 4.2)
    whine = tone(f, sec, ((1, 1.0), (2, 0.35), (3, 0.18), (4, 0.08))) * np.exp(-t * 2.2) * env(n, 0.01, 0.25)
    x = 0.7 * whine
    thud = tone(80, 0.09) * np.exp(-np.linspace(0, 6, int(0.09 * SR)))
    place(x, 0.9 * thud, 0.0)
    place(x, 1.4 * click(0.006, 2500), 0.0)
    for at in (0.62, 0.7, 0.81):
        place(x, 0.5 * click(0.004, 5000), at)
    return write('hud_power_down', x, -8)


def make_power_up():
    # 出生 / 重生 · 系统上电：继电器「咔」+ 往上走的电流声 180→1500 Hz + 两声「就绪」
    sec = 1.0
    x = silence(sec)
    place(x, 1.2 * click(0.006, 2800), 0.0)
    rn = int(0.58 * SR)
    rt = np.arange(rn) / SR
    f = 180 + 1320 * (rt / rt[-1]) ** 1.6
    rise = tone(f, 0.58, ((1, 1.0), (2, 0.25), (3, 0.1))) * env(rn, 0.04, 0.12) * (0.35 + 0.65 * rt / rt[-1])
    place(x, 0.45 * rise, 0.03)
    for at, fr in ((0.66, 1320), (0.78, 1760)):
        b = tone(fr, 0.07, ((1, 1.0), (2, 0.15))) * env(int(0.07 * SR), 0.003, 0.04)
        place(x, 0.8 * b, at)
    return write('hud_power_up', x, -9)


def main():
    os.makedirs(os.path.join(OUT, 'preview'), exist_ok=True)
    order = [make_power_up, make_track, make_lock, make_missile_launch, make_missile, make_caution, make_overheat,
             make_thrust_ready, make_stagger, make_net, make_drag, make_emp, make_static, make_power_down]
    parts = []
    for fn in order:
        x = fn()
        if fn is make_track:
            # 试听：按游戏里的节奏连放几下
            seq = silence(2.2)
            for k in range(4):
                place(seq, x, k * 0.55)
            x = seq
        elif fn is make_lock:
            seq = silence(1.4)
            for k in range(8):
                place(seq, x, k * 0.15)
            x = seq
        elif fn is make_missile:
            seq = silence(2.4)
            at, gap = 0.0, 0.30
            while at < 2.2:
                place(seq, x, at)
                at += gap
                gap = max(0.075, gap * 0.86)
            x = seq
        elif fn is make_caution:
            seq = silence(3.2)
            for k in range(2):
                place(seq, x, k * 1.5)
            x = seq
        parts.append(x)  # 保持各自的电平，听得出谁轻谁响
        parts.append(silence(0.6))
    allx = np.concatenate(parts)
    pcm = np.clip(np.round(allx * 32767), -32768, 32767).astype(np.int16)
    with wave.open(os.path.join(OUT, 'preview', '试听_全部.wav'), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())
    print('试听 preview/试听_全部.wav', f'{len(allx) / SR:.1f} s')


if __name__ == '__main__':
    main()
