"""把 blender_k059_thumbs.py 的缩略图拼成总览：py -3 -I tools/k059_contact_sheet.py 缩略图目录 输出.jpg [列数] [格子边长]"""
import json
import os
import sys

from PIL import Image, ImageDraw, ImageFont

d, out = sys.argv[1], sys.argv[2]
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 8
cell = int(sys.argv[4]) if len(sys.argv) > 4 else 300
idx = json.load(open(os.path.join(d, 'index.json'), encoding='utf-8'))
font = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', 15)
rows = (len(idx) + cols - 1) // cols
sheet = Image.new('RGB', (cols * cell, rows * (cell + 22)), (30, 31, 34))
dr = ImageDraw.Draw(sheet)
for n, it in enumerate(idx):
    x, y = (n % cols) * cell, (n // cols) * (cell + 22)
    sheet.paste(Image.open(os.path.join(d, it['file'])).convert('RGB').resize((cell - 4, cell - 4)), (x + 2, y + 2))
    nm = it['name'].replace('KB3D_CTS_', '').replace('Collection ', 'C')[:14]
    s = it['size']
    dr.text((x + 4, y + cell - 2), f"{it['id']} {nm} {s[0]:.0f}x{s[1]:.0f}x{s[2]:.0f}", fill=(255, 220, 120), font=font)
sheet.save(out, quality=85)
print(out, sheet.size)
