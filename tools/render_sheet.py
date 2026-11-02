"""把 blend_render_items.py 渲出来的图拼成一张对照表：py -3.11 -I render_sheet.py 输出.jpg 列数 格子边长 目录:编号[,编号..]|all[=标签] ..."""
import os, sys
from PIL import Image, ImageDraw, ImageFont

out, cols, cell = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
font = ImageFont.truetype(r'C:\Windows\Fonts\msyh.ttc', max(12, cell // 16))
tiles = []
for arg in sys.argv[4:]:
    d, sel = arg.rsplit(':', 1)
    label = os.path.basename(d)
    if sel.startswith('all'):
        ids = sorted(int(f[:4]) for f in os.listdir(d) if f[:4].isdigit())
    else:
        ids = [int(x) for x in sel.split(',')]
    tiles += [(os.path.join(d, f'{k:04d}.png'), f'{label} #{k}') for k in ids]
rows = (len(tiles) + cols - 1) // cols
pad = cell // 8
sheet = Image.new('RGB', (cols * cell, rows * (cell + pad)), (30, 31, 34))
dr = ImageDraw.Draw(sheet)
for n, (p, t) in enumerate(tiles):
    x, y = (n % cols) * cell, (n // cols) * (cell + pad)
    if os.path.exists(p):
        sheet.paste(Image.open(p).convert('RGB').resize((cell - 4, cell - 4)), (x + 2, y + 2))
    dr.text((x + 4, y + cell - 2), t, fill=(255, 220, 120), font=font)
sheet.save(out, quality=88)
print(out, len(tiles))
