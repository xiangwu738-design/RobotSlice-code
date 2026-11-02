"""K65 俯视图拼成同一比例的总览（2 像素 = 1 米），每张标 编号 名字 尺寸 顶高，20 米网格。py -3 -I tools/k65_top_sheet.py Inbox/_k65/top 输出.jpg"""
import json, os, sys
from PIL import Image, ImageDraw, ImageFont
d, out = sys.argv[1], sys.argv[2]
idx = json.load(open(f'{d}/top_index.json', encoding='utf-8'))
PPM = 2.0
f = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 14)
W = 1900
x = y = 6
rowh = 0
items = []
for it in idx:
    im = Image.open(f"{d}/{it['file']}").convert('RGB')
    w, h = int((it['x1'] - it['x0']) * PPM), int((it['y1'] - it['y0']) * PPM)
    im = im.resize((w, h))
    dr = ImageDraw.Draw(im)
    # 网格对准世界原点（组的原点，也是摆放原点）
    for gx in range(-200, 201, 20):
        px = (gx - it['x0']) * PPM
        if 0 <= px < w:
            dr.line([(px, 0), (px, h)], fill=(0, 200, 255) if gx else (255, 80, 80), width=1)
    for gy in range(-200, 201, 20):
        py = (it['y1'] - gy) * PPM
        if 0 <= py < h:
            dr.line([(0, py), (w, py)], fill=(0, 200, 255) if gy else (255, 80, 80), width=1)
    cell_w = max(w, 190)
    if x + cell_w > W:
        x = 6
        y += rowh + 26
        rowh = 0
    items.append((im, x, y, it))
    x += cell_w + 8
    rowh = max(rowh, h)
H = y + rowh + 30
s = Image.new('RGB', (W, H), (24, 25, 28))
dr = ImageDraw.Draw(s)
for im, x, y, it in items:
    s.paste(im, (x, y + 20))
    dr.text((x, y), f"{it['k']} {it['name'].replace('Bldg','')} {it['x1']-it['x0']-8:.0f}x{it['y1']-it['y0']-8:.0f} 顶{it['zmax']:.0f}", fill=(255, 225, 130), font=f)
s.save(out, quality=88)
print(out, s.size)
