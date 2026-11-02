"""把石柱山水的机位截图拼成一张总览：py -3 -I tools/pillars_sheet.py 截图目录 输出.jpg [每张宽度，默认 800] [只拼这些名字（逗号分隔）]"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

src, out = sys.argv[1], sys.argv[2]
w = int(sys.argv[3]) if len(sys.argv) > 3 else 800
only = sys.argv[4].split(',') if len(sys.argv) > 4 and sys.argv[4] else None
files = sorted(f for f in os.listdir(src) if f.lower().endswith('.png'))
if only:
    files = [f for f in files if os.path.splitext(f)[0] in only]
cols = 2 if len(files) <= 4 else 3
h = w * 9 // 16
rows = (len(files) + cols - 1) // cols
pad = 6
label = 28
sheet = Image.new('RGB', (cols * (w + pad) + pad, rows * (h + label + pad) + pad), (18, 20, 24))
draw = ImageDraw.Draw(sheet)
try:
    font = ImageFont.truetype('msyh.ttc', 18)
except OSError:
    font = ImageFont.load_default()
for i, f in enumerate(files):
    im = Image.open(os.path.join(src, f)).convert('RGB').resize((w, h), Image.LANCZOS)
    x = pad + (i % cols) * (w + pad)
    y = pad + (i // cols) * (h + label + pad)
    draw.text((x + 4, y + 2), os.path.splitext(f)[0], fill=(225, 228, 232), font=font)
    sheet.paste(im, (x, y + label))
sheet.save(out, quality=88)
print(out, sheet.size)
