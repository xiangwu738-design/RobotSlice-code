"""K62 缩略图拼图：每栋一行（俯视 / 正面 / 侧面 / 斜视），标名字和尺寸。py -3 -I tools/k62_thumb_sheet.py thumbs目录 输出前缀"""
import json, os, re, sys
from PIL import Image, ImageDraw, ImageFont
src, out = sys.argv[1], sys.argv[2]
sv = json.load(open('D:/RobotSlice/Inbox/_k62/survey.json', encoding='utf-8'))
sz = {}
for o in sv['objects']:
    m = re.match(r'KB3D_SVB_(Bldg(?:LG|MD|SM)_[A-Z])', o['name'])
    if m and o['type'] == 'MESH':
        e = sz.setdefault(m.group(1), [[1e9] * 3, [-1e9] * 3, 0])
        for i in range(3):
            e[0][i] = min(e[0][i], o['min'][i]); e[1][i] = max(e[1][i], o['max'][i])
        e[2] += o['tris']
font = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 18)
for kind in ('SM', 'MD', 'LG'):
    names = sorted(n for n in sz if n.startswith('Bldg' + kind))
    H, W = 260, 300
    sheet = Image.new('RGB', (5 * (W * 2 + 8), (H * 2 + 8) * 2 + 0), (30, 32, 36))
    d = ImageDraw.Draw(sheet)
    for i, n in enumerate(names):
        col, row = i % 5, i // 5
        x0, y0 = col * (W * 2 + 8), row * (H * 2 + 8)
        for k, v in enumerate(('top', 'iso', 'front', 'side')):
            f = f'{src}/{n}_{v}.jpg'
            if not os.path.exists(f):
                continue
            im = Image.open(f).convert('RGB')
            im.thumbnail((W - 4, H - 4))
            sheet.paste(im, (x0 + (k % 2) * W + 2, y0 + (k // 2) * H + 2))
        e = sz[n]
        d.text((x0 + 4, y0 + 2), f'{n}  {e[1][0]-e[0][0]:.0f}x{e[1][1]-e[0][1]:.0f}x{e[1][2]-e[0][2]:.0f}  {e[2]//1000}k', fill=(255, 230, 90), font=font)
    sheet.save(f'{out}_{kind}.jpg', quality=85)
    print(out, kind, sheet.size)
