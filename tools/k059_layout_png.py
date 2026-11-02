"""城区图布局图：Blender 俯视预览（blender_k059_preview.py --only top）上标出生点、主要路线、高处 → docs/地图/K059_布局.png
py -3 -I tools/k059_layout_png.py"""
import json
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
plan = json.load(open(os.path.join(ROOT, 'Inbox', '_k059', 'plan.json'), encoding='utf-8'))
top = Image.open(os.path.join(ROOT, 'Inbox', '_k059', 'preview', 'top.png')).convert('RGB')
A = plan['arena']
W, H = top.size
span = 2 * A['half'][0] + 30
ppm = W / span


def P(x, y):
    return (W / 2 + x * ppm, H / 2 - y * ppm)


img = top.copy()
ov = Image.new('RGBA', img.size, (0, 0, 0, 0))
d = ImageDraw.Draw(ov)
F = r'C:\Windows\Fonts\msyhbd.ttc'
f_big, f_mid, f_small = ImageFont.truetype(F, 30), ImageFont.truetype(F, 22), ImageFont.truetype(F, 17)

# 场外压暗
hx, hy = A['half']
x0, y0 = P(-hx, hy)
x1, y1 = P(hx, -hy)
for box in ((0, 0, W, y0), (0, y1, W, H), (0, y0, x0, y1), (x1, y0, W, y1)):
    d.rectangle(box, fill=(10, 12, 16, 120))
d.rectangle([x0, y0, x1, y1], outline=(255, 70, 60, 255), width=4)


def route(pts, col, w=9):
    d.line([P(*p) for p in pts], fill=col, width=w, joint='curve')
    for a, b in zip(pts[:-1], pts[1:]):
        pass


# 主要路线（青）：大道、两条街；绕后（橙虚）：后巷、工地、广场、装卸场
MAIN = (60, 220, 255, 170)
FLANK = (255, 160, 40, 190)
route([(-100, 0), (100, 0)], MAIN, 12)
route([(-45, -88), (-45, 88)], MAIN, 10)
route([(45, -88), (45, 88)], MAIN, 10)
for pts in ([(-100, 45), (-64, 45), (-64, 22)], [(-100, 73), (-60, 73)], [(-26, 70), (0, 76), (22, 66), (28, 40)],
            [(-26, -40), (0, -62), (26, -40)], [(-26, -40), (-24, -20)], [(60, -78), (104, -78)], [(62, 78), (104, 78)],
            [(-100, -55), (-62, -55)], [(62, 24), (62, 78)]):
    pts2 = []
    for a, b in zip(pts[:-1], pts[1:]):
        n = max(1, int(((b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2) ** 0.5 / 5))
        for k in range(n):
            if k % 2 == 0:
                s0 = (a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n)
                s1 = (a[0] + (b[0] - a[0]) * (k + 1) / n, a[1] + (b[1] - a[1]) * (k + 1) / n)
                d.line([P(*s0), P(*s1)], fill=FLANK, width=6)

# 高处（白底标签：名字 + 高度）
HIGH = [(-87, 31, '联排屋顶 11–17 米'), (-75, 59, '消防站 20 米'), (-89, -30, '写字楼屋顶 30 米'),
        (-4, 50, '在建框架 7 / 14 米'), (-10, 6, '天桥 7 米'), (12, -7, '转播塔 12 米'), (45, -21, '高架 11 米'),
        (45, 44, '列车顶 14.6 米'), (91, 51, '旅馆屋顶 35 米'), (-8, -28, '站亭屋顶 7 米'), (-71, -52, '窄楼 22 米')]
for x, y, t in HIGH:
    px, py = P(x, y)
    tw = d.textlength(t, font=f_small)
    d.rounded_rectangle([px - tw / 2 - 6, py - 13, px + tw / 2 + 6, py + 13], 6, fill=(250, 250, 250, 225), outline=(40, 40, 40, 255))
    d.text((px - tw / 2, py - 12), t, font=f_small, fill=(20, 20, 20, 255))

# 出生点
for it in plan['items']:
    if it['kind'] != 'spawn':
        continue
    px, py = P(it['x'], it['y'])
    if it['tag'] == 'DM':
        d.ellipse([px - 11, py - 11, px + 11, py + 11], fill=(255, 225, 40, 255), outline=(0, 0, 0, 255), width=2)
    else:
        col = (40, 140, 255, 255) if it['tag'] == 'Team0' else (255, 60, 50, 255)
        d.rectangle([px - 9, py - 9, px + 9, py + 9], fill=col, outline=(0, 0, 0, 255), width=2)
k = 0
for it in plan['items']:
    if it['kind'] == 'spawn' and it['tag'] == 'DM':
        k += 1
        px, py = P(it['x'], it['y'])
        d.text((px - 6 if k < 10 else px - 11, py - 11), str(k), font=f_small, fill=(0, 0, 0, 255))
for tag, t, col in (('Team0', '蓝队出生', (40, 140, 255, 255)), ('Team1', '红队出生', (255, 60, 50, 255))):
    pts = [i for i in plan['items'] if i['kind'] == 'spawn' and i['tag'] == tag]
    px, py = P(sum(p['x'] for p in pts) / len(pts) - (0 if tag == 'Team0' else 8), max(p['y'] for p in pts) + 22)
    tw = d.textlength(t, font=f_mid)
    d.text((px - tw / 2, py - 30), t, font=f_mid, fill=col, stroke_width=3, stroke_fill=(0, 0, 0, 255))

img = Image.alpha_composite(img.convert('RGBA'), ov).convert('RGB')
# 图例条
leg = Image.new('RGB', (W, 120), (24, 26, 30))
dl = ImageDraw.Draw(leg)
dl.text((24, 12), 'K059 城区街口（L_City）· 死斗 / 团队淘汰 · 活动范围 210 × 180 米（红框），头顶 46 米封顶 · 北在上', font=f_mid, fill=(235, 235, 235))
items = [((40, 140, 255), '■ 蓝队出生（团队淘汰）'), ((255, 60, 50), '■ 红队出生'), ((255, 225, 40), '● 死斗重生点 1–12'),
         ((60, 220, 255), '━ 主路线：大道 / 西街 / 东街（高架下）'), ((255, 160, 40), '┅ 绕后：后巷 / 工地 / 广场 / 装卸场')]
x = 24
for col, t in items:
    dl.text((x, 62), t, font=f_small, fill=col)
    x += dl.textlength(t, font=f_small) + 34
out = Image.new('RGB', (W, H + 120))
out.paste(img, (0, 0))
out.paste(leg, (0, H))
os.makedirs(os.path.join(ROOT, 'docs', '地图'), exist_ok=True)
dst = os.path.join(ROOT, 'docs', '地图', 'K059_布局.png')
out.save(dst)
print('[layout] →', dst, out.size)
