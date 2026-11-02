"""相位电子园区布局图：在 k051_preview.py 拍的正交俯视图上标出生点、主要路线、高处、范围和比例尺。
py -3 -I tools/k051_layout_png.py Inbox/_k051/layout.json Inbox/_k051/prev docs/地图/K051_布局.png
"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageEnhance, ImageFont

LAYOUT, PREV, OUT = sys.argv[1:4]
L = json.load(open(LAYOUT, encoding='utf-8'))
V = json.load(open(os.path.join(PREV, 'top_view.json')))
im = Image.open(os.path.join(PREV, 'top.png')).convert('RGB')
im = ImageEnhance.Brightness(im).enhance(1.35)
im = ImageEnhance.Contrast(im).enhance(1.15)
W, H = im.size
VX = V['view_half_x']
VY = VX * H / W
k = W / (2 * VX)


def P(x, y):
    return (x + VX) * k, (VY - y) * k


pad = 150
canvas = Image.new('RGB', (W, H + pad), (24, 27, 31))
canvas.paste(im, (0, pad))
d = ImageDraw.Draw(canvas, 'RGBA')
F = lambda s: ImageFont.truetype(r'C:\Windows\Fonts\msyhbd.ttc', s)
f_title, f_lab, f_small = F(44), F(26), F(22)


def Q(x, y):
    a, b = P(x, y)
    return a, b + pad


def label(x, y, text, fill=(255, 255, 255), font=f_lab, bg=(0, 0, 0, 150)):
    a, b = Q(x, y)
    w = d.textlength(text, font=font)
    d.rounded_rectangle((a - w / 2 - 8, b - 18, a + w / 2 + 8, b + 18), 8, fill=bg)
    d.text((a - w / 2, b - 16), text, fill=fill, font=font)


# 活动范围
bx, by = L['bound']
d.rectangle((*Q(-bx, by), *Q(bx, -by)), outline=(255, 70, 60), width=5)
# 路线
ROUTE_COL = {'高架环路': (80, 220, 255), '中路（地面）': (255, 200, 60), '北侧地面': (170, 255, 120), '南侧地面': (170, 255, 120), '坡道': (255, 120, 220)}
done = set()
for name, pts in L['notes']['routes']:
    col = ROUTE_COL.get(name, (255, 255, 255))
    d.line([Q(*p) for p in pts], fill=col + (235,), width=9, joint='curve')
    if name not in done:
        done.add(name)
        mid = pts[len(pts) // 2]
        off = {'高架环路': (0, 7), '中路（地面）': (-52, 6), '北侧地面': (34, -6), '南侧地面': (34, 6), '坡道': (14, 22)}.get(name, (0, 0))
        label(mid[0] + off[0], mid[1] + off[1], name, fill=col)
# 高处
for text, x, y in L['notes']['high']:
    a, b = Q(x, y)
    d.polygon([(a, b - 16), (a + 14, b + 10), (a - 14, b + 10)], fill=(255, 230, 90), outline=(0, 0, 0))
    label(x, y - 5.5, '▲ ' + text, fill=(255, 235, 140), font=f_small)
# 出生点
COL = {'team0': (60, 140, 255), 'team1': (255, 70, 60), 'dm': (60, 230, 110)}
for kk, lst in L['spawns'].items():
    for x, y, z, yaw in lst:
        a, b = Q(x, y)
        r = 13
        d.ellipse((a - r, b - r, a + r, b + r), fill=COL[kk] + (255,), outline=(0, 0, 0), width=3)
        import math
        dx, dy = math.cos(math.radians(yaw)), -math.sin(math.radians(yaw))
        d.line((a, b, a + dx * 30, b + dy * 30), fill=COL[kk] + (255,), width=5)
# 标题、图例、比例尺
d.text((28, 22), '相位电子园区 L_TechPark · 布局（K051 高科技街道，模块放大 1.5 倍）', fill=(255, 255, 255), font=f_title)
d.text((30, 86), f'活动范围 {2 * bx:.0f} × {2 * by:.0f} 米（红框，四周和头顶 {L["ceiling"]:.0f} 米隐形墙）· 高架环路 / 基地圆台桥面 7.5 米、桥下净空约 5.8 米 · 左右镜像',
       fill=(200, 210, 220), font=f_small)
lx = W - 760
for i, (kk, t) in enumerate((('team0', '团队淘汰 蓝队（西）'), ('team1', '团队淘汰 红队（东）'), ('dm', f'死斗出生点 ×{len(L["spawns"]["dm"])}'))):
    y0 = 30 + i * 36
    d.ellipse((lx, y0, lx + 24, y0 + 24), fill=COL[kk], outline=(0, 0, 0))
    d.text((lx + 34, y0 - 4), t, fill=(230, 230, 230), font=f_small)
d.polygon([(lx + 400, 50), (lx + 414, 74), (lx + 386, 74)], fill=(255, 230, 90))
d.text((lx + 424, 44), '高处（米）', fill=(230, 230, 230), font=f_small)
d.line((lx + 386, 106, lx + 446, 106), fill=(80, 220, 255), width=8)
d.text((lx + 456, 92), '路线', fill=(230, 230, 230), font=f_small)
sx0, sy0 = 40, H + pad - 50
d.rectangle((sx0, sy0, sx0 + 50 * k, sy0 + 12), fill=(255, 255, 255))
d.text((sx0 + 50 * k + 12, sy0 - 12), '50 米', fill=(255, 255, 255), font=f_small)
a, b = Q(0, by - 4)
d.text((W / 2 - 40, pad + 10), '北 ↑', fill=(255, 255, 255), font=f_lab)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
canvas.save(OUT)
print('[layout-png]', OUT, canvas.size)
