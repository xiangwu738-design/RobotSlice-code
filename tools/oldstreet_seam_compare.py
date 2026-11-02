"""比瞬移实测的帧：py -3 tools/oldstreet_seam_compare.py 结果目录 [对比总览.jpg] [比较组结果目录...]
每个机位：基准 = 瞬移前稳定下来的那张。每张（瞬移后第 k 帧）算和基准的差（0–255）：全图平均、最差 1% 的 32×32 块、亮度变化。总览：基准 / 第 0、1、2、3、5、8、16 帧，下一行是差 ×4。
"""
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.stdout.reconfigure(encoding='utf-8')
D = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(D, 'compare.jpg')
seq = json.load(open(os.path.join(D, 'seq.json'), encoding='utf-8'))['seq']
files = sorted(f for f in os.listdir(D) if f.startswith('f') and f.endswith('.png'))
print('帧数', len(files), '序列', len(seq))
n = min(len(files), len(seq))


def load(i):
    return np.asarray(Image.open(os.path.join(D, files[i])).convert('RGB'), dtype=np.float32)


cases = []
for s in seq:
    if s['case'] not in cases:
        cases.append(s['case'])
font = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 18)
W, H = 320, 180
KS = [0, 1, 2, 3, 5, 8, 16]
sheet = Image.new('RGB', (W * (len(KS) + 1), (H * 2 + 4) * len(cases)), (15, 15, 15))
dr = ImageDraw.Draw(sheet)
for r, c in enumerate(cases):
    idx = [i for i in range(n) if seq[i]['case'] == c]
    pre = [i for i in idx if seq[i]['kind'] == 'pre']
    post = {seq[i]['k']: i for i in idx if seq[i]['kind'] == 'post'}
    if not pre or not post:
        continue
    base = load(pre[0])
    y = r * (H * 2 + 4)
    sheet.paste(Image.fromarray(base.astype(np.uint8)).resize((W, H)), (0, y))
    dr.text((4, y + 2), f'{c} 瞬移前', fill=(255, 255, 0), font=font)
    noise = float(np.abs(load(pre[1]) - base).mean()) if len(pre) > 1 else float('nan')
    print(f'== {c}  静止噪声（两张基准之差）{noise:.2f}')
    print('  第k帧  全图平均差  最差1%块  亮度变化')
    for j, k in enumerate(KS):
        if k not in post:
            continue
        im = load(post[k])
        d = np.abs(im - base).mean(axis=2)
        h, w = d.shape
        bh, bw = h // 32, w // 32
        blocks = d[:bh * 32, :bw * 32].reshape(bh, 32, bw, 32).mean(axis=(1, 3))
        m, p, b = float(d.mean()), float(np.percentile(blocks, 99)), float(im.mean() - base.mean())
        print(f'  {k:4d}  {m:9.2f}  {p:8.2f}  {b:+7.2f}')
        sheet.paste(Image.fromarray(im.astype(np.uint8)).resize((W, H)), ((j + 1) * W, y))
        dr.text(((j + 1) * W + 4, y + 2), f'第 {k} 帧', fill=(255, 255, 0), font=font)
        diff = np.clip(np.abs(im - base) * 4, 0, 255).astype(np.uint8)
        sheet.paste(Image.fromarray(diff).resize((W, H)), ((j + 1) * W, y + H))
        dr.text(((j + 1) * W + 4, y + H + 2), f'差×4  均 {m:.1f}', fill=(255, 255, 0), font=font)
sheet.save(OUT, quality=88)
print('写好', OUT, sheet.size)
