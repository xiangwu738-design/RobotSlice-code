"""把自然环境音（Inbox 里的 96k/24bit 长录音）做成游戏用的无缝循环：48k / 16bit、去低频轰鸣、统一响度。
py -3 -I tools/make_nature_loops.py  → SourceAssets/nature_audio/*.wav（之后 ue_nature_audio.py 导入 /Game/Audio/Nature）

每条：从 start 秒起截 length 秒，尾部 xfade 秒与开头等功率交叉淡化（首尾接得上），高通去掉 80 Hz 以下，
响度统一到 TARGET LUFS（真峰值不超过 -1.5 dBFS），位置声转单声道（3D 定位更干净）。
"""
import json
import os
import subprocess
import sys
import tempfile

import numpy as np
from scipy.io import wavfile

ROOT = 'D:/RobotSlice'
JSE = f'{ROOT}/Inbox/D顶级自然环境音效/JSE - Forest Ambiences133个纯净的自然森林环境音效素材'
SFX = f'{ROOT}/Inbox/_sfx_lake/游戏动画音效全集/环境音效'
OUT = f'{ROOT}/SourceAssets/nature_audio'
SR = 48000
TARGET = -23.0

# 名字, 源文件, 起点秒, 长度秒, 交叉淡化秒, 单声道, 高通 Hz
LOOPS = [
    # 全图 2D 底噪
    ('bed_day', f'{JSE}/DECIDUOUS FORESTS/AMBForst_Summer Noon Deciduous Forest Wind Blowing Through Leaves Birds and Woodpecker in Background_JSE_FA.wav', 5, 110, 6, False, 90),
    ('bed_wind', f'{JSE}/CONIFEROUS FORESTS/WINDVege_Winter Afternoon Pine Forest Wind Light Wood Cracking Room Tone_JSE_FA.wav', 10, 95, 6, False, 70),
    # 位置声
    ('spot_leaves', f'{JSE}/DECIDUOUS FORESTS/AMBForst_Spring Afternoon Deciduous Forest Wind Leaves Rustling Chiffchaff_JSE_FA.wav', 4, 70, 5, True, 120),
    ('spot_birds_a', f'{JSE}/BIRDS AND AMBIENCE ELEMENTS/BIRDSong_Summer Afternoon Deciduous Forest Chaffinch Eurasian Wren_JSE_FA.wav', 1, 55, 4, True, 250),
    ('spot_birds_b', f'{JSE}/BIRDS AND AMBIENCE ELEMENTS/BIRDSong_Summer Noon Deciduous Forest Eurasian Blackcap_JSE_FA.wav', 1, 42, 4, True, 250),
    ('spot_birds_c', f'{JSE}/BIRDS AND AMBIENCE ELEMENTS/BIRDSong_Spring Afternoon Deciduous Forest Great Tit Eurasian Wren in Background Subtile Leaf Rustling_JSE_FA.wav', 2, 85, 5, True, 250),
    ('spot_meadow', f'{JSE}/BIRDS AND AMBIENCE ELEMENTS/BIRDSong_Summer Noon Edge of Mixed Forest Yellowhammer Crickets_JSE_FA.wav', 1, 66, 5, True, 200),
    ('spot_meadow2', f'{JSE}/MIXED FORESTS/AMBForst_Spring Afternoon Edge of Mixed Forest Yellowhammer Eurasian Blackcap Tree Pipit Insects_JSE_FA.wav', 2, 80, 5, True, 150),
    ('spot_woodpecker', f'{JSE}/BIRDS AND AMBIENCE ELEMENTS/BIRDMisc_Autumn Noon Coniferous Forest Woodpeckers Hammering on Trunks_JSE_FA.wav', 1, 42, 3, True, 150),
    ('spot_creak', f'{JSE}/BIRDS AND AMBIENCE ELEMENTS/WOODFric_Tree Trunks Creaking_JSE_FA.wav', 0.5, 32, 3, True, 60),
    ('spot_ruin_wind', f'{JSE}/DECIDUOUS FORESTS/WINDVege_Winter Afternoon Wind Blowing Through Defoliated Deciduous Trees_JSE_FA.wav', 10, 90, 6, True, 70),
    ('spot_crows', f'{JSE}/BIRDS AND AMBIENCE ELEMENTS/BIRDCrow_Winter Afternoon Deciduous Forest Ravens Wind Leaves Rustling_JSE_FA.wav', 1, 46, 4, True, 150),
    ('spot_lake', f'{SFX}/01水/06湖泊/湖－xh20070528.wav', 0.3, 27, 3, True, 60),
    ('spot_lake_waves', f'{SFX}/01水/06湖泊/大面积的水波声-YS070529.wav', 0.2, 10.5, 1.5, True, 60),
]


COMPRESS = {'spot_creak', 'spot_crows', 'spot_lake', 'spot_woodpecker'}   # 瞬态多的：轻压缩，免得为了峰值把整体压得太小


def decode(path, mono, hp, comp=False):
    """ffmpeg 解码成 float32、48k；先高通。"""
    af = f'highpass=f={hp}:poles=2'
    if comp:
        af += ',acompressor=threshold=0.05:ratio=4:attack=3:release=250:makeup=2'
    cmd = ['ffmpeg', '-v', 'error', '-i', path, '-af', af, '-ar', str(SR), '-ac', '1' if mono else '2', '-f', 'f32le', '-']
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    a = np.frombuffer(raw, dtype=np.float32)
    return a.reshape(-1, 1 if mono else 2).astype(np.float64)


def loudness(path):
    r = subprocess.run(['ffmpeg', '-nostats', '-hide_banner', '-i', path, '-af', 'ebur128=peak=true', '-f', 'null', '-'],
                       capture_output=True, text=True, errors='replace')
    lufs = peak = None
    for line in r.stderr.splitlines():
        s = line.strip()
        if s.startswith('I:'):
            lufs = float(s.split()[1])
        elif s.startswith('Peak:'):
            peak = float(s.split()[1])
    return lufs, peak


def make(name, src, start, length, xf, mono, hp):
    a = decode(src, mono, hp, name in COMPRESS)
    s0, n, x = int(start * SR), int(length * SR), int(xf * SR)
    if s0 + n + x > len(a):
        n = len(a) - s0 - x
    seg = a[s0:s0 + n + x].copy()
    t = np.linspace(0, np.pi / 2, x)[:, None]
    head, tail = seg[:x], seg[n:n + x]
    loop = seg[:n].copy()
    loop[:x] = head * np.sin(t) + tail * np.cos(t)   # 等功率：结尾（tail）淡出叠在开头（head）淡入上，第 n 个样本回到开头正好接上
    tmp = os.path.join(tempfile.gettempdir(), f'_nat_{name}.wav')
    wavfile.write(tmp, SR, np.clip(loop, -1, 1).astype(np.float32))
    lufs, _ = loudness(tmp)
    gain = 10 ** ((TARGET - lufs) / 20)
    loop *= gain
    pk = np.abs(loop).max()
    lim = 10 ** (-1.5 / 20)
    if pk > lim:   # 峰值超了就整体压低（不做限幅，保持动态）
        loop *= lim / pk
    out = f'{OUT}/{name}.wav'
    wavfile.write(out, SR, (np.clip(loop, -1, 1) * 32767).astype(np.int16))
    lufs2, peak2 = loudness(out)
    os.remove(tmp)
    return dict(name=name, src=os.path.basename(src), seconds=round(len(loop) / SR, 1), channels=1 if mono else 2, lufs=lufs2, peak=peak2)


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    only = sys.argv[1:]
    info = []
    for row in LOOPS:
        if only and row[0] not in only:
            continue
        if not os.path.exists(row[1]):
            print('[loops] 缺源文件', row[0], row[1])
            continue
        d = make(*row)
        info.append(d)
        print('[loops]', d)
    with open(f'{OUT}/loops.json', 'w', encoding='utf-8') as f:
        json.dump(info, f, ensure_ascii=False, indent=1)
