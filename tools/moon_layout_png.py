"""月球基地布局图：地形阴影图 + K65 俯视小图按布局贴上去 + 出生点 / 路线 / 活动范围标注。
py -3 -I tools/moon_layout_png.py 输出.png [UE 俯视截图.png 截图覆盖的半边米数]（给了截图就用截图当底图）"""
import json
import math
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W = 'D:/RobotSlice/Inbox/_moon_nasa/work'
TOP = 'D:/RobotSlice/Inbox/_k65/top'
OUT = sys.argv[1]
L = json.load(open(f'{W}/layout_final.json', encoding='utf-8'))
h = np.load(f'{W}/near.npy').astype(np.float64)
N = h.shape[0]
PPM = 1.5
X0, X1, Y0, Y1 = -540, 600, -470, 470
idx = {it['name'].replace('Bldg', ''): it for it in json.load(open(f'{TOP}/top_index.json', encoding='utf-8'))}


def P(x, y):
    return (x - X0) * PPM, (y - Y0) * PPM


# 底图
gy, gx = np.gradient(h, 2.0)
az, alt = np.radians(215), np.radians(32)     # 太阳从西南偏南来（和地图一致）
lx, ly, lz = np.cos(alt) * np.sin(az), -np.cos(alt) * np.cos(az), np.sin(alt)
nz = 1 / np.sqrt(gx * gx + gy * gy + 1)
sh = np.clip((-gx * nz * lx - gy * nz * ly + nz * lz) * 1.1, 0.05, 1)
t = np.clip((h + 10) / 140, 0, 1)
rgb = np.stack((sh * (0.55 + 0.45 * t), sh * (0.56 + 0.3 * t), sh * (0.6 + 0.05 * t)), -1)
base = Image.fromarray((rgb * 255).astype(np.uint8))
# 裁到显示范围
def crop_px(v):
    return int(v / 2.0 + N // 2)
base = base.crop((crop_px(X0), crop_px(Y0), crop_px(X1), crop_px(Y1))).resize((int((X1 - X0) * PPM), int((Y1 - Y0) * PPM)), Image.LANCZOS).convert('RGBA')
if len(sys.argv) > 3:
    shot = Image.open(sys.argv[2]).convert('RGB')
    half = float(sys.argv[3])
    # 截图（ue_moon_shots.py 的 a_top）：正中 = (0, 25)，上 = 北（-Y），覆盖 ±half 米（正方形）
    sw = shot.width / (half * 2)
    box = ((X0 + half) * sw, (Y0 - 25 + half) * sw, (X1 + half) * sw, (Y1 - 25 + half) * sw)
    base = shot.crop(tuple(int(v) for v in box)).resize(base.size, Image.LANCZOS).convert('RGBA')
else:
    # K65 俯视小图
    for it in L['items']:
        g = it['group']
        if g.startswith('P_'):
            continue
        ti = idx.get(g)
        if not ti:
            continue
        im = Image.open(f"{TOP}/{ti['file']}").convert('RGBA')
        arr = np.asarray(im).astype(np.int32)
        bg = arr[0, 0, :3]
        mask = (np.abs(arr[..., :3] - bg).sum(-1) > 14)
        arr2 = np.asarray(im).copy()
        arr2[..., 3] = mask * 255
        im = Image.fromarray(arr2.astype(np.uint8), 'RGBA')
        s = it['s'] * PPM / ti['ppm']
        im = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))), Image.LANCZOS)
        rim = im.rotate(-it['yaw'], expand=True, resample=Image.BICUBIC)
        # 图中心在组坐标 ((x0+x1)/2, (y0+y1)/2)（Blender y 向上 → UE y = -y）
        cx0, cy0 = (ti['x0'] + ti['x1']) / 2 * it['s'], -(ti['y0'] + ti['y1']) / 2 * it['s']
        r = math.radians(it['yaw'])
        wx = it['x'] + cx0 * math.cos(r) - cy0 * math.sin(r)
        wy = it['y'] + cx0 * math.sin(r) + cy0 * math.cos(r)
        px, py = P(wx, wy)
        base.alpha_composite(rim, (int(px - rim.width / 2), int(py - rim.height / 2)))
d = ImageDraw.Draw(base)
f = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 15)
fb = ImageFont.truetype(r'C:\Windows\Fonts\msyhbd.ttc', 20)
# 零件
if len(sys.argv) <= 3:
    for it in L['items']:
        if it['group'].startswith('P_'):
            r = math.radians(it['yaw'])
            c, s = math.cos(r), math.sin(r)
            pts = []
            for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                lx, ly = sx * it['w'] / 2, sy * it['d'] / 2
                pts.append(P(it['cx'] + lx * c - ly * s, it['cy'] + lx * s + ly * c))
            d.polygon(pts, fill=(205, 205, 195, 255), outline=(40, 40, 40, 255))
# 活动范围
a = L['arena']
x0, x1, y0, y1 = a['cx'] - a['hx'], a['cx'] + a['hx'], a['cy'] - a['hy'], a['cy'] + a['hy']
d.rectangle([P(x0, y0), P(x1, y1)], outline=(255, 90, 90, 255), width=2)
d.text((P(x0, y0)[0] + 6, P(x0, y0)[1] + 4), f'活动范围（隐形墙） {x1-x0:.0f}×{y1-y0:.0f} 米', fill=(255, 150, 150, 255), font=f)
# 主战场框
d.rectangle([P(-200, -150), P(200, 150)], outline=(120, 220, 255, 200), width=1)
d.text((P(-200, -150)[0] + 4, P(-200, -150)[1] - 20), '400×300 参考框', fill=(150, 225, 255, 255), font=f)
# 路线
ROUTES = [
    ('北路', [(-165, -28), (-95, -75), (-40, -92), (40, -92), (95, -75), (165, -28)], (255, 215, 80, 255)),
    ('南路', [(-165, 28), (-95, 75), (-30, 90), (30, 90), (95, 75), (165, 28)], (120, 255, 150, 255)),
    ('穿穹顶', [(-165, 8), (-135, 34), (-55, 22), (55, -22), (135, -34), (165, -8)], (120, 200, 255, 255)),
    ('外圈（环形坑）', [(-165, -8), (-262, -52), (-395, -275), (61, -296), (300, -220), (400, -60), (280, 40), (165, 8)], (235, 130, 255, 255)),
]
for name, pts, col in ROUTES:
    pp = [P(*q) for q in pts]
    d.line(pp, fill=col, width=3, joint='curve')
    mx, my = pp[len(pp) // 2]
    d.text((mx + 6, my + 6), name, fill=col, font=f)
# 名字
for it in L['items']:
    if it['pad']:
        px, py = P(it['cx'], it['cy'])
        note = it['note'].split('：')[0]
        d.text((px - 40, py - 8), note, fill=(255, 255, 255, 255), font=f, stroke_width=2, stroke_fill=(0, 0, 0, 255))
# 环形坑
for x, y, r in L['keep']:
    d.ellipse([P(x - r, y - r), P(x + r, y + r)], outline=(255, 255, 255, 180), width=1)
    d.text(P(x + r, y), '坑', fill=(255, 255, 255, 255), font=f, stroke_width=1, stroke_fill=(0, 0, 0, 255))
# 出生点
for t in L['team']:
    col = (70, 150, 255, 255) if t['name'].endswith('0') else (255, 80, 70, 255)
    px, py = P(t['x'], t['y'])
    d.ellipse([px - 11, py - 11, px + 11, py + 11], fill=col, outline=(255, 255, 255, 255), width=2)
    d.text((px - 8, py - 12), '蓝' if t['name'].endswith('0') else '红', fill=(255, 255, 255, 255), font=f)
for i, s in enumerate(L['dm']):
    px, py = P(s['x'], s['y'])
    d.ellipse([px - 9, py - 9, px + 9, py + 9], fill=(30, 30, 30, 230), outline=(255, 235, 120, 255), width=2)
    d.text((px - (4 if i < 9 else 8), py - 10), str(i + 1), fill=(255, 235, 120, 255), font=f)
# 比例尺 + 北
sx, sy = P(X0 + 30, Y1 - 30)
d.line([(sx, sy), (sx + 100 * PPM, sy)], fill=(255, 255, 255, 255), width=3)
d.text((sx, sy - 22), '100 米', fill=(255, 255, 255, 255), font=f)
d.text((P(X1 - 40, Y0 + 10)), '↑北', fill=(255, 255, 255, 255), font=fb)
base.convert('RGB').save(OUT)
print(OUT, base.size)
